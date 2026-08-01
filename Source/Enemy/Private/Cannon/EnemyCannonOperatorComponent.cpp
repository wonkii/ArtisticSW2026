#include "Cannon/EnemyCannonOperatorComponent.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "Cannon/EnemyCannon.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Ship.h"
#include "Weapon/BaseWeaponComponent.h"
#include "WaveSystem/Route/EnemyWaypointMoveComponent.h"

UEnemyCannonOperatorComponent::UEnemyCannonOperatorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UEnemyCannonOperatorComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ABaseEnemy* Enemy = GetOwningEnemy())
	{
		if (UBaseHealthComponent* HealthComponent = Enemy->GetHealthComponent())
		{
			HealthComponent->OnDeathStarted.AddUniqueDynamic(
				this,
				&UEnemyCannonOperatorComponent::HandleOwnerDeathStarted);
		}
	}
}

void UEnemyCannonOperatorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ABaseEnemy* Enemy = GetOwningEnemy())
	{
		if (UBaseHealthComponent* HealthComponent = Enemy->GetHealthComponent())
		{
			HealthComponent->OnDeathStarted.RemoveDynamic(
				this,
				&UEnemyCannonOperatorComponent::HandleOwnerDeathStarted);
		}
	}

	DismountAndRelease();
	Super::EndPlay(EndPlayReason);
}

bool UEnemyCannonOperatorComponent::TryReserveCannon(AEnemyCannon* Cannon)
{
	ABaseEnemy* Enemy = GetOwningEnemy();
	if (!Enemy || !Enemy->HasAuthority() || !IsValid(Cannon) || MountedCannon)
	{
		return false;
	}

	if (HasValidReservation())
	{
		if (ReservedCannon != Cannon)
		{
			return false;
		}

		return RefreshLease();
	}

	// A reservation released externally must not prevent a later valid request.
	ResetReservationTracking();

	uint32 NewReservationId = 0;
	if (!Cannon->TryReserveForEnemy(Enemy, NewReservationId) || NewReservationId == 0)
	{
		return false;
	}

	ReservedCannon = Cannon;
	ReservationId = NewReservationId;
	BindTrackedCannon(Cannon);
	RefreshLease();
	return true;
}

bool UEnemyCannonOperatorComponent::MountReservedCannon()
{
	ABaseEnemy* Enemy = GetOwningEnemy();
	AEnemyCannon* Cannon = ReservedCannon;
	if (!Enemy
		|| !Enemy->HasAuthority()
		|| !IsValid(Cannon)
		|| MountedCannon
		|| !HasValidReservation()
		|| !Cannon->TryMountReservedEnemy(Enemy, ReservationId))
	{
		return false;
	}

	if (!CaptureOriginalState(Enemy))
	{
		Cannon->ReleaseMountedEnemy(Enemy, ReservationId);
		ResetReservationTracking();
		return false;
	}

	MountedCannon = Cannon;
	if (!ApplyMountedState(Enemy, Cannon, true))
	{
		Cannon->ReleaseMountedEnemy(Enemy, ReservationId);
		RestoreOriginalState(Enemy, Cannon, true, false);
		OriginalStateSnapshot.Reset();
		ResetReservationTracking();
		MountedCannon = nullptr;
		return false;
	}

	Enemy->ForceNetUpdate();
	return true;
}

bool UEnemyCannonOperatorComponent::DismountAndRelease()
{
	if (bIsCleaningUp)
	{
		return true;
	}
	TGuardValue<bool> CleanupGuard(bIsCleaningUp, true);

	ABaseEnemy* Enemy = GetOwningEnemy();
	AEnemyCannon* Cannon = MountedCannon ? MountedCannon.Get() : ReservedCannon.Get();
	const uint32 TrackedReservationId = ReservationId;
	const bool bWasMounted = MountedCannon != nullptr;

	UnbindTrackedCannon();

	if (Enemy && Enemy->HasAuthority() && IsValid(Cannon) && TrackedReservationId != 0)
	{
		if (bWasMounted)
		{
			Cannon->ReleaseMountedEnemy(Enemy, TrackedReservationId);
		}
		else
		{
			// Release validates the exact Enemy/token pair without requiring the
			// Enemy to remain alive.
			Cannon->ReleaseEnemyReservation(Enemy, TrackedReservationId);
		}
	}

	if (Enemy && OriginalStateSnapshot.bCaptured)
	{
		RestoreOriginalState(Enemy, Cannon, IsOwnerAlive(), bWasMounted);
	}

	ReservedCannon = nullptr;
	ReservationId = 0;
	MountedCannon = nullptr;
	LastLeaseRefreshTime = 0.0f;
	OriginalStateSnapshot.Reset();
	return true;
}

bool UEnemyCannonOperatorComponent::HasValidReservation() const
{
	const ABaseEnemy* Enemy = GetOwningEnemy();
	return IsValid(Enemy)
		&& IsValid(ReservedCannon)
		&& ReservationId != 0
		&& ReservedCannon->IsReservationValid(Enemy, ReservationId);
}

bool UEnemyCannonOperatorComponent::RefreshLease()
{
	if (!HasValidReservation())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	LastLeaseRefreshTime = World ? World->GetTimeSeconds() : 0.0f;
	return true;
}

AEnemyCannon* UEnemyCannonOperatorComponent::FindAndReserveBestCannon(
	AActor* TargetActor,
	float MaxSearchDistance)
{
	ABaseEnemy* Enemy = GetOwningEnemy();
	AShip* HostShip = FindHostShip();
	const UBaseHealthComponent* TargetHealth = IsValid(TargetActor)
		? TargetActor->FindComponentByClass<UBaseHealthComponent>()
		: nullptr;
	if (!Enemy || !Enemy->HasAuthority() || !IsValid(TargetActor) || !HostShip || !IsOwnerAlive())
	{
		return nullptr;
	}
	if (TargetHealth && TargetHealth->IsDead())
	{
		return nullptr;
	}

	if (HasValidReservation())
	{
		return ReservedCannon;
	}

	struct FCandidate
	{
		AEnemyCannon* Cannon = nullptr;
		float Score = TNumericLimits<float>::Max();
	};

	TSet<AEnemyCannon*> UniqueCannons;
	TArray<AActor*> AttachedActors;
	HostShip->GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (AEnemyCannon* Cannon = Cast<AEnemyCannon>(AttachedActor))
		{
			UniqueCannons.Add(Cannon);
		}
	}

	TInlineComponentArray<UChildActorComponent*> ChildActorComponents;
	HostShip->GetComponents(ChildActorComponents, true);
	for (const UChildActorComponent* ChildComponent : ChildActorComponents)
	{
		if (AEnemyCannon* Cannon = ChildComponent ? Cast<AEnemyCannon>(ChildComponent->GetChildActor()) : nullptr)
		{
			UniqueCannons.Add(Cannon);
		}
	}
	// ChildActor ownership/attachment can be reconstructed during BeginPlay on
	// moving Ships. The filtered iterator keeps discovery correct in that window
	// while still accepting only Cannons whose resolved HostShip is this Ship.
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AEnemyCannon> It(World); It; ++It)
		{
			if (It->GetOwningShip() == HostShip)
			{
				UniqueCannons.Add(*It);
			}
		}
	}

	const float MaxDistanceSquared = MaxSearchDistance > 0.0f
		? FMath::Square(MaxSearchDistance)
		: TNumericLimits<float>::Max();
	TArray<FCandidate> Candidates;
	for (AEnemyCannon* Cannon : UniqueCannons)
	{
		if (!IsValid(Cannon)
			|| Cannon->IsActorBeingDestroyed()
			|| Cannon->GetOwningShip() != HostShip
			|| !Cannon->IsAvailableForEnemy(Enemy))
		{
			continue;
		}

		const FVector MountLocation = Cannon->GetEnemyMountWorldTransform().GetLocation();
		const float DistanceSquared = FVector::DistSquared(Enemy->GetActorLocation(), MountLocation);
		if (DistanceSquared > MaxDistanceSquared)
		{
			continue;
		}

		float RequiredPitch = 0.0f;
		float RequiredYaw = 0.0f;
		if (!Cannon->GetRequiredAimAtWorldLocation(
			TargetActor->GetActorLocation(), RequiredPitch, RequiredYaw)
			|| !Cannon->CanAimAtWorldLocation(TargetActor->GetActorLocation()))
		{
			continue;
		}

		const float MoveDistance = FMath::Sqrt(DistanceSquared);
		const float FlightTime = FVector::Distance(
			Cannon->GetActorLocation(), TargetActor->GetActorLocation()) / 3000.0f;
		FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Cannon = Cannon;
		Candidate.Score = MoveDistance * 0.45f
			+ FMath::Abs(RequiredYaw) * 0.25f
			+ FMath::Abs(RequiredPitch) * 0.10f
			+ FlightTime * 0.10f;
	}

	Candidates.Sort([](const FCandidate& Left, const FCandidate& Right)
	{
		if (!FMath::IsNearlyEqual(Left.Score, Right.Score))
		{
			return Left.Score < Right.Score;
		}
		return GetNameSafe(Left.Cannon) < GetNameSafe(Right.Cannon);
	});

	for (const FCandidate& Candidate : Candidates)
	{
		if (TryReserveCannon(Candidate.Cannon))
		{
			return Candidate.Cannon;
		}
	}
	return nullptr;
}

bool UEnemyCannonOperatorComponent::GetReservedMountGoal(FVector& OutWorldGoal) const
{
	if (!HasValidReservation())
	{
		return false;
	}

	OutWorldGoal = ReservedCannon->GetEnemyMountWorldTransform().GetLocation();
	return !OutWorldGoal.ContainsNaN();
}

bool UEnemyCannonOperatorComponent::IsWithinReservedMountRadius(float AcceptanceRadius) const
{
	const ABaseEnemy* Enemy = GetOwningEnemy();
	FVector MountGoal;
	if (!Enemy || !GetReservedMountGoal(MountGoal))
	{
		return false;
	}

	const float Radius = AcceptanceRadius >= 0.0f
		? AcceptanceRadius
		: ReservedCannon->GetEnemyMountAcceptanceRadius();
	return FVector::DistSquared(Enemy->GetActorLocation(), MountGoal)
		<= FMath::Square(FMath::Max(1.0f, Radius));
}

void UEnemyCannonOperatorComponent::NotifyCannonEndPlay(const AEnemyCannon* Cannon)
{
	if (Cannon != ReservedCannon && Cannon != MountedCannon)
	{
		return;
	}

	ABaseEnemy* Enemy = GetOwningEnemy();
	if (Enemy && OriginalStateSnapshot.bCaptured)
	{
		RestoreOriginalState(Enemy, Cannon, IsOwnerAlive(), MountedCannon == Cannon);
	}

	UnbindTrackedCannon();
	ReservedCannon = nullptr;
	ReservationId = 0;
	MountedCannon = nullptr;
	LastLeaseRefreshTime = 0.0f;
	OriginalStateSnapshot.Reset();
}

void UEnemyCannonOperatorComponent::ApplyReplicatedMountState(AEnemyCannon* Cannon, bool bMounted)
{
	ABaseEnemy* Enemy = GetOwningEnemy();
	if (!Enemy || Enemy->HasAuthority())
	{
		return;
	}

	if (bMounted)
	{
		if (!OriginalStateSnapshot.bCaptured)
		{
			CaptureOriginalState(Enemy);
		}
		MountedCannon = Cannon;
		ApplyMountedState(Enemy, Cannon, false);
		return;
	}

	if (OriginalStateSnapshot.bCaptured)
	{
		RestoreOriginalState(Enemy, Cannon, true, false);
	}
	MountedCannon = nullptr;
	OriginalStateSnapshot.Reset();
}

ABaseEnemy* UEnemyCannonOperatorComponent::GetOwningEnemy() const
{
	return Cast<ABaseEnemy>(GetOwner());
}

void UEnemyCannonOperatorComponent::ResetReservationTracking()
{
	UnbindTrackedCannon();
	ReservedCannon = nullptr;
	ReservationId = 0;
	LastLeaseRefreshTime = 0.0f;
}

void UEnemyCannonOperatorComponent::BindTrackedCannon(AEnemyCannon* Cannon)
{
	if (!Cannon)
	{
		return;
	}

	Cannon->OnEndPlay.AddUniqueDynamic(
		this,
		&UEnemyCannonOperatorComponent::HandleTrackedCannonEndPlay);
}

void UEnemyCannonOperatorComponent::UnbindTrackedCannon()
{
	if (IsValid(ReservedCannon))
	{
		ReservedCannon->OnEndPlay.RemoveDynamic(
			this,
			&UEnemyCannonOperatorComponent::HandleTrackedCannonEndPlay);
	}
}

bool UEnemyCannonOperatorComponent::CaptureOriginalState(ABaseEnemy* Enemy)
{
	if (!Enemy || OriginalStateSnapshot.bCaptured)
	{
		return Enemy && OriginalStateSnapshot.bCaptured;
	}

	const UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	const UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	const USceneComponent* RootComponent = Enemy->GetRootComponent();
	if (!Movement || !Capsule || !RootComponent)
	{
		return false;
	}

	OriginalStateSnapshot.MovementMode = Movement->MovementMode;
	OriginalStateSnapshot.CapsuleCollisionEnabled = Capsule->GetCollisionEnabled();
	OriginalStateSnapshot.CapsuleCollisionProfile = Capsule->GetCollisionProfileName();
	OriginalStateSnapshot.bReplicateMovement = Enemy->IsReplicatingMovement();
	OriginalStateSnapshot.AttachParent = RootComponent->GetAttachParent();
	OriginalStateSnapshot.AttachSocket = RootComponent->GetAttachSocketName();

	if (const UBaseWeaponComponent* WeaponComponent = Enemy->GetWeaponComponent())
	{
		OriginalStateSnapshot.bWeaponWasEquipped = WeaponComponent->IsWeaponEquipped();
	}

	OriginalStateSnapshot.bCaptured = true;
	return true;
}

bool UEnemyCannonOperatorComponent::ApplyMountedState(
	ABaseEnemy* Enemy,
	AEnemyCannon* Cannon,
	bool bAuthoritative)
{
	if (!Enemy || !Cannon || !Cannon->GetEnemyMountPoint())
	{
		return false;
	}

	if (bAuthoritative)
	{
		if (AAIController* AIController = Cast<AAIController>(Enemy->GetController()))
		{
			AIController->StopMovement();
		}
		if (UEnemyWaypointMoveComponent* WaypointMove = Enemy->GetWaypointMoveComponent())
		{
			WaypointMove->StopRoute(true);
		}
		if (UBaseWeaponComponent* WeaponComponent = Enemy->GetWeaponComponent())
		{
			WeaponComponent->UnequipCurrentWeapon();
		}
	}

	if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent())
	{
		// Preserve the collision profile/responses while removing physical contact.
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	if (bAuthoritative)
	{
		Enemy->SetReplicateMovement(false);
	}

	const bool bAttached = Enemy->AttachToComponent(
		Cannon->GetEnemyMountPoint(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	if (!bAttached)
	{
		return false;
	}

	if (bAuthoritative)
	{
		if (UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent())
		{
			ASC->AddLooseGameplayTag(
				State_Operating_Cannon,
				1,
				EGameplayTagReplicationState::TagOnly);
		}
	}
	return true;
}

void UEnemyCannonOperatorComponent::RestoreOriginalState(
	ABaseEnemy* Enemy,
	const AEnemyCannon* Cannon,
	bool bRestoreCharacterState,
	bool bMoveToDismountPoint)
{
	if (!Enemy)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(State_Operating_Cannon);
	}

	if (Enemy->GetAttachParentActor() == Cannon)
	{
		Enemy->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}

	if (!bRestoreCharacterState)
	{
		return;
	}

	if (bMoveToDismountPoint && IsValid(Cannon))
	{
		const FTransform DismountTransform = ResolveDismountTransform(Cannon);
		Enemy->SetActorLocationAndRotation(
			DismountTransform.GetLocation(),
			DismountTransform.Rotator(),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}

	if (IsValid(OriginalStateSnapshot.AttachParent))
	{
		Enemy->AttachToComponent(
			OriginalStateSnapshot.AttachParent,
			FAttachmentTransformRules::KeepWorldTransform,
			OriginalStateSnapshot.AttachSocket);
	}

	if (UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent())
	{
		if (OriginalStateSnapshot.CapsuleCollisionProfile != NAME_None)
		{
			Capsule->SetCollisionProfileName(OriginalStateSnapshot.CapsuleCollisionProfile);
		}
		Capsule->SetCollisionEnabled(OriginalStateSnapshot.CapsuleCollisionEnabled);
	}

	Enemy->SetReplicateMovement(OriginalStateSnapshot.bReplicateMovement);
	if (UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement())
	{
		Movement->SetMovementMode(OriginalStateSnapshot.MovementMode);
	}
	if (OriginalStateSnapshot.bWeaponWasEquipped)
	{
		if (UBaseWeaponComponent* WeaponComponent = Enemy->GetWeaponComponent())
		{
			WeaponComponent->EquipCurrentWeapon();
		}
	}
	Enemy->ForceNetUpdate();
}

FTransform UEnemyCannonOperatorComponent::ResolveDismountTransform(const AEnemyCannon* Cannon) const
{
	FTransform Result = Cannon ? Cannon->GetEnemyDismountWorldTransform() : FTransform::Identity;
	if (!Cannon)
	{
		return Result;
	}

	if (UWorld* World = GetWorld())
	{
		if (UNavigationSystemV1* NavigationSystem = UNavigationSystemV1::GetCurrent(World))
		{
			FNavLocation ProjectedLocation;
			if (NavigationSystem->ProjectPointToNavigation(
				Result.GetLocation(),
				ProjectedLocation,
				FVector(75.0f, 75.0f, 150.0f)))
			{
				Result.SetLocation(ProjectedLocation.Location);
			}
		}
	}
	return Result;
}

bool UEnemyCannonOperatorComponent::IsOwnerAlive() const
{
	const ABaseEnemy* Enemy = GetOwningEnemy();
	if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed())
	{
		return false;
	}

	const UBaseHealthComponent* HealthComponent = Enemy->GetHealthComponent();
	return !HealthComponent || !HealthComponent->IsDead();
}

AShip* UEnemyCannonOperatorComponent::FindHostShip() const
{
	const ABaseEnemy* Enemy = GetOwningEnemy();
	if (!Enemy)
	{
		return nullptr;
	}

	if (AShip* ShipOwner = Cast<AShip>(Enemy->GetOwner()))
	{
		return ShipOwner;
	}
	if (AShip* BasedShip = Cast<AShip>(APawn::GetMovementBaseActor(Enemy)))
	{
		return BasedShip;
	}
	for (AActor* Parent = Enemy->GetAttachParentActor(); Parent; Parent = Parent->GetAttachParentActor())
	{
		if (AShip* ParentShip = Cast<AShip>(Parent))
		{
			return ParentShip;
		}
	}
	return nullptr;
}

void UEnemyCannonOperatorComponent::HandleOwnerDeathStarted(UBaseHealthComponent* HealthComponent)
{
	DismountAndRelease();
}

void UEnemyCannonOperatorComponent::HandleTrackedCannonEndPlay(
	AActor* Actor,
	EEndPlayReason::Type EndPlayReason)
{
	if (Actor != ReservedCannon && Actor != MountedCannon)
	{
		return;
	}

	// The Cannon owns its own EndPlay cleanup. Do not call back into it while it
	// is being destroyed; only invalidate this component's references.
	NotifyCannonEndPlay(Cast<AEnemyCannon>(Actor));
}

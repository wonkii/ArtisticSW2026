#include "Cannon/EnemyCannon.h"

#include "BaseEnemy.h"
#include "Cannon/EnemyCannonOperatorComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Ship.h"

AEnemyCannon::AEnemyCannon()
{
	EnemyMountPoint = CreateDefaultSubobject<USceneComponent>(TEXT("EnemyMountPoint"));
	EnemyMountPoint->SetupAttachment(BaseMesh);

	EnemyDismountPoint = CreateDefaultSubobject<USceneComponent>(TEXT("EnemyDismountPoint"));
	EnemyDismountPoint->SetupAttachment(RootComponent);
}

void AEnemyCannon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AEnemyCannon, ReservedEnemy);
	DOREPLIFETIME(AEnemyCannon, MountedEnemy);
	DOREPLIFETIME(AEnemyCannon, ReservationRevision);
}

EEnemyCannonOperationState AEnemyCannon::GetOperationState() const
{
	if (IsCannonDisabled())
	{
		return EEnemyCannonOperationState::Disabled;
	}
	if (MountedEnemy)
	{
		return EEnemyCannonOperationState::EnemyMounted;
	}
	if (GetRidingPlayer())
	{
		return EEnemyCannonOperationState::PlayerControlled;
	}
	if (ReservedEnemy)
	{
		return EEnemyCannonOperationState::ReservedForEnemy;
	}
	return EEnemyCannonOperationState::Available;
}

bool AEnemyCannon::TryReserveForEnemy(ABaseEnemy* Enemy, uint32& OutReservationId)
{
	OutReservationId = 0;

	if (!HasAuthority() || !IsEnemyEligibleForReservation(Enemy))
	{
		return false;
	}

	if (GetRidingPlayer() || IsPlayerControlled() || MountedEnemy)
	{
		return false;
	}

	if (ReservedEnemy)
	{
		if (ReservedEnemy == Enemy && ReservationRevision != 0)
		{
			OutReservationId = ReservationRevision;
			return true;
		}
		return false;
	}

	ReservedEnemy = Enemy;
	AdvanceReservationRevision();
	OutReservationId = ReservationRevision;
	ForceNetUpdate();
	return true;
}

bool AEnemyCannon::ReleaseEnemyReservation(ABaseEnemy* Enemy, uint32 ReservationId)
{
	if (!HasAuthority()
		|| !Enemy
		|| ReservedEnemy != Enemy
		|| MountedEnemy
		|| ReservationId == 0
		|| ReservationRevision != ReservationId)
	{
		return false;
	}

	ReservedEnemy = nullptr;
	AdvanceReservationRevision();
	ForceNetUpdate();
	return true;
}

bool AEnemyCannon::IsReservationValid(const ABaseEnemy* Enemy, uint32 ReservationId) const
{
	return Enemy
		&& IsEnemyEligibleForReservation(Enemy)
		&& ReservedEnemy == Enemy
		&& ReservationId != 0
		&& ReservationRevision == ReservationId
		&& !GetRidingPlayer()
		&& !MountedEnemy;
}

bool AEnemyCannon::IsAvailableForEnemy(const ABaseEnemy* Enemy) const
{
	if (!IsEnemyEligibleForReservation(Enemy)
		|| GetRidingPlayer()
		|| IsPlayerControlled()
		|| MountedEnemy)
	{
		return false;
	}

	return !ReservedEnemy || ReservedEnemy == Enemy;
}

bool AEnemyCannon::TryMountReservedEnemy(ABaseEnemy* Enemy, uint32 ReservationId)
{
	if (!HasAuthority()
		|| IsCannonDisabled()
		|| !IsEnemyEligibleForReservation(Enemy)
		|| GetRidingPlayer()
		|| IsPlayerControlled()
		|| MountedEnemy
		|| ReservedEnemy != Enemy
		|| ReservationId == 0
		|| ReservationRevision != ReservationId
		|| !IsEnemyOnOwningShip(Enemy))
	{
		return false;
	}

	const float MaxDistanceSquared = FMath::Square(FMath::Max(1.0f, EnemyMountAcceptanceRadius));
	if (FVector::DistSquared(Enemy->GetActorLocation(), GetEnemyMountWorldTransform().GetLocation())
		> MaxDistanceSquared)
	{
		return false;
	}

	MountedEnemy = Enemy;
	ForceNetUpdate();
	return true;
}

bool AEnemyCannon::ReleaseMountedEnemy(ABaseEnemy* Enemy, uint32 ReservationId)
{
	if (!HasAuthority()
		|| !Enemy
		|| MountedEnemy != Enemy
		|| ReservedEnemy != Enemy
		|| ReservationId == 0
		|| ReservationRevision != ReservationId)
	{
		return false;
	}

	MountedEnemy = nullptr;
	ReservedEnemy = nullptr;
	AdvanceReservationRevision();
	ForceNetUpdate();
	return true;
}

FTransform AEnemyCannon::GetEnemyMountWorldTransform() const
{
	return EnemyMountPoint ? EnemyMountPoint->GetComponentTransform() : GetActorTransform();
}

FTransform AEnemyCannon::GetEnemyDismountWorldTransform() const
{
	return EnemyDismountPoint ? EnemyDismountPoint->GetComponentTransform() : GetActorTransform();
}

void AEnemyCannon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ABaseEnemy* ReservedEnemyAtEndPlay = ReservedEnemy;
	ABaseEnemy* MountedEnemyAtEndPlay = MountedEnemy;

	if (HasAuthority() && (ReservedEnemy || MountedEnemy))
	{
		ReservedEnemy = nullptr;
		MountedEnemy = nullptr;
		AdvanceReservationRevision();
	}

	if (ReservedEnemyAtEndPlay)
	{
		if (UEnemyCannonOperatorComponent* Operator = ReservedEnemyAtEndPlay->GetCannonOperatorComponent())
		{
			Operator->NotifyCannonEndPlay(this);
		}
	}
	if (MountedEnemyAtEndPlay && MountedEnemyAtEndPlay != ReservedEnemyAtEndPlay)
	{
		if (UEnemyCannonOperatorComponent* Operator = MountedEnemyAtEndPlay->GetCannonOperatorComponent())
		{
			Operator->NotifyCannonEndPlay(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool AEnemyCannon::CanBoard(APawn* PlayerPawn) const
{
	return Super::CanBoard(PlayerPawn)
		&& !ReservedEnemy
		&& !MountedEnemy;
}

void AEnemyCannon::OnPlayerBoarded(APawn* PlayerPawn)
{
	Super::OnPlayerBoarded(PlayerPawn);

	ensureAlwaysMsgf(
		!ReservedEnemy && !MountedEnemy,
		TEXT("AEnemyCannon allowed Player boarding while Enemy occupancy state was active."));
}

APawn* AEnemyCannon::ResolveFiringOperator() const
{
	return MountedEnemy ? Cast<APawn>(MountedEnemy) : Super::ResolveFiringOperator();
}

bool AEnemyCannon::IsEnemyEligibleForReservation(const ABaseEnemy* Enemy) const
{
	if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed())
	{
		return false;
	}

	const UBaseHealthComponent* HealthComponent = Enemy->GetHealthComponent();
	return !HealthComponent || !HealthComponent->IsDead();
}

bool AEnemyCannon::IsEnemyOnOwningShip(const ABaseEnemy* Enemy) const
{
	const AShip* OwningShip = GetOwningShip();
	if (!Enemy || !OwningShip)
	{
		return false;
	}

	if (Enemy->IsBasedOnActor(OwningShip) || APawn::GetMovementBaseActor(Enemy) == OwningShip)
	{
		return true;
	}

	for (const AActor* Parent = Enemy->GetAttachParentActor(); Parent; Parent = Parent->GetAttachParentActor())
	{
		if (Parent == OwningShip)
		{
			return true;
		}
	}

	return Enemy->GetOwner() == OwningShip;
}

void AEnemyCannon::AdvanceReservationRevision()
{
	++ReservationRevision;
	if (ReservationRevision == 0)
	{
		++ReservationRevision;
	}
}

void AEnemyCannon::OnRep_ReservedEnemy()
{
}

void AEnemyCannon::OnRep_MountedEnemy(ABaseEnemy* PreviousMountedEnemy)
{
	if (PreviousMountedEnemy && PreviousMountedEnemy != MountedEnemy)
	{
		if (UEnemyCannonOperatorComponent* Operator = PreviousMountedEnemy->GetCannonOperatorComponent())
		{
			Operator->ApplyReplicatedMountState(this, false);
		}
	}

	if (MountedEnemy)
	{
		if (UEnemyCannonOperatorComponent* Operator = MountedEnemy->GetCannonOperatorComponent())
		{
			Operator->ApplyReplicatedMountState(this, true);
		}
	}
}

void AEnemyCannon::OnRep_ReservationRevision()
{
}

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "Cannon/EnemyCannon.h"
#include "Cannon/EnemyCannonOperatorComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Ship.h"

namespace EnemyCannonMilestone4Tests
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		explicit FScopedTestWorld(const TCHAR* WorldName)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(WorldName));
			if (World && GEngine)
			{
				FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
				WorldContext.SetCurrentWorld(World);
			}
		}

		void StartPlay() const
		{
			if (World && !World->HasBegunPlay())
			{
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
			}
		}

		void EnsureActorBeginPlay(AActor* Actor) const
		{
			if (Actor && !Actor->HasActorBegunPlay())
			{
				Actor->DispatchBeginPlay();
			}
		}

		void Tick() const
		{
			if (World)
			{
				World->Tick(LEVELTICK_All, 0.01f);
			}
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
			}
		}
	};

	void AttachToShip(AActor* Actor, AShip* Ship)
	{
		if (Actor && Ship)
		{
			Actor->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyCannonMilestone4MountAndRestoreTest,
	"ArtisticSW.EnemyCannon.Milestone4.MountAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCannonMilestone4MountAndRestoreTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCannonMilestone4Tests;
	FScopedTestWorld TestWorld(TEXT("EnemyCannonMilestone4MountAndRestoreWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AShip* Ship = TestWorld.World->SpawnActor<AShip>();
	AEnemyCannon* Cannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	ABaseEnemy* Enemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	if (!TestNotNull(TEXT("Host Ship is spawned"), Ship)
		|| !TestNotNull(TEXT("EnemyCannon is spawned"), Cannon)
		|| !TestNotNull(TEXT("Enemy is spawned"), Enemy))
	{
		return false;
	}

	AttachToShip(Cannon, Ship);
	AttachToShip(Enemy, Ship);
	Cannon->SetActorRelativeLocation(FVector::ZeroVector);
	Cannon->GetEnemyMountPoint()->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));
	Cannon->GetEnemyDismountPoint()->SetRelativeLocation(FVector(-100.0f, 0.0f, 0.0f));
	Enemy->SetActorLocation(Cannon->GetEnemyMountWorldTransform().GetLocation());

	TestWorld.StartPlay();
	TestWorld.EnsureActorBeginPlay(Ship);
	TestWorld.EnsureActorBeginPlay(Cannon);
	TestWorld.EnsureActorBeginPlay(Enemy);

	UEnemyCannonOperatorComponent* Operator = Enemy->GetCannonOperatorComponent();
	UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Enemy has an OperatorComponent"), Operator)
		|| !TestNotNull(TEXT("Enemy has CharacterMovement"), Movement)
		|| !TestNotNull(TEXT("Enemy has Capsule"), Capsule)
		|| !TestNotNull(TEXT("Enemy has ASC"), ASC))
	{
		return false;
	}

	Movement->SetMovementMode(MOVE_Walking);
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Enemy->SetReplicateMovement(true);

	const EMovementMode OriginalMovementMode = Movement->MovementMode;
	const ECollisionEnabled::Type OriginalCollision = Capsule->GetCollisionEnabled();
	const FName OriginalCollisionProfile = Capsule->GetCollisionProfileName();
	USceneComponent* OriginalAttachParent = Enemy->GetRootComponent()->GetAttachParent();

	TestTrue(TEXT("Enemy reserves the nearby Cannon"), Operator->TryReserveCannon(Cannon));
	TestTrue(TEXT("Reserved Enemy mounts the Cannon"), Operator->MountReservedCannon());
	TestEqual(TEXT("Cannon enters EnemyMounted state"),
		Cannon->GetOperationState(),
		EEnemyCannonOperationState::EnemyMounted);
	TestTrue(TEXT("Cannon tracks the mounted Enemy"), Cannon->GetMountedEnemy() == Enemy);
	TestTrue(TEXT("Operator tracks the mounted Cannon"), Operator->GetMountedCannon() == Cannon);
	TestTrue(TEXT("Enemy attaches to the Cannon actor"), Enemy->GetAttachParentActor() == Cannon);
	TestTrue(TEXT("Enemy snaps to MountPoint"),
		Enemy->GetActorTransform().Equals(Cannon->GetEnemyMountWorldTransform(), 0.1f));
	TestEqual(TEXT("Mounted Enemy movement is disabled"), Movement->MovementMode, MOVE_None);
	TestEqual(TEXT("Mounted Enemy capsule remains queryable"),
		Capsule->GetCollisionEnabled(),
		ECollisionEnabled::QueryOnly);
	TestFalse(TEXT("Mounted Enemy movement replication is disabled"), Enemy->IsReplicatingMovement());
	TestTrue(TEXT("Mounted Enemy receives State.Operating.Cannon"),
		ASC->HasMatchingGameplayTag(State_Operating_Cannon));
	TestTrue(TEXT("Original state snapshot is captured"),
		Operator->GetOriginalStateSnapshot().bCaptured);

	const FVector LocationBeforeRotation = Enemy->GetActorLocation();
	Cannon->AddActorWorldRotation(FRotator(0.0f, 45.0f, 0.0f));
	TestFalse(TEXT("Mounted Enemy follows Cannon rotation"),
		Enemy->GetActorLocation().Equals(LocationBeforeRotation, 0.1f));
	TestTrue(TEXT("Enemy remains snapped after Cannon rotation"),
		Enemy->GetActorTransform().Equals(Cannon->GetEnemyMountWorldTransform(), 0.1f));

	const FTransform ExpectedDismountTransform = Cannon->GetEnemyDismountWorldTransform();
	TestTrue(TEXT("Normal Dismount succeeds"), Operator->DismountAndRelease());
	TestEqual(TEXT("Cannon returns to Available"),
		Cannon->GetOperationState(),
		EEnemyCannonOperationState::Available);
	TestNull(TEXT("Cannon clears MountedEnemy"), Cannon->GetMountedEnemy());
	TestNull(TEXT("Cannon clears ReservedEnemy"), Cannon->GetReservedEnemy());
	TestNull(TEXT("Operator clears MountedCannon"), Operator->GetMountedCannon());
	TestNull(TEXT("Operator clears ReservedCannon"), Operator->GetReservedCannon());
	TestFalse(TEXT("Operating tag is removed"),
		ASC->HasMatchingGameplayTag(State_Operating_Cannon));
	TestEqual(TEXT("MovementMode is restored"), Movement->MovementMode, OriginalMovementMode);
	TestEqual(TEXT("Capsule collision mode is restored"), Capsule->GetCollisionEnabled(), OriginalCollision);
	TestEqual(TEXT("Capsule profile is restored"), Capsule->GetCollisionProfileName(), OriginalCollisionProfile);
	TestTrue(TEXT("Movement replication is restored"), Enemy->IsReplicatingMovement());
	TestTrue(TEXT("Original attach parent is restored"),
		Enemy->GetRootComponent()->GetAttachParent() == OriginalAttachParent);
	TestTrue(TEXT("Enemy moves to the authored DismountPoint"),
		Enemy->GetActorLocation().Equals(ExpectedDismountTransform.GetLocation(), 1.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyCannonMilestone4ValidationAndDeathCleanupTest,
	"ArtisticSW.EnemyCannon.Milestone4.ValidationAndDeathCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCannonMilestone4ValidationAndDeathCleanupTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCannonMilestone4Tests;
	FScopedTestWorld TestWorld(TEXT("EnemyCannonMilestone4ValidationAndDeathWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AShip* ShipA = TestWorld.World->SpawnActor<AShip>();
	AShip* ShipB = TestWorld.World->SpawnActor<AShip>();
	AEnemyCannon* Cannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	ABaseEnemy* Enemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	if (!TestNotNull(TEXT("Ship A is spawned"), ShipA)
		|| !TestNotNull(TEXT("Ship B is spawned"), ShipB)
		|| !TestNotNull(TEXT("EnemyCannon is spawned"), Cannon)
		|| !TestNotNull(TEXT("Enemy is spawned"), Enemy))
	{
		return false;
	}

	AttachToShip(Cannon, ShipA);
	AttachToShip(Enemy, ShipB);
	Cannon->SetActorRelativeLocation(FVector::ZeroVector);
	Cannon->GetEnemyMountPoint()->SetRelativeLocation(FVector(50.0f, 0.0f, 0.0f));
	Enemy->SetActorLocation(Cannon->GetEnemyMountWorldTransform().GetLocation());

	TestWorld.StartPlay();
	TestWorld.EnsureActorBeginPlay(ShipA);
	TestWorld.EnsureActorBeginPlay(ShipB);
	TestWorld.EnsureActorBeginPlay(Cannon);
	TestWorld.EnsureActorBeginPlay(Enemy);

	UEnemyCannonOperatorComponent* Operator = Enemy->GetCannonOperatorComponent();
	UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Enemy has an OperatorComponent"), Operator)
		|| !TestNotNull(TEXT("Enemy has ASC"), ASC))
	{
		return false;
	}

	TestTrue(TEXT("Reservation can be acquired before final ship validation"),
		Operator->TryReserveCannon(Cannon));
	TestFalse(TEXT("Enemy on another Ship cannot mount"), Operator->MountReservedCannon());
	TestTrue(TEXT("Failed mount preserves the reservation for retry"), Operator->HasValidReservation());
	Operator->DismountAndRelease();

	Enemy->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	AttachToShip(Enemy, ShipA);
	Enemy->SetActorLocation(
		Cannon->GetEnemyMountWorldTransform().GetLocation()
		+ FVector(Cannon->GetEnemyMountAcceptanceRadius() + 100.0f, 0.0f, 0.0f));
	TestTrue(TEXT("Enemy reserves Cannon for distance validation"), Operator->TryReserveCannon(Cannon));
	TestFalse(TEXT("Enemy outside Mount radius cannot mount"), Operator->MountReservedCannon());
	TestTrue(TEXT("Distance failure preserves the reservation"), Operator->HasValidReservation());

	Enemy->SetActorLocation(Cannon->GetEnemyMountWorldTransform().GetLocation());
	TestTrue(TEXT("Enemy mounts after entering the acceptance radius"), Operator->MountReservedCannon());
	TestTrue(TEXT("Mounted Enemy has operating tag"),
		ASC->HasMatchingGameplayTag(State_Operating_Cannon));

	UBaseHealthComponent* HealthComponent = Enemy->GetHealthComponent();
	if (!TestNotNull(TEXT("Enemy has a HealthComponent"), HealthComponent))
	{
		return false;
	}
	AddExpectedError(
		TEXT("EnemyCorpseStorageClass is not configured"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	HealthComponent->StartDeath();

	TestEqual(TEXT("Death releases mounted Cannon"),
		Cannon->GetOperationState(),
		EEnemyCannonOperationState::Available);
	TestNull(TEXT("Death clears MountedEnemy"), Cannon->GetMountedEnemy());
	TestNull(TEXT("Death clears Operator MountedCannon"), Operator->GetMountedCannon());
	TestFalse(TEXT("Death removes operating tag"),
		ASC->HasMatchingGameplayTag(State_Operating_Cannon));
	TestFalse(TEXT("Dead Enemy is detached from Cannon"), Enemy->GetAttachParentActor() == Cannon);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyCannonMilestone4CannonEndPlayRestoreTest,
	"ArtisticSW.EnemyCannon.Milestone4.CannonEndPlayRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCannonMilestone4CannonEndPlayRestoreTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCannonMilestone4Tests;
	FScopedTestWorld TestWorld(TEXT("EnemyCannonMilestone4CannonEndPlayWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AShip* Ship = TestWorld.World->SpawnActor<AShip>();
	AEnemyCannon* Cannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	ABaseEnemy* Enemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	if (!TestNotNull(TEXT("Host Ship is spawned"), Ship)
		|| !TestNotNull(TEXT("EnemyCannon is spawned"), Cannon)
		|| !TestNotNull(TEXT("Enemy is spawned"), Enemy))
	{
		return false;
	}

	AttachToShip(Cannon, Ship);
	AttachToShip(Enemy, Ship);
	Cannon->GetEnemyMountPoint()->SetRelativeLocation(FVector(50.0f, 0.0f, 0.0f));
	Enemy->SetActorLocation(Cannon->GetEnemyMountWorldTransform().GetLocation());
	TestWorld.StartPlay();
	TestWorld.EnsureActorBeginPlay(Ship);
	TestWorld.EnsureActorBeginPlay(Cannon);
	TestWorld.EnsureActorBeginPlay(Enemy);

	UEnemyCannonOperatorComponent* Operator = Enemy->GetCannonOperatorComponent();
	UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent();
	if (!TestNotNull(TEXT("Enemy has an OperatorComponent"), Operator)
		|| !TestNotNull(TEXT("Enemy has CharacterMovement"), Movement)
		|| !TestNotNull(TEXT("Enemy has Capsule"), Capsule))
	{
		return false;
	}

	Movement->SetMovementMode(MOVE_Walking);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Enemy->SetReplicateMovement(true);
	USceneComponent* OriginalAttachParent = Enemy->GetRootComponent()->GetAttachParent();

	TestTrue(TEXT("Enemy reserves Cannon"), Operator->TryReserveCannon(Cannon));
	TestTrue(TEXT("Enemy mounts Cannon"), Operator->MountReservedCannon());
	Cannon->Destroy();
	TestWorld.Tick();

	TestNull(TEXT("Cannon EndPlay clears Operator ReservedCannon"), Operator->GetReservedCannon());
	TestNull(TEXT("Cannon EndPlay clears Operator MountedCannon"), Operator->GetMountedCannon());
	TestEqual(TEXT("Cannon EndPlay clears reservation token"),
		Operator->GetReservationId(),
		static_cast<int64>(0));
	TestTrue(TEXT("Living Enemy original attach parent is restored"),
		Enemy->GetRootComponent()->GetAttachParent() == OriginalAttachParent);
	TestEqual(TEXT("Living Enemy movement is restored"), Movement->MovementMode, MOVE_Walking);
	TestEqual(TEXT("Living Enemy collision is restored"),
		Capsule->GetCollisionEnabled(),
		ECollisionEnabled::QueryAndPhysics);
	TestTrue(TEXT("Living Enemy movement replication is restored"), Enemy->IsReplicatingMovement());
	return true;
}

#endif

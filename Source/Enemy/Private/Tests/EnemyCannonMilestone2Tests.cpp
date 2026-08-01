#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseEnemy.h"
#include "BaseGameplayTags.h"
#include "Cannon/EnemyCannon.h"
#include "Components/BaseHealthComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "InteractableComponent.h"
#include "UObject/UnrealType.h"

namespace EnemyCannonMilestone2Tests
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyCannonMilestone2ReservationTest,
	"ArtisticSW.EnemyCannon.Milestone2.ReservationAndPlayerExclusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCannonMilestone2ReservationTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCannonMilestone2Tests;
	FScopedTestWorld TestWorld(TEXT("EnemyCannonMilestone2ReservationWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AEnemyCannon* Cannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	ABaseEnemy* EnemyA = TestWorld.World->SpawnActor<ABaseEnemy>();
	ABaseEnemy* EnemyB = TestWorld.World->SpawnActor<ABaseEnemy>();
	APlayerController* PlayerController = TestWorld.World->SpawnActor<APlayerController>();
	ACharacter* Player = TestWorld.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("EnemyCannon is spawned"), Cannon)
		|| !TestNotNull(TEXT("Enemy A is spawned"), EnemyA)
		|| !TestNotNull(TEXT("Enemy B is spawned"), EnemyB)
		|| !TestNotNull(TEXT("PlayerController is spawned"), PlayerController)
		|| !TestNotNull(TEXT("Player is spawned"), Player))
	{
		return false;
	}
	PlayerController->Possess(Player);

	TestEqual(TEXT("New EnemyCannon starts available"),
		Cannon->GetOperationState(),
		EEnemyCannonOperationState::Available);
	TestTrue(TEXT("Enemy A sees an empty cannon as available"), Cannon->IsAvailableForEnemy(EnemyA));

	uint32 EnemyAReservation = 0;
	TestTrue(TEXT("Enemy A can reserve an empty cannon"),
		Cannon->TryReserveForEnemy(EnemyA, EnemyAReservation));
	TestTrue(TEXT("A successful reservation returns a non-zero ID"), EnemyAReservation != 0);
	TestTrue(TEXT("ReservedEnemy points to Enemy A"), Cannon->GetReservedEnemy() == EnemyA);
	TestEqual(TEXT("Reservation changes operation state"),
		Cannon->GetOperationState(),
		EEnemyCannonOperationState::ReservedForEnemy);
	TestTrue(TEXT("Enemy A reservation validates"),
		Cannon->IsReservationValid(EnemyA, EnemyAReservation));

	uint32 IdempotentReservation = 0;
	TestTrue(TEXT("Enemy A repeated reservation is idempotent"),
		Cannon->TryReserveForEnemy(EnemyA, IdempotentReservation));
	TestEqual(TEXT("Idempotent reservation keeps the same ID"),
		IdempotentReservation,
		EnemyAReservation);

	uint32 EnemyBReservation = 123;
	TestFalse(TEXT("Enemy B cannot steal Enemy A reservation"),
		Cannon->TryReserveForEnemy(EnemyB, EnemyBReservation));
	TestEqual(TEXT("Failed reservation clears its output ID"), EnemyBReservation, static_cast<uint32>(0));
	TestFalse(TEXT("A wrong reservation ID cannot release the cannon"),
		Cannon->ReleaseEnemyReservation(EnemyA, EnemyAReservation + 1));
	TestTrue(TEXT("Wrong release leaves Enemy A reservation valid"),
		Cannon->IsReservationValid(EnemyA, EnemyAReservation));

	TestFalse(TEXT("Player cannot board while Enemy A owns the reservation"), Cannon->Board(Player));
	TestTrue(TEXT("Rejected Player boarding preserves Character possession"),
		PlayerController->GetPawn() == Player);
	TestNull(TEXT("Rejected Player boarding does not set RidingPlayer"), Cannon->GetRidingPlayer());

	TestTrue(TEXT("Enemy A can release with the matching ID"),
		Cannon->ReleaseEnemyReservation(EnemyA, EnemyAReservation));
	TestEqual(TEXT("Released cannon returns to available"),
		Cannon->GetOperationState(),
		EEnemyCannonOperationState::Available);
	TestFalse(TEXT("Released reservation ID is invalidated"),
		Cannon->IsReservationValid(EnemyA, EnemyAReservation));

	TestTrue(TEXT("Player can board after reservation release"), Cannon->Board(Player));
	TestEqual(TEXT("Player boarding changes operation state"),
		Cannon->GetOperationState(),
		EEnemyCannonOperationState::PlayerControlled);
	uint32 ReservationWhilePlayerControls = 0;
	TestFalse(TEXT("Enemy cannot reserve while Player controls the cannon"),
		Cannon->TryReserveForEnemy(EnemyB, ReservationWhilePlayerControls));

	Cannon->ForceExit();
	TestEqual(TEXT("Player exit returns cannon to available"),
		Cannon->GetOperationState(),
		EEnemyCannonOperationState::Available);
	TestTrue(TEXT("Enemy B can reserve after Player exits"),
		Cannon->TryReserveForEnemy(EnemyB, EnemyBReservation));
	TestTrue(TEXT("Enemy B receives a new revision"),
		EnemyBReservation != 0 && EnemyBReservation != EnemyAReservation);
	TestTrue(TEXT("Enemy B can release its reservation"),
		Cannon->ReleaseEnemyReservation(EnemyB, EnemyBReservation));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyCannonMilestone2EligibilityAndComponentsTest,
	"ArtisticSW.EnemyCannon.Milestone2.EligibilityAndComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCannonMilestone2EligibilityAndComponentsTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCannonMilestone2Tests;
	FScopedTestWorld TestWorld(TEXT("EnemyCannonMilestone2EligibilityWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AEnemyCannon* Cannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	ABaseEnemy* Enemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	if (!TestNotNull(TEXT("EnemyCannon is spawned"), Cannon)
		|| !TestNotNull(TEXT("Enemy is spawned"), Enemy))
	{
		return false;
	}

	auto TestRepNotifyProperty = [this](const TCHAR* PropertyName)
	{
		const FProperty* Property = FindFProperty<FProperty>(AEnemyCannon::StaticClass(), FName(PropertyName));
		if (!TestNotNull(*FString::Printf(TEXT("%s property exists"), PropertyName), Property))
		{
			return;
		}
		TestTrue(*FString::Printf(TEXT("%s is replicated"), PropertyName),
			Property->HasAnyPropertyFlags(CPF_Net));
		TestTrue(*FString::Printf(TEXT("%s uses RepNotify"), PropertyName),
			Property->HasAnyPropertyFlags(CPF_RepNotify));
	};
	TestRepNotifyProperty(TEXT("ReservedEnemy"));
	TestRepNotifyProperty(TEXT("MountedEnemy"));
	TestRepNotifyProperty(TEXT("ReservationRevision"));

	USceneComponent* MountPoint = Cannon->GetEnemyMountPoint();
	USceneComponent* DismountPoint = Cannon->GetEnemyDismountPoint();
	UInteractableComponent* Interactable = Cannon->FindComponentByClass<UInteractableComponent>();
	TestNotNull(TEXT("EnemyCannon inherits an InteractableComponent"), Interactable);
	if (Interactable)
	{
		TestTrue(TEXT("EnemyCannon inherits the CannonBoard interaction tag"),
			Interactable->InteractionTag.MatchesTagExact(Interaction_CannonBoard));
	}
	TestNotNull(TEXT("EnemyMountPoint exists"), MountPoint);
	TestNotNull(TEXT("EnemyDismountPoint exists"), DismountPoint);
	if (MountPoint)
	{
		TestEqual(TEXT("EnemyMountPoint follows the yaw BaseMesh"),
			MountPoint->GetAttachParent() ? MountPoint->GetAttachParent()->GetFName() : NAME_None,
			FName(TEXT("BaseMesh")));
		TestTrue(TEXT("Mount transform API returns the component transform"),
			Cannon->GetEnemyMountWorldTransform().Equals(MountPoint->GetComponentTransform()));
	}
	if (DismountPoint)
	{
		TestTrue(TEXT("EnemyDismountPoint is attached to the cannon root"),
			DismountPoint->GetAttachParent() == Cannon->GetRootComponent());
		TestTrue(TEXT("Dismount transform API returns the component transform"),
			Cannon->GetEnemyDismountWorldTransform().Equals(DismountPoint->GetComponentTransform()));
	}

	uint32 NullReservation = 77;
	TestFalse(TEXT("Null Enemy cannot reserve"), Cannon->TryReserveForEnemy(nullptr, NullReservation));
	TestEqual(TEXT("Null reservation clears output ID"), NullReservation, static_cast<uint32>(0));

	UBaseHealthComponent* HealthComponent = Enemy->GetHealthComponent();
	if (!TestNotNull(TEXT("Enemy has a HealthComponent"), HealthComponent))
	{
		return false;
	}
	HealthComponent->InitializeWithAbilitySystem(Enemy->GetAbilitySystemComponent());
	HealthComponent->StartDeath();
	TestTrue(TEXT("Enemy is marked dead for eligibility test"), HealthComponent->IsDead());

	uint32 DeadEnemyReservation = 0;
	TestFalse(TEXT("Dead Enemy cannot reserve"),
		Cannon->TryReserveForEnemy(Enemy, DeadEnemyReservation));
	TestFalse(TEXT("Dead Enemy does not see the cannon as available"),
		Cannon->IsAvailableForEnemy(Enemy));
	return true;
}

#endif

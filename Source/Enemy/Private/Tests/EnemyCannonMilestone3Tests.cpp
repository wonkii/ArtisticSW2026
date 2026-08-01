#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseEnemy.h"
#include "Cannon/EnemyCannon.h"
#include "Cannon/EnemyCannonOperatorComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace EnemyCannonMilestone3Tests
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyCannonMilestone3ReservationLifecycleTest,
	"ArtisticSW.EnemyCannon.Milestone3.ComponentReservationLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCannonMilestone3ReservationLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCannonMilestone3Tests;
	FScopedTestWorld TestWorld(TEXT("EnemyCannonMilestone3ReservationLifecycleWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AEnemyCannon* CannonA = TestWorld.World->SpawnActor<AEnemyCannon>();
	AEnemyCannon* CannonB = TestWorld.World->SpawnActor<AEnemyCannon>();
	ABaseEnemy* Enemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	if (!TestNotNull(TEXT("Cannon A is spawned"), CannonA)
		|| !TestNotNull(TEXT("Cannon B is spawned"), CannonB)
		|| !TestNotNull(TEXT("Enemy is spawned"), Enemy))
	{
		return false;
	}
	TestWorld.StartPlay();
	TestWorld.EnsureActorBeginPlay(CannonA);
	TestWorld.EnsureActorBeginPlay(CannonB);
	TestWorld.EnsureActorBeginPlay(Enemy);

	UEnemyCannonOperatorComponent* Operator = Enemy->GetCannonOperatorComponent();
	if (!TestNotNull(TEXT("Every BaseEnemy owns a CannonOperatorComponent"), Operator))
	{
		return false;
	}

	TestFalse(TEXT("CannonOperatorComponent does not tick"), Operator->PrimaryComponentTick.bCanEverTick);
	TestNull(TEXT("Component starts without a reservation"), Operator->GetReservedCannon());
	TestEqual(TEXT("Component starts with reservation ID zero"), Operator->GetReservationId(), static_cast<int64>(0));
	TestFalse(TEXT("Component starts without a valid reservation"), Operator->HasValidReservation());

	TestTrue(TEXT("Component reserves Cannon A"), Operator->TryReserveCannon(CannonA));
	TestTrue(TEXT("Component validates its reservation"), Operator->HasValidReservation());
	TestTrue(TEXT("Component stores Cannon A"), Operator->GetReservedCannon() == CannonA);
	TestTrue(TEXT("Component stores a non-zero reservation ID"), Operator->GetReservationId() != 0);
	const int64 OriginalReservationId = Operator->GetReservationId();

	TestTrue(TEXT("Repeated reservation of Cannon A is idempotent"), Operator->TryReserveCannon(CannonA));
	TestEqual(TEXT("Repeated reservation keeps the token"), Operator->GetReservationId(), OriginalReservationId);
	TestFalse(TEXT("A valid reservation is not silently switched to Cannon B"), Operator->TryReserveCannon(CannonB));
	TestEqual(TEXT("Rejected switch leaves Cannon B available"),
		CannonB->GetOperationState(),
		EEnemyCannonOperationState::Available);

	TestFalse(TEXT("Mount without a shared HostShip is rejected"), Operator->MountReservedCannon());
	TestTrue(TEXT("Rejected mount preserves the reservation"), Operator->HasValidReservation());

	TestTrue(TEXT("DismountAndRelease releases the reservation"), Operator->DismountAndRelease());
	TestNull(TEXT("Release clears the stored Cannon"), Operator->GetReservedCannon());
	TestEqual(TEXT("Release clears the stored token"), Operator->GetReservationId(), static_cast<int64>(0));
	TestEqual(TEXT("Release returns Cannon A to available"),
		CannonA->GetOperationState(),
		EEnemyCannonOperationState::Available);
	TestTrue(TEXT("Repeated DismountAndRelease is harmless"), Operator->DismountAndRelease());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyCannonMilestone3FailureCleanupTest,
	"ArtisticSW.EnemyCannon.Milestone3.DeathAndEndPlayCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCannonMilestone3FailureCleanupTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCannonMilestone3Tests;
	FScopedTestWorld TestWorld(TEXT("EnemyCannonMilestone3FailureCleanupWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AEnemyCannon* DeathCannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	ABaseEnemy* DyingEnemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	AEnemyCannon* OwnerEndPlayCannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	ABaseEnemy* DestroyedEnemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	AEnemyCannon* DestroyedCannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	ABaseEnemy* SurvivingEnemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	if (!TestNotNull(TEXT("Death Cannon is spawned"), DeathCannon)
		|| !TestNotNull(TEXT("Dying Enemy is spawned"), DyingEnemy)
		|| !TestNotNull(TEXT("Owner EndPlay Cannon is spawned"), OwnerEndPlayCannon)
		|| !TestNotNull(TEXT("Destroyed Enemy is spawned"), DestroyedEnemy)
		|| !TestNotNull(TEXT("Destroyed Cannon is spawned"), DestroyedCannon)
		|| !TestNotNull(TEXT("Surviving Enemy is spawned"), SurvivingEnemy))
	{
		return false;
	}
	TestWorld.StartPlay();
	TestWorld.EnsureActorBeginPlay(DeathCannon);
	TestWorld.EnsureActorBeginPlay(DyingEnemy);
	TestWorld.EnsureActorBeginPlay(OwnerEndPlayCannon);
	TestWorld.EnsureActorBeginPlay(DestroyedEnemy);
	TestWorld.EnsureActorBeginPlay(DestroyedCannon);
	TestWorld.EnsureActorBeginPlay(SurvivingEnemy);

	UEnemyCannonOperatorComponent* DyingOperator =
		DyingEnemy ? DyingEnemy->GetCannonOperatorComponent() : nullptr;
	if (!TestNotNull(TEXT("Dying Enemy has an operator"), DyingOperator))
	{
		return false;
	}

	TestTrue(TEXT("Dying Enemy reserves a Cannon"), DyingOperator->TryReserveCannon(DeathCannon));
	UBaseHealthComponent* HealthComponent = DyingEnemy->GetHealthComponent();
	if (!TestNotNull(TEXT("Dying Enemy has a HealthComponent"), HealthComponent))
	{
		return false;
	}
	AddExpectedError(
		TEXT("EnemyCorpseStorageClass is not configured"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	HealthComponent->StartDeath();
	TestFalse(TEXT("Death invalidates the component reservation"), DyingOperator->HasValidReservation());
	TestNull(TEXT("Death clears the component Cannon pointer"), DyingOperator->GetReservedCannon());
	TestEqual(TEXT("Death makes the Cannon available"),
		DeathCannon->GetOperationState(),
		EEnemyCannonOperationState::Available);

	UEnemyCannonOperatorComponent* DestroyedOperator =
		DestroyedEnemy ? DestroyedEnemy->GetCannonOperatorComponent() : nullptr;
	if (!TestNotNull(TEXT("Destroyed Enemy has an operator"), DestroyedOperator))
	{
		return false;
	}

	TestTrue(TEXT("Destroyed Enemy reserves a Cannon"), DestroyedOperator->TryReserveCannon(OwnerEndPlayCannon));
	DestroyedEnemy->Destroy();
	TestWorld.Tick();
	TestEqual(TEXT("Enemy EndPlay makes its Cannon available"),
		OwnerEndPlayCannon->GetOperationState(),
		EEnemyCannonOperationState::Available);

	UEnemyCannonOperatorComponent* SurvivingOperator =
		SurvivingEnemy ? SurvivingEnemy->GetCannonOperatorComponent() : nullptr;
	if (!TestNotNull(TEXT("Surviving Enemy has an operator"), SurvivingOperator))
	{
		return false;
	}

	TestTrue(TEXT("Surviving Enemy reserves the soon-destroyed Cannon"),
		SurvivingOperator->TryReserveCannon(DestroyedCannon));
	DestroyedCannon->Destroy();
	TestWorld.Tick();
	TestNull(TEXT("Cannon EndPlay clears the component Cannon pointer"),
		SurvivingOperator->GetReservedCannon());
	TestEqual(TEXT("Cannon EndPlay clears the component token"),
		SurvivingOperator->GetReservationId(),
		static_cast<int64>(0));
	TestFalse(TEXT("Cannon EndPlay leaves no valid reservation"),
		SurvivingOperator->HasValidReservation());
	TestTrue(TEXT("Cleanup remains idempotent after Cannon EndPlay"),
		SurvivingOperator->DismountAndRelease());
	return true;
}

#endif

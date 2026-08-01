#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BaseEnemy.h"
#include "Cannon/EnemyCannon.h"
#include "Cannon/EnemyCannonOperatorComponent.h"
#include "Engine/Engine.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "Ship.h"

namespace EnemyCannonMilestone6And7Tests
{
	struct FScopedWorld
	{
		UWorld* World = nullptr;
		explicit FScopedWorld(const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, FName(Name));
			if (World && GEngine)
			{
				FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
				Context.SetCurrentWorld(World);
			}
		}
		void StartPlay() const
		{
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}
		~FScopedWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
	};

	void AttachAt(AActor* Actor, AActor* Parent, const FVector& RelativeLocation)
	{
		Actor->AttachToActor(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		Actor->SetActorRelativeLocation(RelativeLocation);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyCannonMilestone6CandidateCompetitionTest,
	"ArtisticSW.EnemyCannon.Milestone6.CandidateCompetitionAndShipFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCannonMilestone6CandidateCompetitionTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCannonMilestone6And7Tests;
	FScopedWorld TestWorld(TEXT("EnemyCannonMilestone6World"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AShip* HostShip = TestWorld.World->SpawnActor<AShip>();
	AShip* OtherShip = TestWorld.World->SpawnActor<AShip>();
	ABaseEnemy* EnemyA = TestWorld.World->SpawnActor<ABaseEnemy>();
	ABaseEnemy* EnemyB = TestWorld.World->SpawnActor<ABaseEnemy>();
	AEnemyCannon* NearCannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	AEnemyCannon* FarCannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	AEnemyCannon* OtherShipCannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	AActor* Target = TestWorld.World->SpawnActor<ATargetPoint>(
		ATargetPoint::StaticClass(), FVector(2500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!HostShip || !OtherShip || !EnemyA || !EnemyB || !NearCannon || !FarCannon || !OtherShipCannon || !Target)
	{
		AddError(TEXT("Failed to spawn milestone 6 test actors."));
		return false;
	}

	AttachAt(EnemyA, HostShip, FVector::ZeroVector);
	AttachAt(EnemyB, HostShip, FVector(0.0f, 25.0f, 0.0f));
	AttachAt(NearCannon, HostShip, FVector(100.0f, 0.0f, 0.0f));
	AttachAt(FarCannon, HostShip, FVector(350.0f, 0.0f, 0.0f));
	AttachAt(OtherShipCannon, OtherShip, FVector(10.0f, 0.0f, 0.0f));
	TestWorld.StartPlay();

	UEnemyCannonOperatorComponent* OperatorA = EnemyA->GetCannonOperatorComponent();
	UEnemyCannonOperatorComponent* OperatorB = EnemyB->GetCannonOperatorComponent();
	TestTrue(TEXT("Enemy resolves its attached Host Ship"), OperatorA->GetHostShip() == HostShip);
	TestTrue(TEXT("Near Cannon resolves the same Host Ship"), NearCannon->GetOwningShip() == HostShip);
	TestTrue(TEXT("Near Cannon is available to Enemy"), NearCannon->IsAvailableForEnemy(EnemyA));
	TestTrue(TEXT("Near Cannon can approximately aim at Target"),
		NearCannon->CanAimAtWorldLocation(Target->GetActorLocation()));
	AEnemyCannon* ChoiceA = OperatorA->FindAndReserveBestCannon(Target, 1000.0f);
	AEnemyCannon* ChoiceB = OperatorB->FindAndReserveBestCannon(Target, 1000.0f);
	TestTrue(TEXT("First Enemy reserves the best nearby Cannon"), ChoiceA == NearCannon);
	TestTrue(TEXT("Second Enemy falls through to the next candidate"), ChoiceB == FarCannon);
	TestTrue(TEXT("One Cannon never has two reserving Enemies"),
		NearCannon->GetReservedEnemy() == EnemyA && FarCannon->GetReservedEnemy() == EnemyB);
	TestNull(TEXT("Cannon on another Ship is excluded"), OtherShipCannon->GetReservedEnemy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyCannonMilestone7MovingGoalAndMountTest,
	"ArtisticSW.EnemyCannon.Milestone7.MovingGoalAndMount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCannonMilestone7MovingGoalAndMountTest::RunTest(const FString& Parameters)
{
	using namespace EnemyCannonMilestone6And7Tests;
	FScopedWorld TestWorld(TEXT("EnemyCannonMilestone7World"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	AShip* HostShip = TestWorld.World->SpawnActor<AShip>();
	ABaseEnemy* Enemy = TestWorld.World->SpawnActor<ABaseEnemy>();
	AEnemyCannon* Cannon = TestWorld.World->SpawnActor<AEnemyCannon>();
	AActor* Target = TestWorld.World->SpawnActor<ATargetPoint>(
		ATargetPoint::StaticClass(), FVector(2500.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
	if (!HostShip || !Enemy || !Cannon || !Target)
	{
		AddError(TEXT("Failed to spawn milestone 7 test actors."));
		return false;
	}

	AttachAt(Enemy, HostShip, FVector::ZeroVector);
	AttachAt(Cannon, HostShip, FVector(200.0f, 0.0f, 0.0f));
	Cannon->GetEnemyMountPoint()->SetRelativeLocation(FVector(50.0f, 0.0f, 0.0f));
	TestWorld.StartPlay();

	UEnemyCannonOperatorComponent* Operator = Enemy->GetCannonOperatorComponent();
	TestTrue(TEXT("Enemy resolves its Host Ship"), Operator->GetHostShip() == HostShip);
	TestTrue(TEXT("Cannon resolves the same Host Ship"), Cannon->GetOwningShip() == HostShip);
	TestTrue(TEXT("Cannon can approximately aim at Target"),
		Cannon->CanAimAtWorldLocation(Target->GetActorLocation()));
	TestTrue(TEXT("Enemy automatically finds and reserves the Cannon"),
		Operator->FindAndReserveBestCannon(Target, 1000.0f) == Cannon);
	FVector FirstGoal;
	FVector MovedGoal;
	TestTrue(TEXT("Reserved MountPoint supplies a live movement goal"), Operator->GetReservedMountGoal(FirstGoal));
	HostShip->AddActorWorldOffset(FVector(175.0f, 80.0f, 0.0f));
	HostShip->AddActorWorldRotation(FRotator(0.0f, 20.0f, 0.0f));
	TestTrue(TEXT("Goal can be read again after Ship movement"), Operator->GetReservedMountGoal(MovedGoal));
	TestFalse(TEXT("Mount goal is not a cached initial world position"), FirstGoal.Equals(MovedGoal, 0.1f));

	Enemy->SetActorLocation(MovedGoal);
	TestTrue(TEXT("Enemy reaches the current MountPoint radius"), Operator->IsWithinReservedMountRadius());
	TestTrue(TEXT("Enemy mounts after the reservation and distance revalidation"), Operator->MountReservedCannon());
	TestTrue(TEXT("Enemy is snapped to the moving Cannon MountPoint"),
		Enemy->GetActorTransform().Equals(Cannon->GetEnemyMountWorldTransform(), 0.1f));
	TestTrue(TEXT("Cannon tracks the mounted Enemy"), Cannon->GetMountedEnemy() == Enemy);
	return true;
}

#endif

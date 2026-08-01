#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Cannon.h"
#include "Cannonball.h"
#include "CannonMilestone1TestTypes.h"
#include "BaseGameplayTags.h"
#include "CollisionChannels.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InteractableComponent.h"

namespace CannonMilestone1Tests
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
	FCannonMilestone1BoardingHooksTest,
	"ArtisticSW.Cannon.Milestone1.BoardingHooks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCannonMilestone1BoardingHooksTest::RunTest(const FString& Parameters)
{
	using namespace CannonMilestone1Tests;
	FScopedTestWorld TestWorld(TEXT("CannonMilestone1BoardingWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	APlayerController* PlayerController = TestWorld.World->SpawnActor<APlayerController>();
	ACharacter* Player = TestWorld.World->SpawnActor<ACharacter>();
	ACannonMilestone1TestDouble* Cannon = TestWorld.World->SpawnActor<ACannonMilestone1TestDouble>();
	if (!TestNotNull(TEXT("PlayerController is spawned"), PlayerController)
		|| !TestNotNull(TEXT("Player is spawned"), Player)
		|| !TestNotNull(TEXT("Test cannon is spawned"), Cannon))
	{
		return false;
	}

	PlayerController->Possess(Player);
	UInteractableComponent* Interactable = Cannon->FindComponentByClass<UInteractableComponent>();
	TestNotNull(TEXT("Cannon has an InteractableComponent"), Interactable);
	if (Interactable)
	{
		TestTrue(TEXT("Cannon defaults to the CannonBoard interaction tag"),
			Interactable->InteractionTag.MatchesTagExact(Interaction_CannonBoard));
		TestEqual(TEXT("Cannon interaction collision is query enabled"),
			Interactable->GetCollisionEnabled(),
			ECollisionEnabled::QueryOnly);
		TestEqual(TEXT("Cannon blocks the interact trace channel"),
			Interactable->GetCollisionResponseToChannel(ECC_Interactable),
			ECR_Block);
	}

	Cannon->SetBoardAllowed(false);
	TestFalse(TEXT("Derived boarding policy can reject a player through ACannon::Board"), Cannon->Board(Player));
	TestNull(TEXT("Rejected boarding does not claim the player"), Cannon->GetRidingPlayer());
	TestEqual(TEXT("Rejected boarding does not invoke the boarded hook"), Cannon->GetBoardedCallCount(), 0);
	TestTrue(TEXT("Rejected boarding preserves player possession"), PlayerController->GetPawn() == Player);

	Cannon->SetBoardAllowed(true);
	ACannon* CannonBase = Cannon;
	TestTrue(TEXT("Board succeeds through an ACannon pointer"), CannonBase->Board(Player));
	TestTrue(TEXT("Cannon tracks the riding player"), Cannon->GetRidingPlayer() == Player);
	TestTrue(TEXT("PlayerController possesses the cannon"), PlayerController->GetPawn() == Cannon);
	TestEqual(TEXT("Successful boarding invokes the boarded hook once"), Cannon->GetBoardedCallCount(), 1);
	TestTrue(TEXT("Boarded hook receives the player"), Cannon->GetLastBoardedPawn() == Player);
	TestFalse(TEXT("Boarding disables player collision"), Player->GetActorEnableCollision());
	TestFalse(TEXT("Boarding disables player movement replication"), Player->IsReplicatingMovement());

	Cannon->ForceExit();
	TestNull(TEXT("ForceExit clears the riding player"), Cannon->GetRidingPlayer());
	TestTrue(TEXT("ForceExit restores player possession"), PlayerController->GetPawn() == Player);
	TestEqual(TEXT("ForceExit invokes the exited hook once"), Cannon->GetExitedCallCount(), 1);
	TestTrue(TEXT("Exited hook receives the player"), Cannon->GetLastExitedPawn() == Player);
	TestTrue(TEXT("ForceExit restores player collision"), Player->GetActorEnableCollision());
	TestTrue(TEXT("ForceExit restores player movement replication"), Player->IsReplicatingMovement());
	TestEqual(TEXT("ForceExit restores walking movement"),
		Player->GetCharacterMovement()->MovementMode,
		MOVE_Walking);

	TestTrue(TEXT("The same player can board again"), CannonBase->Board(Player));
	Cannon->ForceExit();
	TestEqual(TEXT("Repeated board invokes the boarded hook once per success"), Cannon->GetBoardedCallCount(), 2);
	TestEqual(TEXT("Repeated ForceExit invokes the exited hook once per exit"), Cannon->GetExitedCallCount(), 2);
	TestTrue(TEXT("Repeated exit restores player possession"), PlayerController->GetPawn() == Player);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCannonMilestone1FiringOperatorTest,
	"ArtisticSW.Cannon.Milestone1.FiringOperator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCannonMilestone1FiringOperatorTest::RunTest(const FString& Parameters)
{
	using namespace CannonMilestone1Tests;
	FScopedTestWorld TestWorld(TEXT("CannonMilestone1FiringWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), TestWorld.World))
	{
		return false;
	}

	ACannonMilestone1TestDouble* Cannon = TestWorld.World->SpawnActor<ACannonMilestone1TestDouble>();
	ACharacter* Operator = TestWorld.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Test cannon is spawned"), Cannon)
		|| !TestNotNull(TEXT("Firing operator is spawned"), Operator))
	{
		return false;
	}

	Cannon->SetProjectileClassForTest(ACannonball::StaticClass());
	Cannon->SetFiringOperator(Operator);
	TestTrue(TEXT("Cannon fires with an overridden operator"), Cannon->FireCannon());

	ACannonball* OperatorProjectile = nullptr;
	for (TActorIterator<ACannonball> It(TestWorld.World); It; ++It)
	{
		if (It->GetOwner() == Cannon)
		{
			OperatorProjectile = *It;
			break;
		}
	}
	TestNotNull(TEXT("Operator-controlled fire spawns a projectile"), OperatorProjectile);
	if (OperatorProjectile)
	{
		TestTrue(TEXT("Spawned projectile uses ResolveFiringOperator as Instigator"),
			OperatorProjectile->GetInstigator() == Operator);
	}

	ACannonMilestone1TestDouble* AICannon = TestWorld.World->SpawnActor<ACannonMilestone1TestDouble>();
	if (!TestNotNull(TEXT("AI-path cannon is spawned"), AICannon))
	{
		return false;
	}
	AICannon->SetProjectileClassForTest(ACannonball::StaticClass());
	TestTrue(TEXT("An unpossessed AI-path cannon still fires"), AICannon->FireCannon());

	ACannonball* AIProjectile = nullptr;
	for (TActorIterator<ACannonball> It(TestWorld.World); It; ++It)
	{
		if (It->GetOwner() == AICannon)
		{
			AIProjectile = *It;
			break;
		}
	}
	TestNotNull(TEXT("AI-path fire spawns a projectile"), AIProjectile);
	if (AIProjectile)
	{
		TestNull(TEXT("Default unpossessed cannon has no firing Instigator"), AIProjectile->GetInstigator());
	}
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Cannon.h"
#include "Cannon/EnemyCannon.h"
#include "Components/ChildActorComponent.h"
#include "EnemyShipCannonTestTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyShipUniqueCannonDiscoveryTest,
	"ArtisticSW.EnemyCannon.Integration.UniqueShipCannonDiscovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyShipUniqueCannonDiscoveryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyShipUniqueCannonWorld"));
	if (!TestNotNull(TEXT("Transient game world is created"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	auto CleanupWorld = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	AEnemyShipCannonTestDouble* Ship = World->SpawnActor<AEnemyShipCannonTestDouble>();
	AActor* Target = World->SpawnActor<AActor>(AActor::StaticClass(), FVector(2000.0, 0.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("EnemyShip test double is spawned"), Ship)
		|| !TestNotNull(TEXT("AI target is spawned"), Target))
	{
		CleanupWorld();
		return false;
	}

	auto AddCannonChild = [Ship](const FName ComponentName, TSubclassOf<ACannon> CannonClass)
	{
		UChildActorComponent* ChildComponent = NewObject<UChildActorComponent>(Ship, ComponentName);
		Ship->AddInstanceComponent(ChildComponent);
		ChildComponent->SetupAttachment(Ship->GetRootComponent());
		ChildComponent->SetChildActorClass(CannonClass);
		ChildComponent->RegisterComponent();
		return ChildComponent;
	};

	UChildActorComponent* BaseCannonChild = AddCannonChild(TEXT("BaseCannonChild"), ACannon::StaticClass());
	UChildActorComponent* EnemyCannonChild = AddCannonChild(TEXT("EnemyCannonChild"), AEnemyCannon::StaticClass());
	if (!TestNotNull(TEXT("Base Cannon child is created"), BaseCannonChild->GetChildActor())
		|| !TestNotNull(TEXT("EnemyCannon child is created"), EnemyCannonChild->GetChildActor()))
	{
		CleanupWorld();
		return false;
	}

	Ship->SetAITarget(Target);
	Ship->SetMaxActiveCannons(2);
	Ship->RefreshCannonsForTest();

	TestEqual(TEXT("Each ChildActor Cannon is discovered once"), Ship->GetAttachedCannonCount(), 2);
	TestEqual(TEXT("Attached Cannon list contains two unique instances"), Ship->GetUniqueAttachedCannonCount(), 2);
	TestEqual(TEXT("Both Cannons become active when MaxActiveCannons is two"), Ship->GetActiveCannonCount(), 2);
	TestEqual(TEXT("Active Cannon list contains two unique instances"), Ship->GetUniqueActiveCannonCount(), 2);

	CleanupWorld();
	return true;
}

#endif

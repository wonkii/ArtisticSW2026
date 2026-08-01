#include "EnemyCannonMilestone5TestTypes.h"

#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

AEnemyCannonTeamTestPawn::AEnemyCannonTeamTestPawn()
{
	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	Attributes = CreateDefaultSubobject<UBaseAttributeSet>(TEXT("Attributes"));
	AbilitySystem->SetIsReplicated(false);
}

void AEnemyCannonTeamTestPawn::InitializeAbilitySystemForTest()
{
	AbilitySystem->AddAttributeSetSubobject(Attributes.Get());
	AbilitySystem->InitAbilityActorInfo(this, this);
}

UEnemyCannonDamageTestEffect::UEnemyCannonDamageTestEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo DamageModifier;
	DamageModifier.Attribute = UBaseAttributeSet::GetDamageAttribute();
	DamageModifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat SetByCallerDamage;
	SetByCallerDamage.DataTag = Data_Damage;
	DamageModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCallerDamage);
	Modifiers.Add(DamageModifier);
}

ECollisionResponse AEnemyCannonballTestDouble::GetPawnCollisionResponseForTest() const
{
	return SphereCollision ? SphereCollision->GetCollisionResponseToChannel(ECC_Pawn) : ECR_Ignore;
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEnemyCannonMilestone5OperatorTeamDamageTest,
	"ArtisticSW.EnemyCannon.Milestone5.OperatorTeamAndPawnDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyCannonMilestone5OperatorTeamDamageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("EnemyCannonMilestone5World"));
	if (!TestNotNull(TEXT("Transient game world is created"), World) || !GEngine)
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	auto Cleanup = [World]()
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};

	AEnemyCannonTeamTestPawn* EnemyOperator = World->SpawnActor<AEnemyCannonTeamTestPawn>();
	AEnemyCannonTeamTestPawn* FriendlyEnemy = World->SpawnActor<AEnemyCannonTeamTestPawn>();
	AEnemyCannonTeamTestPawn* PlayerTarget = World->SpawnActor<AEnemyCannonTeamTestPawn>();
	AEnemyCannonballTestDouble* Projectile = World->SpawnActor<AEnemyCannonballTestDouble>();
	if (!TestNotNull(TEXT("Enemy operator is spawned"), EnemyOperator)
		|| !TestNotNull(TEXT("Friendly Enemy is spawned"), FriendlyEnemy)
		|| !TestNotNull(TEXT("Player target is spawned"), PlayerTarget)
		|| !TestNotNull(TEXT("Projectile is spawned"), Projectile))
	{
		Cleanup();
		return false;
	}

	EnemyOperator->InitializeAbilitySystemForTest();
	FriendlyEnemy->InitializeAbilitySystemForTest();
	PlayerTarget->InitializeAbilitySystemForTest();
	EnemyOperator->GetAbilitySystemComponent()->AddLooseGameplayTag(Team_Enemy);
	FriendlyEnemy->GetAbilitySystemComponent()->AddLooseGameplayTag(Team_Enemy);
	PlayerTarget->GetAbilitySystemComponent()->AddLooseGameplayTag(Team_Player);
	FCannonFireContext Context;
	Context.Operator = EnemyOperator;
	Context.SourceTeam = Team_Enemy;
	Context.Damage = 25.0f;
	Context.ProjectileSpeed = 3000.0f;
	Projectile->SetInstigator(EnemyOperator);
	Projectile->SetDamageEffect(UEnemyCannonDamageTestEffect::StaticClass());
	Projectile->InitializeProjectile(Context);

	TestEqual(TEXT("Enemy projectile blocks Pawn collision"),
		Projectile->GetPawnCollisionResponseForTest(), ECR_Block);
	TestTrue(TEXT("Enemy operator supplies the projectile team"),
		Projectile->GetFireContextForTest().SourceTeam.MatchesTagExact(Team_Enemy));
	TestTrue(TEXT("Same-team pawn is recognized as friendly"), Projectile->IsFriendlyTargetForTest(FriendlyEnemy));
	TestTrue(TEXT("Operator cannot damage itself"), Projectile->IsFriendlyTargetForTest(EnemyOperator));
	TestFalse(TEXT("Player pawn is hostile to Enemy projectile"), Projectile->IsFriendlyTargetForTest(PlayerTarget));

	const float PlayerHealthBefore = PlayerTarget->GetAttributes()->GetHealth();
	TestTrue(TEXT("Enemy projectile creates and applies a GAS damage spec"),
		Projectile->ApplyDamageForTest(PlayerTarget));
	TestEqual(TEXT("Player health is reduced by Data.Damage"),
		PlayerTarget->GetAttributes()->GetHealth(), PlayerHealthBefore - 25.0f);
	TestEqual(TEXT("Friendly health is unchanged because friendly targets are filtered before damage"),
		FriendlyEnemy->GetAttributes()->GetHealth(), 100.0f);

	Cleanup();
	return true;
}

#endif

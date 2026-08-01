#pragma once

#include "AbilitySystemInterface.h"
#include "Cannonball.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "EnemyCannonMilestone5TestTypes.generated.h"

class UAbilitySystemComponent;
class UBaseAttributeSet;

UCLASS(Transient, NotBlueprintable)
class AEnemyCannonTeamTestPawn : public APawn, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AEnemyCannonTeamTestPawn();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
	UBaseAttributeSet* GetAttributes() const { return Attributes; }
	void InitializeAbilitySystemForTest();

private:
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	UPROPERTY()
	TObjectPtr<UBaseAttributeSet> Attributes;
};

UCLASS(Transient, NotBlueprintable)
class UEnemyCannonDamageTestEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UEnemyCannonDamageTestEffect();
};

UCLASS(Transient, NotBlueprintable)
class AEnemyCannonballTestDouble : public ACannonball
{
	GENERATED_BODY()

public:
	void SetDamageEffect(TSubclassOf<UGameplayEffect> EffectClass) { DamageGEClass = EffectClass; }
	bool ApplyDamageForTest(AActor* Target, const FHitResult* Hit = nullptr)
	{
		return ApplyDamageToActor(Target, Hit);
	}
	const FCannonFireContext& GetFireContextForTest() const { return GetFireContext(); }
	bool IsFriendlyTargetForTest(const AActor* Target) const { return IsFriendlyTarget(Target); }
	ECollisionResponse GetPawnCollisionResponseForTest() const;
};

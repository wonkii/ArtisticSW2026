// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Cannonball.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class AShip;
class APawn;
class UGameplayEffect;

/** Immutable gameplay identity and launch values captured when a Cannon fires. */
USTRUCT(BlueprintType)
struct WATERANDSHIP_API FCannonFireContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cannonball")
	TObjectPtr<AShip> MountShip = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Cannonball")
	TObjectPtr<APawn> Operator = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Cannonball")
	FGameplayTag SourceTeam;

	UPROPERTY(BlueprintReadOnly, Category = "Cannonball")
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cannonball")
	float ProjectileSpeed = 0.0f;
};

UCLASS()
class WATERANDSHIP_API ACannonball : public AActor
{
	GENERATED_BODY()
	
public:	
	ACannonball();

protected:
	virtual void BeginPlay() override;
	virtual void PostNetReceiveLocationAndRotation() override;
	virtual void PostNetReceiveVelocity(const FVector& NewVelocity) override;

public:	
	virtual void Tick(float DeltaTime) override;

	/** Initialize Projectile values on spawn */
	void InitializeProjectile(AShip* InLaunchingShip, float InDamage, float InSpeed);
	void InitializeProjectile(const FCannonFireContext& InFireContext);

	/** Optional exact endpoint used by skills so terrain impacts do not continue below the Landscape. */
	void SetDesignatedImpactLocation(const FVector& InImpactLocation, float InArrivalTolerance = 75.0f);

protected:
	// ---- Components ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> SphereCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CannonballMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	// ---- Properties ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Damage")
	TSubclassOf<UGameplayEffect> DamageGEClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Damage")
	float DamageAmount = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Water")
	float LifeTimeAfterWaterHit = 2.0f;

	/** Initial amplitude for water ripple */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Water")
	float RippleAmplitude = 50.0f;

protected:
	// Water remains overlap-driven so the authoritative WaterBody delegate can
	// create and replicate the ripple. Ship damage is handled by swept blocking hits.
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	virtual void HandleShipHit(AShip* HitShip);
	virtual void HandleCharacterHit(APawn* HitPawn, const FHitResult& Hit);
	AShip* GetLaunchingShip() const { return LaunchingShip; }
	const FCannonFireContext& GetFireContext() const { return FireContext; }
	bool IsFriendlyTarget(const AActor* TargetActor) const;
	bool ApplyDamageToActor(AActor* TargetActor, const FHitResult* HitResult);
	void TriggerWaterRipple(const FVector& HitLocation);
	void DeactivateProjectile();

private:
	// ---- State ----
	UPROPERTY()
	TObjectPtr<AShip> LaunchingShip = nullptr;

	UPROPERTY()
	FCannonFireContext FireContext;

	bool bHasHitWater = false;
	bool bHasProcessedShipHit = false;
	bool bHasProcessedCharacterHit = false;
	bool bHasDesignatedImpact = false;
	FVector DesignatedImpactLocation = FVector::ZeroVector;
	FVector PreviousProjectileLocation = FVector::ZeroVector;
	float DesignatedImpactTolerance = 75.0f;
	FTimerHandle WaterHitTimerHandle;
};

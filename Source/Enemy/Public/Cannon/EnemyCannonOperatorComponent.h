#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnemyCannonOperatorComponent.generated.h"

class ABaseEnemy;
class AEnemyCannon;
class UBaseHealthComponent;
class USceneComponent;
class AShip;

/**
 * State restored after an Enemy stops operating a cannon.
 */
USTRUCT(BlueprintType)
struct ENEMY_API FEnemyCannonOperatorSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Cannon")
	TEnumAsByte<EMovementMode> MovementMode = MOVE_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Cannon")
	TEnumAsByte<ECollisionEnabled::Type> CapsuleCollisionEnabled = ECollisionEnabled::NoCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Cannon")
	FName CapsuleCollisionProfile = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Cannon")
	bool bReplicateMovement = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Cannon")
	TObjectPtr<USceneComponent> AttachParent = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Cannon")
	FName AttachSocket = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Cannon")
	bool bWeaponWasEquipped = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Cannon")
	bool bCaptured = false;

	void Reset()
	{
		*this = FEnemyCannonOperatorSnapshot();
	}
};

/**
 * Server-authoritative owner of an Enemy's cannon reservation lifecycle.
 * This component intentionally does not tick. Behavior Tree tasks introduced by
 * later milestones call this API instead of mutating AEnemyCannon directly.
 */
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
class ENEMY_API UEnemyCannonOperatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnemyCannonOperatorComponent();

	/** Attempts to acquire and remember a reservation. A valid existing reservation is kept. */
	UFUNCTION(BlueprintCallable, Category = "Enemy Cannon")
	bool TryReserveCannon(AEnemyCannon* Cannon);

	/** Validates the reservation, snapshots the Enemy, and attaches it to the MountPoint. */
	UFUNCTION(BlueprintCallable, Category = "Enemy Cannon")
	bool MountReservedCannon();

	/** Releases all locally tracked cannon state. Safe to call repeatedly. */
	UFUNCTION(BlueprintCallable, Category = "Enemy Cannon")
	bool DismountAndRelease();

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon")
	bool HasValidReservation() const;

	/**
	 * Records a reservation heartbeat for the future lease system.
	 * Milestone 9 will make AEnemyCannon enforce expiry.
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy Cannon")
	bool RefreshLease();

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon")
	AEnemyCannon* GetReservedCannon() const { return ReservedCannon; }

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon")
	AEnemyCannon* GetMountedCannon() const { return MountedCannon; }

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon")
	int64 GetReservationId() const { return static_cast<int64>(ReservationId); }

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon")
	float GetLastLeaseRefreshTime() const { return LastLeaseRefreshTime; }

	/** One-shot candidate search used when entering the Cannon BT branch. */
	UFUNCTION(BlueprintCallable, Category = "Enemy Cannon|AI")
	AEnemyCannon* FindAndReserveBestCannon(AActor* TargetActor, float MaxSearchDistance = 3000.0f);

	/** Always resolves the live MountPoint transform; it is never cached. */
	UFUNCTION(BlueprintPure, Category = "Enemy Cannon|AI")
	bool GetReservedMountGoal(FVector& OutWorldGoal) const;

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon|AI")
	bool IsWithinReservedMountRadius(float AcceptanceRadius = -1.0f) const;

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon|AI")
	AShip* GetHostShip() const { return FindHostShip(); }

	const FEnemyCannonOperatorSnapshot& GetOriginalStateSnapshot() const { return OriginalStateSnapshot; }

	/** Called by AEnemyCannon::EndPlay to invalidate references without re-entering the dying Cannon. */
	void NotifyCannonEndPlay(const AEnemyCannon* Cannon);

	/** Client-side correction called from AEnemyCannon::OnRep_MountedEnemy. */
	void ApplyReplicatedMountState(AEnemyCannon* Cannon, bool bMounted);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	ABaseEnemy* GetOwningEnemy() const;
	void ResetReservationTracking();
	void BindTrackedCannon(AEnemyCannon* Cannon);
	void UnbindTrackedCannon();
	bool CaptureOriginalState(ABaseEnemy* Enemy);
	bool ApplyMountedState(ABaseEnemy* Enemy, AEnemyCannon* Cannon, bool bAuthoritative);
	void RestoreOriginalState(
		ABaseEnemy* Enemy,
		const AEnemyCannon* Cannon,
		bool bRestoreCharacterState,
		bool bMoveToDismountPoint);
	FTransform ResolveDismountTransform(const AEnemyCannon* Cannon) const;
	bool IsOwnerAlive() const;
	AShip* FindHostShip() const;

	UFUNCTION()
	void HandleOwnerDeathStarted(UBaseHealthComponent* HealthComponent);

	UFUNCTION()
	void HandleTrackedCannonEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy Cannon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AEnemyCannon> ReservedCannon = nullptr;

	UPROPERTY(VisibleInstanceOnly, Category = "Enemy Cannon")
	uint32 ReservationId = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy Cannon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AEnemyCannon> MountedCannon = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy Cannon", meta = (AllowPrivateAccess = "true"))
	FEnemyCannonOperatorSnapshot OriginalStateSnapshot;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Enemy Cannon", meta = (AllowPrivateAccess = "true"))
	float LastLeaseRefreshTime = 0.0f;

	bool bIsCleaningUp = false;
};

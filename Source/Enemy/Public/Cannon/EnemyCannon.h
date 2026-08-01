#pragma once

#include "CoreMinimal.h"
#include "Cannon.h"
#include "EnemyCannon.generated.h"

class ABaseEnemy;
class USceneComponent;

UENUM(BlueprintType)
enum class EEnemyCannonOperationState : uint8
{
	Available,
	ReservedForEnemy,
	EnemyMounted,
	PlayerControlled,
	Disabled
};

/**
 * Cannon variant that owns the server-authoritative Player/Enemy occupancy policy.
 * Enemy attachment and AI firing are introduced by later milestones.
 */
UCLASS()
class ENEMY_API AEnemyCannon : public ACannon
{
	GENERATED_BODY()

public:
	AEnemyCannon();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon")
	EEnemyCannonOperationState GetOperationState() const;

	bool TryReserveForEnemy(ABaseEnemy* Enemy, uint32& OutReservationId);
	bool ReleaseEnemyReservation(ABaseEnemy* Enemy, uint32 ReservationId);
	bool IsReservationValid(const ABaseEnemy* Enemy, uint32 ReservationId) const;
	bool IsAvailableForEnemy(const ABaseEnemy* Enemy) const;
	bool TryMountReservedEnemy(ABaseEnemy* Enemy, uint32 ReservationId);
	bool ReleaseMountedEnemy(ABaseEnemy* Enemy, uint32 ReservationId);

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon")
	FTransform GetEnemyMountWorldTransform() const;

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon")
	FTransform GetEnemyDismountWorldTransform() const;

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon")
	ABaseEnemy* GetReservedEnemy() const { return ReservedEnemy; }

	UFUNCTION(BlueprintPure, Category = "Enemy Cannon")
	ABaseEnemy* GetMountedEnemy() const { return MountedEnemy; }

	uint32 GetReservationRevision() const { return ReservationRevision; }
	USceneComponent* GetEnemyMountPoint() const { return EnemyMountPoint; }
	USceneComponent* GetEnemyDismountPoint() const { return EnemyDismountPoint; }
	float GetEnemyMountAcceptanceRadius() const { return EnemyMountAcceptanceRadius; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool CanBoard(APawn* PlayerPawn) const override;
	virtual void OnPlayerBoarded(APawn* PlayerPawn) override;
	virtual APawn* ResolveFiringOperator() const override;

	bool IsEnemyEligibleForReservation(const ABaseEnemy* Enemy) const;
	bool IsEnemyOnOwningShip(const ABaseEnemy* Enemy) const;
	void AdvanceReservationRevision();

	UFUNCTION()
	void OnRep_ReservedEnemy();

	UFUNCTION()
	void OnRep_MountedEnemy(ABaseEnemy* PreviousMountedEnemy);

	UFUNCTION()
	void OnRep_ReservationRevision();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Cannon|Mount")
	TObjectPtr<USceneComponent> EnemyMountPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy Cannon|Mount")
	TObjectPtr<USceneComponent> EnemyDismountPoint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy Cannon|Mount", meta = (ClampMin = "1.0", Units = "cm"))
	float EnemyMountAcceptanceRadius = 65.0f;

private:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_ReservedEnemy, Category = "Enemy Cannon|State", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ABaseEnemy> ReservedEnemy = nullptr;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_MountedEnemy, Category = "Enemy Cannon|State", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ABaseEnemy> MountedEnemy = nullptr;

	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_ReservationRevision, Category = "Enemy Cannon|State")
	uint32 ReservationRevision = 0;
};

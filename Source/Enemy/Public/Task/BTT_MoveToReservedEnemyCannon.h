#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTT_MoveToReservedEnemyCannon.generated.h"

struct FEnemyCannonMoveTaskMemory
{
	float ElapsedTime = 0.0f;
	float TimeSinceGoalUpdate = 0.0f;
};

UCLASS()
class ENEMY_API UBTT_MoveToReservedEnemyCannon : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_MoveToReservedEnemyCannon();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FEnemyCannonMoveTaskMemory); }
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;

	bool UpdateMoveGoal(UBehaviorTreeComponent& OwnerComp) const;
	class UEnemyCannonOperatorComponent* ResolveOperator(UBehaviorTreeComponent& OwnerComp) const;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector MountGoalKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetActorKey;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.05", ClampMax = "0.15", Units = "s"))
	float GoalUpdateInterval = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "1.0", Units = "cm"))
	float AcceptanceRadius = 50.0f;

	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.1", Units = "s"))
	float MoveTimeout = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Movement")
	bool bUsePathfinding = false;
};

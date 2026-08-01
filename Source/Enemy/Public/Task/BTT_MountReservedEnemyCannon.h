#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTT_MountReservedEnemyCannon.generated.h"

UCLASS()
class ENEMY_API UBTT_MountReservedEnemyCannon : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_MountReservedEnemyCannon();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasReservationKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector IsMountedKey;
};

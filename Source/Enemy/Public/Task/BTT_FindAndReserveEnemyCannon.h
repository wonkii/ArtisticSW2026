#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Tasks/BTTask_BlackboardBase.h"
#include "BTT_FindAndReserveEnemyCannon.generated.h"

UCLASS()
class ENEMY_API UBTT_FindAndReserveEnemyCannon : public UBTTask_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTT_FindAndReserveEnemyCannon();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector ReservedCannonKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasReservationKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector IsMountedKey;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasAimSolutionKey;

	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxSearchDistance = 3000.0f;
};

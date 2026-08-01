#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Decorators/BTDecorator_BlackboardBase.h"
#include "BTD_HasValidEnemyCannonReservation.generated.h"

UCLASS()
class ENEMY_API UBTD_HasValidEnemyCannonReservation : public UBTDecorator_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTD_HasValidEnemyCannonReservation();

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
};

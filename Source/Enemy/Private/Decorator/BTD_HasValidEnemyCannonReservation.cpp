#include "Decorator/BTD_HasValidEnemyCannonReservation.h"

#include "AIController.h"
#include "BaseEnemy.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Cannon/EnemyCannon.h"
#include "Cannon/EnemyCannonOperatorComponent.h"

UBTD_HasValidEnemyCannonReservation::UBTD_HasValidEnemyCannonReservation()
{
	NodeName = TEXT("Has Valid Enemy Cannon Reservation");
	BlackboardKey.SelectedKeyName = TEXT("ReservedEnemyCannon");
	BlackboardKey.AddObjectFilter(
		this,
		GET_MEMBER_NAME_CHECKED(UBTD_HasValidEnemyCannonReservation, BlackboardKey),
		AEnemyCannon::StaticClass());
}

bool UBTD_HasValidEnemyCannonReservation::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	const AAIController* AIController = OwnerComp.GetAIOwner();
	const ABaseEnemy* Enemy = AIController ? Cast<ABaseEnemy>(AIController->GetPawn()) : nullptr;
	const UEnemyCannonOperatorComponent* Operator = Enemy ? Enemy->GetCannonOperatorComponent() : nullptr;
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	const AEnemyCannon* BlackboardCannon = Blackboard
		? Cast<AEnemyCannon>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
	return Operator
		&& Operator->HasValidReservation()
		&& BlackboardCannon == Operator->GetReservedCannon();
}

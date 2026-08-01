#include "Task/BTT_MountReservedEnemyCannon.h"

#include "AIController.h"
#include "BaseEnemy.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Cannon/EnemyCannon.h"
#include "Cannon/EnemyCannonOperatorComponent.h"

UBTT_MountReservedEnemyCannon::UBTT_MountReservedEnemyCannon()
{
	NodeName = TEXT("Mount Reserved Enemy Cannon");
	BlackboardKey.SelectedKeyName = TEXT("ReservedEnemyCannon");
	BlackboardKey.AddObjectFilter(
		this, GET_MEMBER_NAME_CHECKED(UBTT_MountReservedEnemyCannon, BlackboardKey), AEnemyCannon::StaticClass());
	HasReservationKey.SelectedKeyName = TEXT("HasCannonReservation");
	HasReservationKey.AddBoolFilter(
		this, GET_MEMBER_NAME_CHECKED(UBTT_MountReservedEnemyCannon, HasReservationKey));
	IsMountedKey.SelectedKeyName = TEXT("IsMountedOnCannon");
	IsMountedKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_MountReservedEnemyCannon, IsMountedKey));
}

EBTNodeResult::Type UBTT_MountReservedEnemyCannon::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABaseEnemy* Enemy = AIController ? Cast<ABaseEnemy>(AIController->GetPawn()) : nullptr;
	UEnemyCannonOperatorComponent* Operator = Enemy ? Enemy->GetCannonOperatorComponent() : nullptr;
	AEnemyCannon* BlackboardCannon = Blackboard
		? Cast<AEnemyCannon>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
	if (Blackboard && Operator && BlackboardCannon == Operator->GetReservedCannon()
		&& Operator->MountReservedCannon())
	{
		Blackboard->SetValueAsBool(HasReservationKey.SelectedKeyName, false);
		Blackboard->SetValueAsBool(IsMountedKey.SelectedKeyName, true);
		return EBTNodeResult::Succeeded;
	}

	if (Operator)
	{
		Operator->DismountAndRelease();
	}
	if (Blackboard)
	{
		Blackboard->ClearValue(GetSelectedBlackboardKey());
		Blackboard->SetValueAsBool(HasReservationKey.SelectedKeyName, false);
		Blackboard->SetValueAsBool(IsMountedKey.SelectedKeyName, false);
	}
	return EBTNodeResult::Failed;
}

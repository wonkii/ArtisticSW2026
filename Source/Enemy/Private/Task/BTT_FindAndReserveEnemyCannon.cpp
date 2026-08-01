#include "Task/BTT_FindAndReserveEnemyCannon.h"

#include "AIController.h"
#include "BaseEnemy.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Cannon/EnemyCannon.h"
#include "Cannon/EnemyCannonOperatorComponent.h"

UBTT_FindAndReserveEnemyCannon::UBTT_FindAndReserveEnemyCannon()
{
	NodeName = TEXT("Find And Reserve Enemy Cannon");
	BlackboardKey.SelectedKeyName = TEXT("TargetActor");
	BlackboardKey.AddObjectFilter(
		this, GET_MEMBER_NAME_CHECKED(UBTT_FindAndReserveEnemyCannon, BlackboardKey), AActor::StaticClass());

	ReservedCannonKey.SelectedKeyName = TEXT("ReservedEnemyCannon");
	ReservedCannonKey.AddObjectFilter(
		this, GET_MEMBER_NAME_CHECKED(UBTT_FindAndReserveEnemyCannon, ReservedCannonKey), AEnemyCannon::StaticClass());
	HasReservationKey.SelectedKeyName = TEXT("HasCannonReservation");
	HasReservationKey.AddBoolFilter(
		this, GET_MEMBER_NAME_CHECKED(UBTT_FindAndReserveEnemyCannon, HasReservationKey));
	IsMountedKey.SelectedKeyName = TEXT("IsMountedOnCannon");
	IsMountedKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_FindAndReserveEnemyCannon, IsMountedKey));
	HasAimSolutionKey.SelectedKeyName = TEXT("HasCannonAimSolution");
	HasAimSolutionKey.AddBoolFilter(
		this, GET_MEMBER_NAME_CHECKED(UBTT_FindAndReserveEnemyCannon, HasAimSolutionKey));
}

EBTNodeResult::Type UBTT_FindAndReserveEnemyCannon::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABaseEnemy* Enemy = AIController ? Cast<ABaseEnemy>(AIController->GetPawn()) : nullptr;
	UEnemyCannonOperatorComponent* Operator = Enemy ? Enemy->GetCannonOperatorComponent() : nullptr;
	AActor* Target = Blackboard
		? Cast<AActor>(Blackboard->GetValueAsObject(GetSelectedBlackboardKey()))
		: nullptr;
	if (!Blackboard || !Operator || !IsValid(Target))
	{
		return EBTNodeResult::Failed;
	}

	AEnemyCannon* ReservedCannon = Operator->FindAndReserveBestCannon(Target, MaxSearchDistance);
	Blackboard->SetValueAsObject(ReservedCannonKey.SelectedKeyName, ReservedCannon);
	Blackboard->SetValueAsBool(HasReservationKey.SelectedKeyName, ReservedCannon != nullptr);
	Blackboard->SetValueAsBool(IsMountedKey.SelectedKeyName, false);
	Blackboard->SetValueAsBool(HasAimSolutionKey.SelectedKeyName, ReservedCannon != nullptr);
	return ReservedCannon ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}

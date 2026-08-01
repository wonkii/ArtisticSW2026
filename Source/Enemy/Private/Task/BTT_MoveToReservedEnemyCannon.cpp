#include "Task/BTT_MoveToReservedEnemyCannon.h"

#include "AIController.h"
#include "BaseEnemy.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "Cannon/EnemyCannon.h"
#include "Cannon/EnemyCannonOperatorComponent.h"
#include "Components/BaseHealthComponent.h"
#include "Navigation/PathFollowingComponent.h"

UBTT_MoveToReservedEnemyCannon::UBTT_MoveToReservedEnemyCannon()
{
	NodeName = TEXT("Move To Reserved Enemy Cannon");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
	BlackboardKey.SelectedKeyName = TEXT("ReservedEnemyCannon");
	BlackboardKey.AddObjectFilter(
		this, GET_MEMBER_NAME_CHECKED(UBTT_MoveToReservedEnemyCannon, BlackboardKey), AEnemyCannon::StaticClass());
	MountGoalKey.SelectedKeyName = TEXT("CannonMountGoal");
	MountGoalKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTT_MoveToReservedEnemyCannon, MountGoalKey));
	TargetActorKey.SelectedKeyName = TEXT("TargetActor");
	TargetActorKey.AddObjectFilter(
		this, GET_MEMBER_NAME_CHECKED(UBTT_MoveToReservedEnemyCannon, TargetActorKey), AActor::StaticClass());
}

EBTNodeResult::Type UBTT_MoveToReservedEnemyCannon::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	FEnemyCannonMoveTaskMemory* Memory = CastInstanceNodeMemory<FEnemyCannonMoveTaskMemory>(NodeMemory);
	UEnemyCannonOperatorComponent* Operator = ResolveOperator(OwnerComp);
	if (!Memory || !Operator || !Operator->HasValidReservation())
	{
		return EBTNodeResult::Failed;
	}
	Memory->ElapsedTime = 0.0f;
	Memory->TimeSinceGoalUpdate = GoalUpdateInterval;
	if (Operator->IsWithinReservedMountRadius(AcceptanceRadius))
	{
		UpdateMoveGoal(OwnerComp);
		return EBTNodeResult::Succeeded;
	}
	return UpdateMoveGoal(OwnerComp) ? EBTNodeResult::InProgress : EBTNodeResult::Failed;
}

void UBTT_MoveToReservedEnemyCannon::TickTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	float DeltaSeconds)
{
	FEnemyCannonMoveTaskMemory* Memory = CastInstanceNodeMemory<FEnemyCannonMoveTaskMemory>(NodeMemory);
	UEnemyCannonOperatorComponent* Operator = ResolveOperator(OwnerComp);
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AActor* Target = Blackboard
		? Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName))
		: nullptr;
	const UBaseHealthComponent* TargetHealth = Target ? Target->FindComponentByClass<UBaseHealthComponent>() : nullptr;
	if (!Memory || !Operator || !Operator->HasValidReservation()
		|| !IsValid(Target) || (TargetHealth && TargetHealth->IsDead()))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	Memory->ElapsedTime += DeltaSeconds;
	Memory->TimeSinceGoalUpdate += DeltaSeconds;
	if (Operator->IsWithinReservedMountRadius(AcceptanceRadius))
	{
		UpdateMoveGoal(OwnerComp);
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}
	if (Memory->ElapsedTime >= MoveTimeout)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
	if (Memory->TimeSinceGoalUpdate >= GoalUpdateInterval)
	{
		Memory->TimeSinceGoalUpdate = 0.0f;
		if (!UpdateMoveGoal(OwnerComp))
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}
	}
}

EBTNodeResult::Type UBTT_MoveToReservedEnemyCannon::AbortTask(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory)
{
	if (UEnemyCannonOperatorComponent* Operator = ResolveOperator(OwnerComp))
	{
		Operator->DismountAndRelease();
	}
	if (AAIController* AIController = OwnerComp.GetAIOwner())
	{
		AIController->StopMovement();
	}
	return EBTNodeResult::Aborted;
}

void UBTT_MoveToReservedEnemyCannon::OnTaskFinished(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTNodeResult::Type TaskResult)
{
	if (AAIController* AIController = OwnerComp.GetAIOwner())
	{
		AIController->StopMovement();
	}
	if (TaskResult != EBTNodeResult::Succeeded)
	{
		if (UEnemyCannonOperatorComponent* Operator = ResolveOperator(OwnerComp))
		{
			Operator->DismountAndRelease();
		}
	}
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

void UBTT_MoveToReservedEnemyCannon::InitializeMemory(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FEnemyCannonMoveTaskMemory>(NodeMemory, InitType);
}

void UBTT_MoveToReservedEnemyCannon::CleanupMemory(
	UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory,
	EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FEnemyCannonMoveTaskMemory>(NodeMemory, CleanupType);
}

bool UBTT_MoveToReservedEnemyCannon::UpdateMoveGoal(UBehaviorTreeComponent& OwnerComp) const
{
	UEnemyCannonOperatorComponent* Operator = ResolveOperator(OwnerComp);
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	FVector CurrentGoal;
	if (!Operator || !Blackboard || !AIController || !Operator->GetReservedMountGoal(CurrentGoal))
	{
		return false;
	}

	Blackboard->SetValueAsVector(MountGoalKey.SelectedKeyName, CurrentGoal);
	if (Operator->IsWithinReservedMountRadius(AcceptanceRadius))
	{
		return true;
	}

	const EPathFollowingRequestResult::Type Result = AIController->MoveToLocation(
		CurrentGoal,
		AcceptanceRadius,
		false,
		bUsePathfinding,
		false,
		true,
		nullptr,
		true);
	return Result != EPathFollowingRequestResult::Failed;
}

UEnemyCannonOperatorComponent* UBTT_MoveToReservedEnemyCannon::ResolveOperator(
	UBehaviorTreeComponent& OwnerComp) const
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	ABaseEnemy* Enemy = AIController ? Cast<ABaseEnemy>(AIController->GetPawn()) : nullptr;
	return Enemy ? Enemy->GetCannonOperatorComponent() : nullptr;
}

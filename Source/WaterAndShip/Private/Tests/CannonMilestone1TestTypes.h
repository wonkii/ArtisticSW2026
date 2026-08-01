#pragma once

#include "Cannon.h"
#include "CannonMilestone1TestTypes.generated.h"

/**
 * Native test double that proves ACannon's milestone-1 extension points are
 * reachable through the public base-class API.
 */
UCLASS(Transient, NotBlueprintable)
class ACannonMilestone1TestDouble : public ACannon
{
	GENERATED_BODY()

public:
	void SetBoardAllowed(bool bInBoardAllowed) { bBoardAllowed = bInBoardAllowed; }
	void SetFiringOperator(APawn* InFiringOperator) { FiringOperator = InFiringOperator; }
	void SetProjectileClassForTest(TSubclassOf<AActor> InProjectileClass)
	{
		CannonballClass = InProjectileClass;
		FireCooldown = 0.05f;
	}

	int32 GetBoardedCallCount() const { return BoardedCallCount; }
	int32 GetExitedCallCount() const { return ExitedCallCount; }
	APawn* GetLastBoardedPawn() const { return LastBoardedPawn; }
	APawn* GetLastExitedPawn() const { return LastExitedPawn; }

protected:
	virtual bool CanBoard(APawn* PlayerPawn) const override
	{
		return bBoardAllowed && Super::CanBoard(PlayerPawn);
	}

	virtual void OnPlayerBoarded(APawn* PlayerPawn) override
	{
		++BoardedCallCount;
		LastBoardedPawn = PlayerPawn;
	}

	virtual void OnPlayerExited(APawn* PlayerPawn) override
	{
		++ExitedCallCount;
		LastExitedPawn = PlayerPawn;
	}

	virtual APawn* ResolveFiringOperator() const override
	{
		return FiringOperator ? FiringOperator.Get() : Super::ResolveFiringOperator();
	}

private:
	bool bBoardAllowed = true;
	int32 BoardedCallCount = 0;
	int32 ExitedCallCount = 0;

	UPROPERTY()
	TObjectPtr<APawn> FiringOperator = nullptr;

	UPROPERTY()
	TObjectPtr<APawn> LastBoardedPawn = nullptr;

	UPROPERTY()
	TObjectPtr<APawn> LastExitedPawn = nullptr;
};

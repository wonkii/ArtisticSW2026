#pragma once

#include "ShipAI/EnemyShip.h"
#include "EnemyShipCannonTestTypes.generated.h"

UCLASS(Transient, NotBlueprintable)
class AEnemyShipCannonTestDouble : public AEnemyShip
{
	GENERATED_BODY()

public:
	void RefreshCannonsForTest()
	{
		FindAttachedCannons();
		UpdateActiveCannons();
	}

	int32 GetAttachedCannonCount() const { return AttachedCannons.Num(); }
	int32 GetActiveCannonCount() const { return ActiveAICannons.Num(); }

	int32 GetUniqueAttachedCannonCount() const
	{
		TSet<const ACannon*> UniqueCannons;
		for (const ACannon* Cannon : AttachedCannons)
		{
			UniqueCannons.Add(Cannon);
		}
		return UniqueCannons.Num();
	}

	int32 GetUniqueActiveCannonCount() const
	{
		TSet<const ACannon*> UniqueCannons;
		for (const ACannon* Cannon : ActiveAICannons)
		{
			UniqueCannons.Add(Cannon);
		}
		return UniqueCannons.Num();
	}
};

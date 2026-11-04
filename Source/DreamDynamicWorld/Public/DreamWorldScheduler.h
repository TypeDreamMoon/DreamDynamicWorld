// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Spreads a world's occasional heavy work over frames. A task runs no more often than its MinInterval;
 * among the tasks that are due, the overdue ones (past MaxInterval) run first whatever the budget, then the
 * rest by priority until the frame's budget is spent. A task returns whether it did any work, so a task that
 * finds nothing to do costs nothing against the budget beyond its own check.
 */
class DREAMDYNAMICWORLD_API FDreamWorldScheduler
{
public:
	using FTaskFunction = TFunction<bool()>;

	/** Returns a handle for Unregister. Higher Priority runs first. MaxInterval <= 0 = never forced. */
	int32 Register(FName Name, int32 Priority, double MinInterval, double MaxInterval, FTaskFunction Function);
	void Unregister(int32 Handle);

	/** Makes a task due now (it still waits its turn). */
	void Request(int32 Handle);

	/** Runs the due tasks. Now is seconds on any monotonic clock. */
	void Tick(double Now, double BudgetMs);

	int32 Num() const { return Tasks.Num(); }

	/** Tasks run on the last Tick. */
	int32 GetLastRunCount() const { return LastRunCount; }

private:
	struct FTask
	{
		int32 Handle = 0;
		FName Name;
		int32 Priority = 0;
		double MinInterval = 0.0;
		double MaxInterval = 0.0;
		double LastRun = -1.0e30;
		bool bRequested = false;
		FTaskFunction Function;
	};

	TArray<FTask> Tasks;
	int32 NextHandle = 1;
	int32 LastRunCount = 0;
};

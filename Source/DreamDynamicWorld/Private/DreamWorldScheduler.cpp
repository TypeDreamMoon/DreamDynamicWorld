// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldScheduler.h"
#include "HAL/PlatformTime.h"

int32 FDreamWorldScheduler::Register(FName Name, int32 Priority, double MinInterval, double MaxInterval, FTaskFunction Function)
{
	FTask& Task = Tasks.AddDefaulted_GetRef();
	Task.Handle = NextHandle++;
	Task.Name = Name;
	Task.Priority = Priority;
	Task.MinInterval = FMath::Max(0.0, MinInterval);
	Task.MaxInterval = MaxInterval;
	Task.Function = MoveTemp(Function);
	return Task.Handle;
}

void FDreamWorldScheduler::Unregister(int32 Handle)
{
	Tasks.RemoveAll([Handle](const FTask& T) { return T.Handle == Handle; });
}

void FDreamWorldScheduler::Request(int32 Handle)
{
	for (FTask& Task : Tasks)
	{
		if (Task.Handle == Handle)
		{
			Task.bRequested = true;
		}
	}
}

void FDreamWorldScheduler::Tick(double Now, double BudgetMs)
{
	LastRunCount = 0;

	struct FDue
	{
		int32 Handle;
		bool bOverdue;
		int32 Priority;
	};
	TArray<FDue, TInlineAllocator<16>> Due;
	for (const FTask& T : Tasks)
	{
		const double Since = Now - T.LastRun;
		if (Since < T.MinInterval && !T.bRequested)
		{
			continue;
		}
		Due.Add({ T.Handle, T.MaxInterval > 0.0 && Since >= T.MaxInterval, T.Priority });
	}
	Due.StableSort([](const FDue& A, const FDue& B)
	{
		if (A.bOverdue != B.bOverdue)
		{
			return A.bOverdue;
		}
		return A.Priority > B.Priority;
	});

	const double Start = FPlatformTime::Seconds();
	for (const FDue& D : Due)
	{
		const double SpentMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		if (!D.bOverdue && SpentMs >= BudgetMs)
		{
			break;
		}
		// A task may register or unregister tasks, so look it up by handle and call a copy of its function.
		const FTask* Found = Tasks.FindByPredicate([&D](const FTask& T) { return T.Handle == D.Handle; });
		if (!Found)
		{
			continue;
		}
		FTaskFunction Function = Found->Function;
		const bool bDidWork = Function ? Function() : false;
		for (FTask& T : Tasks)
		{
			if (T.Handle == D.Handle)
			{
				T.bRequested = false;
				if (bDidWork)
				{
					T.LastRun = Now;
				}
			}
		}
		LastRunCount += bDidWork ? 1 : 0;
	}
}

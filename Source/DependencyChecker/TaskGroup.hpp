#pragma once


#include "CoreMinimal.h"

#include <atomic>

#include "Async/Async.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_TaskGroup
{
	class FGraphTask
	{
	public:
		template <typename FUNC>
		FGraphTask(FUNC&& Func, std::atomic<int32>& _TaskCounter)
			: Function(Forward<FUNC>(Func))
			, TaskCounter(_TaskCounter)
		{
			TaskCounter.fetch_add(1);
		}

		
	public:
		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionGraphEvent)
		{
			Function();
			TaskCounter.fetch_sub(1);
		}

	public:
		FORCEINLINE static const TCHAR* GetTaskName() { return TEXT("TaskGroup"); }
		FORCEINLINE static TStatId GetStatId() { return GET_STATID(STAT_TaskGraph_OtherTasks); }
		FORCEINLINE static ENamedThreads::Type GetDesiredThread() { return ENamedThreads::AnyHiPriThreadHiPriTask; }
		FORCEINLINE static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::FireAndForget; }

		
	private:
		TFunction<void()> Function;
		std::atomic<int32>& TaskCounter;
	};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FTaskGroup
{
public:
	FTaskGroup()
		: TaskCounter(0)
	{}

	~FTaskGroup() { Wait(); }

	
public:
	template <typename FUNC>
	FORCEINLINE void Run(FUNC&& Func) { TGraphTask<__hidden_TaskGroup::FGraphTask>::CreateTask().ConstructAndDispatchWhenReady(Forward<FUNC>(Func), TaskCounter); }
	FORCEINLINE void Wait() { while (TaskCounter.load(std::memory_order_acquire) > 0); }

	
private:
	std::atomic<int32> TaskCounter;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

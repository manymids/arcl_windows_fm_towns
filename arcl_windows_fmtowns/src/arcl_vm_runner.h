/*
  ARCL deterministic VM runner.

  This runner deliberately does not use TownsThread::VMMainLoopTemplate:
  that loop is correct for an interactive emulator, but intentionally applies
  host-time pacing.  ARCL's synchronous arcl_run contract instead advances a
  fixed amount of *virtual* Towns time and never sleeps.
*/
#ifndef ARCL_VM_RUNNER_IS_INCLUDED
#define ARCL_VM_RUNNER_IS_INCLUDED

#include <cstdint>

#include "towns.h"
#include "townsdef.h"

class ArclVmRunner
{
public:
	static constexpr uint64_t FRAME_NS=PER_SECOND/60;

	enum StopReason
	{
		STOP_NONE,
		STOP_ABORT,
		STOP_DEBUG_BREAK,
	};

	struct Result
	{
		uint64_t frame=0;
		uint64_t framesUsed=0;
		uint64_t townsTime=0;
		StopReason stopReason=STOP_NONE;
	};

	static uint64_t FrameFromTownsTime(uint64_t townsTime);
	static uint64_t NextFrameBoundary(uint64_t townsTime);
	static uint64_t IdleWakeTime(
	    uint64_t townsTime,
	    uint64_t nextFastDevicePollingTime,
	    uint64_t frameEnd);

	/*! Advance one ARCL frame.  The caller is the sole owner of towns.

	    The inner ordering matches TownsThread's RUNMODE_RUN loop.  Host-clock
	    pacing, host input polling, sound-device polling, rendering, and UI work
	    intentionally do not appear here; the ARCL session invokes those services
	    at its explicit virtual-time boundaries.
	*/
	template <class TownsClass>
	Result RunToTime(TownsClass &towns,uint64_t deadline) const
	{
		Result result;

		while(towns.state.townsTime<deadline)
		{
			while(towns.state.townsTime<deadline &&
			      towns.state.townsTime<=towns.state.nextFastDevicePollingTime &&
			      0==towns.GetStopFlags())
			{
				const auto townsTimeBeforeInstruction=towns.state.townsTime;
				towns.RunOneInstruction();
				towns.pic.ProcessIRQ(towns.CPU(),towns.mem);

				// ARCL intentionally has no host-time pacing.  A zero-clock
				// instruction emitted by the legacy core therefore
				// cannot be allowed to keep virtual time still.  Advance only to
				// the next regular device-polling boundary, where scheduled
				// callbacks and IRQ generation can wake the guest.
				if(towns.state.townsTime<=townsTimeBeforeInstruction)
				{
					towns.state.townsTime=IdleWakeTime(
					    towns.state.townsTime,
					    towns.state.nextFastDevicePollingTime,
					    deadline);
				}
			}

			towns.RunScheduledTasks();
			towns.RunFastDevicePolling();

			if(0!=towns.GetStopFlags())
			{
				result.stopReason=(true==towns.CheckAbort() ? STOP_ABORT : STOP_DEBUG_BREAK);
				break;
			}
		}

		result.frame=FrameFromTownsTime(towns.state.townsTime);
		result.townsTime=towns.state.townsTime;
		return result;
	}

	template <class TownsClass>
	Result RunOneFrame(TownsClass &towns) const
	{
		auto result=RunToTime(towns,NextFrameBoundary(towns.state.townsTime));
		result.framesUsed=(STOP_NONE==result.stopReason ? 1 : 0);
		return result;
	}

	template <class TownsClass>
	Result RunFrames(TownsClass &towns,uint64_t frames) const
	{
		Result result;
		result.frame=FrameFromTownsTime(towns.state.townsTime);
		result.townsTime=towns.state.townsTime;

		for(uint64_t i=0; i<frames; ++i)
		{
			auto oneFrame=RunOneFrame(towns);
			result.framesUsed+=oneFrame.framesUsed;
			result.frame=oneFrame.frame;
			result.townsTime=oneFrame.townsTime;
			result.stopReason=oneFrame.stopReason;
			if(STOP_NONE!=oneFrame.stopReason)
			{
				break;
			}
		}
		return result;
	}
};

#endif

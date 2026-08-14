#include <cassert>
#include <cstdint>

#include "arcl_vm_runner.h"

namespace
{
struct FakeCpu
{
};

struct FakeMemory
{
};

struct FakePic
{
	void ProcessIRQ(FakeCpu &,FakeMemory &)
	{
	}
};

struct FakeTowns
{
	struct
	{
		uint64_t townsTime=0;
		uint64_t nextFastDevicePollingTime=0;
	} state;
	FakeCpu cpu;
	FakeMemory mem;
	FakePic pic;
	unsigned int scheduledTaskCalls=0,fastPollingCalls=0;

	unsigned int GetStopFlags(void) const
	{
		return 0;
	}
	void RunOneInstruction(void)
	{
		// Deliberately consume no virtual clocks, like an idle legacy-core step.
	}
	FakeCpu &CPU(void)
	{
		return cpu;
	}
	void RunScheduledTasks(void)
	{
		++scheduledTaskCalls;
	}
	void RunFastDevicePolling(void)
	{
		++fastPollingCalls;
	}
	bool CheckAbort(void) const
	{
		return false;
	}
};
}

int main()
{
	constexpr auto frame=ArclVmRunner::FRAME_NS;
	assert(16666666==frame);
	assert(0==ArclVmRunner::FrameFromTownsTime(0));
	assert(0==ArclVmRunner::FrameFromTownsTime(frame-1));
	assert(1==ArclVmRunner::FrameFromTownsTime(frame));
	assert(frame==ArclVmRunner::NextFrameBoundary(0));
	assert(frame==ArclVmRunner::NextFrameBoundary(frame-1));
	assert(2*frame==ArclVmRunner::NextFrameBoundary(frame));
	assert(2*frame==ArclVmRunner::NextFrameBoundary(frame+1));

	assert(10001==ArclVmRunner::IdleWakeTime(0,10000,frame));
	assert(1==ArclVmRunner::IdleWakeTime(0,0,frame));
	assert(frame==ArclVmRunner::IdleWakeTime(frame-1,frame+10000,frame));
	assert(frame==ArclVmRunner::IdleWakeTime(frame,frame+10000,frame));

	// If a zero-clock instruction advances exactly to the deadline while its
	// fast-device polling time is still later, RunToTime must leave the inner
	// instruction loop.  Without the deadline guard, this case re-runs the
	// zero-clock instruction forever at the same virtual time.
	FakeTowns idleAtDeadline;
	idleAtDeadline.state.townsTime=frame-1;
	idleAtDeadline.state.nextFastDevicePollingTime=frame+10000;
	const auto idleResult=ArclVmRunner().RunToTime(idleAtDeadline,frame);
	assert(frame==idleResult.townsTime);
	assert(1==idleAtDeadline.scheduledTaskCalls);
	assert(1==idleAtDeadline.fastPollingCalls);

	// TOWNSEMU represents 60 Hz in integer nanoseconds.  The fractional
	// remainder is not invented by ARCL; it remains the core's convention.
	assert(60*frame==999999960ULL);
	return 0;
}

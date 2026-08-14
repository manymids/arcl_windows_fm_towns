#include "arcl_vm_runner.h"

uint64_t ArclVmRunner::FrameFromTownsTime(uint64_t townsTime)
{
	return townsTime/FRAME_NS;
}

uint64_t ArclVmRunner::NextFrameBoundary(uint64_t townsTime)
{
	return (FrameFromTownsTime(townsTime)+1)*FRAME_NS;
}

uint64_t ArclVmRunner::IdleWakeTime(
    uint64_t townsTime,
    uint64_t nextFastDevicePollingTime,
    uint64_t frameEnd)
{
	if(frameEnd<=townsTime)
	{
		return townsTime;
	}

	// A polling time at or behind the current time must still make progress.
	const auto nextTime=(townsTime<nextFastDevicePollingTime ? nextFastDevicePollingTime+1 : townsTime+1);
	return (nextTime<frameEnd ? nextTime : frameEnd);
}

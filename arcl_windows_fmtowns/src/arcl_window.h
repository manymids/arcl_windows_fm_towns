/* LICENSE>>
Copyright 2026

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

<< LICENSE */
#ifndef ARCL_WINDOW_IS_INCLUDED
#define ARCL_WINDOW_IS_INCLUDED

#include <atomic>
#include <chrono>
#include <set>

#include "arcl_capture_host.h"

/*! Native ARCL frontend.

    It presents the RGBA snapshot, gathers direct input, and requests VM
    shutdown when its window is closed.  Rendering is rate limited separately
    from input polling so a slow GPU cannot throttle the emulated machine.
*/
class ArclNativeWindow : public ArclCaptureHost::CaptureWindow
{
public:
	struct GuiInput
	{
		std::set <unsigned int> townsKeys;
		bool a=false,b=false,left=false,right=false,up=false,down=false,run=false,pause=false,zoom=false;
	};
	struct Timing
	{
		uint64_t intervals=0,intervalNanoseconds=0,renders=0,renderNanoseconds=0;
	};

	void Start(void) override;
	void Stop(void) override;
	void Interval(void) override;
	void Render(bool swapBuffers) override;
	void Communicate(Outside_World *outsideWorld) override;
	GuiInput CopyGuiInput(void) const;
	Timing CopyTiming(void) const;

private:
	unsigned int textureId=0;
	bool opened=false;
	GuiInput guiInput;
	mutable std::mutex guiInputLock;
	std::chrono::steady_clock::time_point lastRender;
	std::atomic <uint64_t> intervalCount=0,intervalNanoseconds=0,renderCount=0,renderNanoseconds=0;

	static bool CloseWindowCallBack(void *thisPtr);
	void UpdateGuiInput(void);
};

/*! Selects the native frontend instead of the no-window capture endpoint. */
class ArclWindowedHost : public ArclCaptureHost
{
public:
	WindowInterface *CreateWindowInterface(void) const override;
	void DeleteWindowInterface(WindowInterface *window) const override;
	Outside_World::Sound *CreateSound(void) const override;
	void DeleteSound(Outside_World::Sound *sound) const override;
};

#endif

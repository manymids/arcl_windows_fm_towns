/* LICENSE>>
Copyright 2026

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

<< LICENSE */
#ifndef ARCL_CAPTURE_HOST_IS_INCLUDED
#define ARCL_CAPTURE_HOST_IS_INCLUDED

#include <cstdint>
#include <chrono>
#include <mutex>
#include <vector>

#include "outside_world.h"

/*! A headless Outside_World implementation that retains the latest rendered frame.

    The VM thread owns an ImageCopy until UpdateImage is called.  This class moves
    that image into a short critical section and exposes a copied, immutable
    snapshot to RPC and GUI consumers.  Consumers therefore never touch VM state
    or the renderer directly.
*/
class ArclCaptureHost : public Outside_World
{
public:
	ArclCaptureHost();

	std::string GetProgramResourceDirectory(void) const override;
	void Start(void) override;
	void Stop(void) override;
	void DevicePolling(class FMTownsCommon &towns) override;
	bool ImageNeedsFlip(void) override;
	void SetKeyboardLayout(unsigned int layout) override;

	class CaptureWindow : public WindowInterface
	{
	public:
		struct Frame
		{
			unsigned int width=0;
			unsigned int height=0;
			std::vector <unsigned char> rgba;
			uint64_t sequence=0;
		};

		void Start(void) override;
		void Stop(void) override;
		void Interval(void) override;
		void Render(bool swapBuffers) override;
		void UpdateImage(TownsRender::ImageCopy &img) override;
		void Communicate(Outside_World *) override;

		/*! Returns false until the first image is published. */
		bool CopyLatestFrame(Frame &frame) const;

	private:
		mutable std::mutex frameLock;
		Frame latestFrame;
	};

	WindowInterface *CreateWindowInterface(void) const override;
	void DeleteWindowInterface(WindowInterface *window) const override;
	Sound *CreateSound(void) const override;
	void DeleteSound(Sound *sound) const override;

private:
	mutable std::mutex captureWindowLock;
	mutable CaptureWindow *captureWindow=nullptr;
};

#endif

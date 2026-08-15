/* LICENSE>>
Copyright 2026

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

<< LICENSE */
#include "arcl_capture_host.h"

namespace
{
class ArclSilentSound final : public Outside_World::Sound
{
public:
	std::chrono::steady_clock::time_point fmPcmReady,beepReady;

	ArclSilentSound()
	{
		fmPcmReady=beepReady=std::chrono::steady_clock::now();
	}
	void Start(void) override {}
	void Stop(void) override {}
	void Polling(void) override {}
	void CDDAPlay(const DiscImage &,DiscImage::MinSecFrm,DiscImage::MinSecFrm,bool,unsigned int,unsigned int) override {}
	void CDDASetVolume(float,float) override {}
	void CDDAStop(void) override {}
	void CDDAPause(void) override {}
	void CDDAResume(void) override {}
	bool CDDAIsPlaying(void) override { return false; }
	DiscImage::MinSecFrm CDDACurrentPosition(void) override
	{
		DiscImage::MinSecFrm position;
		position.FromHSG(0);
		return position;
	}
	void FMPCMPlay(std::vector <unsigned char> &wave) override
	{
		fmPcmReady=std::chrono::steady_clock::now()+std::chrono::microseconds(10000*(wave.size()/4)/441);
	}
	void FMPCMPlayStop(void) override {}
	bool FMPCMChannelPlaying(void) override { return std::chrono::steady_clock::now()<fmPcmReady; }
	void BeepPlay(int,std::vector <unsigned char> &wave) override
	{
		beepReady=std::chrono::steady_clock::now()+std::chrono::microseconds(10000*(wave.size()/4)/441);
	}
	void BeepPlayStop(void) override {}
	bool BeepChannelPlaying() const override { return std::chrono::steady_clock::now()<beepReady; }
};
}

ArclCaptureHost::ArclCaptureHost()
{
	SetKeyboardMode(TOWNS_KEYBOARD_MODE_DIRECT);
	SetKeyboardLayout(KEYBOARD_LAYOUT_US);
}

std::string ArclCaptureHost::GetProgramResourceDirectory(void) const
{
	return ".";
}

void ArclCaptureHost::Start(void)
{
}

void ArclCaptureHost::Stop(void)
{
}

void ArclCaptureHost::DevicePolling(class FMTownsCommon &)
{
}

bool ArclCaptureHost::ImageNeedsFlip(void)
{
	return false;
}

void ArclCaptureHost::SetKeyboardLayout(unsigned int)
{
}

void ArclCaptureHost::CaptureWindow::Start(void)
{
}

void ArclCaptureHost::CaptureWindow::Stop(void)
{
}

void ArclCaptureHost::CaptureWindow::Interval(void)
{
	// RunFrames publishes its synchronous ForceRender result directly through
	// UpdateImage.  Unlike a GUI window, this headless host has no separate
	// renderer thread, so BaseInterval would attempt a second asynchronous render
	// from a transient CRTC state.  In particular EGB changes those registers in
	// several steps, and that intermediate state can describe an impractically
	// large image.  Keep polling limited to device state.
	std::lock_guard <std::mutex> lock(deviceStateLock);
	winThr.VMClosed=shared.VMClosedFromVMThread;
}

void ArclCaptureHost::CaptureWindow::Render(bool)
{
}

void ArclCaptureHost::CaptureWindow::UpdateImage(TownsRender::ImageCopy &img)
{
	std::lock_guard <std::mutex> lock(frameLock);
	latestFrame.width=img.wid;
	latestFrame.height=img.hei;
	latestFrame.rgba=std::move(img.rgba);
	++latestFrame.sequence;
}

void ArclCaptureHost::CaptureWindow::Communicate(Outside_World *)
{
}

bool ArclCaptureHost::CaptureWindow::CopyLatestFrame(Frame &frame) const
{
	std::lock_guard <std::mutex> lock(frameLock);
	if(0==latestFrame.sequence)
	{
		return false;
	}
	frame=latestFrame;
	return true;
}

bool ArclCaptureHost::CaptureWindow::CopyLatestFrameInfo(FrameInfo &info) const
{
	std::lock_guard <std::mutex> lock(frameLock);
	if(0==latestFrame.sequence)
	{
		return false;
	}
	info.width=latestFrame.width;
	info.height=latestFrame.height;
	info.rgbaBytes=latestFrame.rgba.size();
	info.sequence=latestFrame.sequence;
	return true;
}

Outside_World::WindowInterface *ArclCaptureHost::CreateWindowInterface(void) const
{
	auto *window=new CaptureWindow;
	std::lock_guard <std::mutex> lock(captureWindowLock);
	captureWindow=window;
	return window;
}

void ArclCaptureHost::DeleteWindowInterface(WindowInterface *window) const
{
	{
		std::lock_guard <std::mutex> lock(captureWindowLock);
		if(captureWindow==window)
		{
			captureWindow=nullptr;
		}
	}
	delete window;
}

Outside_World::Sound *ArclCaptureHost::CreateSound(void) const
{
	return new ArclSilentSound;
}

void ArclCaptureHost::DeleteSound(Sound *sound) const
{
	delete dynamic_cast <ArclSilentSound *>(sound);
}

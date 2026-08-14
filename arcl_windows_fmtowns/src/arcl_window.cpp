/* LICENSE>>
Copyright 2026

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

<< LICENSE */
#include <algorithm>

#include "arcl_window.h"
#include "fssimplewindow.h"
#include "fssimplewindow_connection.h"
#include "townsdef.h"

namespace
{
void AddKey(std::set <unsigned int> &keys,int fsKey,const char townsKey[])
{
	if(0!=FsGetKeyState(fsKey)) keys.insert(TownsStrToKeyCode(std::string("TOWNS_JISKEY_")+townsKey));
}
}

void ArclNativeWindow::Start(void)
{
	if(0==FsCheckWindowOpen())
	{
		FsOpenWindow(0,0,640,480,1,"Tsugaru ARCL");
		FsRegisterCloseWindowCallBack(CloseWindowCallBack,this);
	}

	glClearColor(0.0f,0.0f,0.0f,1.0f);
	glGenTextures(1,&textureId);
	glBindTexture(GL_TEXTURE_2D,textureId);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	lastRender=std::chrono::steady_clock::time_point();
	opened=true;
}

void ArclNativeWindow::Stop(void)
{
	if(0!=textureId)
	{
		glDeleteTextures(1,&textureId);
		textureId=0;
	}
	opened=false;
}

void ArclNativeWindow::Interval(void)
{
	const auto intervalStart=std::chrono::steady_clock::now();
	ArclCaptureHost::CaptureWindow::Interval();
	FsPollDevice();
	UpdateGuiInput();
	if(FSKEY_ESC==FsInkey())
	{
		std::lock_guard <std::mutex> lock(deviceStateLock);
		closeWindow=true;
	}
	const auto now=std::chrono::steady_clock::now();
	if(lastRender+std::chrono::milliseconds(16)<=now)
	{
		const auto renderStart=std::chrono::steady_clock::now();
		Render(true);
		renderNanoseconds+=std::chrono::duration_cast <std::chrono::nanoseconds>(std::chrono::steady_clock::now()-renderStart).count();
		++renderCount;
		lastRender=now;
	}
	intervalNanoseconds+=std::chrono::duration_cast <std::chrono::nanoseconds>(std::chrono::steady_clock::now()-intervalStart).count();
	++intervalCount;
}

ArclNativeWindow::GuiInput ArclNativeWindow::CopyGuiInput(void) const
{
	std::lock_guard <std::mutex> lock(guiInputLock);
	return guiInput;
}

ArclNativeWindow::Timing ArclNativeWindow::CopyTiming(void) const
{
	return {intervalCount.load(),intervalNanoseconds.load(),renderCount.load(),renderNanoseconds.load()};
}

void ArclNativeWindow::UpdateGuiInput(void)
{
	GuiInput input;
	for(int i=0; i<26; ++i)
	{
		if(0!=FsGetKeyState(FSKEY_A+i)) input.townsKeys.insert(TownsStrToKeyCode(std::string("TOWNS_JISKEY_")+char('A'+i)));
	}
	for(int i=0; i<10; ++i)
	{
		if(0!=FsGetKeyState(FSKEY_0+i)) input.townsKeys.insert(TownsStrToKeyCode(std::string("TOWNS_JISKEY_")+char('0'+i)));
	}
	const struct { int fsKey; const char *townsKey; } keyMap[]=
	{
		{FSKEY_SPACE,"SPACE"},{FSKEY_BS,"BACKSPACE"},{FSKEY_TAB,"TAB"},{FSKEY_ENTER,"RETURN"},
		{FSKEY_SHIFT,"SHIFT"},{FSKEY_CTRL,"CTRL"},{FSKEY_ALT,"ALT"},{FSKEY_UP,"UP"},{FSKEY_DOWN,"DOWN"},
		{FSKEY_LEFT,"LEFT"},{FSKEY_RIGHT,"RIGHT"},{FSKEY_INS,"INSERT"},{FSKEY_DEL,"DELETE"},{FSKEY_HOME,"HOME"},
		{FSKEY_PAGEUP,"PREV"},{FSKEY_PAGEDOWN,"NEXT"},{FSKEY_MINUS,"MINUS"},{FSKEY_TILDA,"AT"},
		{FSKEY_BACKSLASH,"BACKSLASH"},{FSKEY_SEMICOLON,"SEMICOLON"},{FSKEY_SINGLEQUOTE,"COLON"},
		{FSKEY_COMMA,"COMMA"},{FSKEY_DOT,"DOT"},{FSKEY_SLASH,"SLASH"}
	};
	for(const auto &mapping : keyMap) AddKey(input.townsKeys,mapping.fsKey,mapping.townsKey);
	for(int i=0; i<12; ++i) AddKey(input.townsKeys,FSKEY_F1+i,(std::string("PF")+(i<9 ? "0" : "")+std::to_string(i+1)).c_str());

	input.left=(0!=FsGetKeyState(FSKEY_LEFT));
	input.right=(0!=FsGetKeyState(FSKEY_RIGHT));
	input.up=(0!=FsGetKeyState(FSKEY_UP));
	input.down=(0!=FsGetKeyState(FSKEY_DOWN));
	input.a=(0!=FsGetKeyState(FSKEY_Z));
	input.b=(0!=FsGetKeyState(FSKEY_X));
	input.run=(0!=FsGetKeyState(FSKEY_A));
	input.pause=(0!=FsGetKeyState(FSKEY_S));
	input.zoom=(0!=FsGetKeyState(FSKEY_Q));
	std::lock_guard <std::mutex> lock(guiInputLock);
	guiInput=std::move(input);
}

void ArclNativeWindow::Communicate(Outside_World *outsideWorld)
{
	std::lock_guard <std::mutex> lock(deviceStateLock);
	outsideWorld->closeWindow=closeWindow;
}

void ArclNativeWindow::Render(bool swapBuffers)
{
	if(true!=opened)
	{
		return;
	}

	int windowWidth,windowHeight;
	FsGetWindowSize(windowWidth,windowHeight);
	glViewport(0,0,windowWidth,windowHeight);
	glClear(GL_COLOR_BUFFER_BIT);

	Frame frame;
	if(true==CopyLatestFrame(frame) && 0<frame.width && 0<frame.height)
	{
		const float scale=std::min(float(windowWidth)/frame.width,float(windowHeight)/frame.height);
		const float width=frame.width*scale;
		const float height=frame.height*scale;
		const float x0=(windowWidth-width)/2.0f;
		const float y0=(windowHeight-height)/2.0f;

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0.0f,double(windowWidth),double(windowHeight),0.0f,-1.0f,1.0f);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D,textureId);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,frame.width,frame.height,0,GL_RGBA,GL_UNSIGNED_BYTE,frame.rgba.data());
		glColor3f(1.0f,1.0f,1.0f);
		glBegin(GL_QUADS);
		glTexCoord2f(0.0f,1.0f); glVertex2f(x0,y0+height);
		glTexCoord2f(1.0f,1.0f); glVertex2f(x0+width,y0+height);
		glTexCoord2f(1.0f,0.0f); glVertex2f(x0+width,y0);
		glTexCoord2f(0.0f,0.0f); glVertex2f(x0,y0);
		glEnd();
		glDisable(GL_TEXTURE_2D);
	}

	if(true==swapBuffers)
	{
		FsSwapBuffers();
	}
}

bool ArclNativeWindow::CloseWindowCallBack(void *thisPtr)
{
	auto *window=static_cast <ArclNativeWindow *>(thisPtr);
	std::lock_guard <std::mutex> lock(window->deviceStateLock);
	window->closeWindow=true;
	return false;
}

Outside_World::WindowInterface *ArclWindowedHost::CreateWindowInterface(void) const
{
	return new ArclNativeWindow;
}

void ArclWindowedHost::DeleteWindowInterface(WindowInterface *window) const
{
	delete window;
}

Outside_World::Sound *ArclWindowedHost::CreateSound(void) const
{
	return new FsSimpleWindowConnection::SoundConnection;
}

void ArclWindowedHost::DeleteSound(Outside_World::Sound *sound) const
{
	auto *nativeSound=dynamic_cast <FsSimpleWindowConnection::SoundConnection *>(sound);
	delete nativeSound;
}

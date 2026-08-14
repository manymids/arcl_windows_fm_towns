#include <cstdlib>
#include <iostream>
#include <memory>

#include "arcl_capture_host.h"

namespace
{
void Check(bool condition,const char message[])
{
	if(true!=condition)
	{
		std::cerr << "FAILED: " << message << std::endl;
		std::exit(1);
	}
}
}

int main(void)
{
	auto window=std::make_unique <ArclCaptureHost::CaptureWindow>();
	ArclCaptureHost::CaptureWindow::Frame frame;
	Check(false==window->CopyLatestFrame(frame),"A new capture window must be empty.");

	TownsRender::ImageCopy image;
	image.wid=2;
	image.hei=1;
	image.rgba={1,2,3,4,5,6,7,8};
	window->UpdateImage(image);

	Check(true==image.rgba.empty(),"UpdateImage must transfer ownership from the renderer.");
	Check(true==window->CopyLatestFrame(frame),"Published image must be readable.");
	Check(2==frame.width && 1==frame.height,"Frame dimensions must be preserved.");
	Check(1==frame.sequence,"First image must have sequence one.");
	Check(std::vector<unsigned char>({1,2,3,4,5,6,7,8})==frame.rgba,"RGBA bytes must be preserved.");

	image.wid=1;
	image.hei=1;
	image.rgba={9,10,11,12};
	window->UpdateImage(image);
	Check(true==window->CopyLatestFrame(frame),"Replacement image must be readable.");
	Check(2==frame.sequence && 1==frame.width && 1==frame.height,"Frame metadata must advance together.");
	Check(std::vector<unsigned char>({9,10,11,12})==frame.rgba,"Latest image must replace the previous image.");

	return 0;
}

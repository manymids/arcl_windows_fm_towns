/*
  Phase-0 executable for the ARCL host.

  This is intentionally a finite, deterministic runner rather than an MCP
  server.  The command server will become the sole public owner of this loop in
  Phase 1.
*/
#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#ifdef OUT
#undef OUT
#endif
#ifdef interface
#undef interface
#endif
#endif

#include "arcl_capture_host.h"
#include "arcl_mcp_server.h"
#include "arcl_vm_runner.h"
#include "arcl_window.h"
#include "i486debugmemaccess.h"
#include "i486symtable.h"
#include "townsargv.h"
#include "yspngenc.h"

namespace
{
struct OutsideWorldSoundDeleter
{
	Outside_World *outsideWorld=nullptr;
	void operator()(Outside_World::Sound *sound) const
	{
		if(nullptr!=outsideWorld && nullptr!=sound) outsideWorld->DeleteSound(sound);
	}
};

struct OutsideWorldWindowDeleter
{
	Outside_World *outsideWorld=nullptr;
	void operator()(Outside_World::WindowInterface *window) const
	{
		if(nullptr!=outsideWorld && nullptr!=window) outsideWorld->DeleteWindowInterface(window);
	}
};

std::string Base64(const unsigned char data[],size_t length)
{
	static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string encoded;
	encoded.reserve((length+2)/3*4);
	for(size_t i=0; i<length; i+=3)
	{
		const auto value=(unsigned(data[i])<<16)|((i+1<length ? unsigned(data[i+1]) : 0)<<8)|(i+2<length ? unsigned(data[i+2]) : 0);
		encoded+=alphabet[(value>>18)&63]; encoded+=alphabet[(value>>12)&63];
		encoded+=(i+1<length ? alphabet[(value>>6)&63] : '=');
		encoded+=(i+2<length ? alphabet[value&63] : '=');
	}
	return encoded;
}

std::string JsonString(const std::string &json,const char key[])
{
	auto p=json.find(std::string("\"")+key+"\"");
	if(std::string::npos==p || std::string::npos==(p=json.find('"',json.find(':',p)+1))) return "";
	auto e=json.find('"',p+1);
	return (std::string::npos==e ? "" : json.substr(p+1,e-p-1));
}
uint64_t JsonUnsigned(const std::string &json,const char key[])
{
	auto p=json.find(std::string("\"")+key+"\"");
	if(std::string::npos==p || std::string::npos==(p=json.find(':',p))) return 0;
	return std::strtoull(json.c_str()+p+1,nullptr,10);
}
bool JsonUnsignedOrHex(const std::string &json,const char key[],uint64_t &value)
{
	auto p=json.find(std::string("\"")+key+"\"");
	if(std::string::npos==p || std::string::npos==(p=json.find(':',p))) return false;
	++p;
	while(p<json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
	if(p<json.size() && '"'==json[p])
	{
		const auto e=json.find('"',p+1);
		if(std::string::npos==e) return false;
		const auto text=json.substr(p+1,e-p-1);
		char *endPtr=nullptr;
		value=std::strtoull(text.c_str(),&endPtr,0);
		return ('\0'==*endPtr);
	}
	char *endPtr=nullptr;
	value=std::strtoull(json.c_str()+p,&endPtr,10);
	return (json.c_str()+p!=endPtr);
}
int JsonInteger(const std::string &json,const char key[])
{
	auto p=json.find(std::string("\"")+key+"\"");
	if(std::string::npos==p || std::string::npos==(p=json.find(':',p))) return 0;
	return static_cast<int>(std::strtol(json.c_str()+p+1,nullptr,10));
}
bool JsonBoolean(const std::string &json,const char key[])
{
	auto p=json.find(std::string("\"")+key+"\"");
	if(std::string::npos==p || std::string::npos==(p=json.find(':',p))) return false;
	++p;
	while(p<json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
	return 0==json.compare(p,4,"true");
}
bool HasJsonKey(const std::string &json,const char key[])
{
	return std::string::npos!=json.find(std::string("\"")+key+"\"");
}
std::string JsonArguments(const std::string &request)
{
	auto p=request.find("\"arguments\"");
	if(std::string::npos==p || std::string::npos==(p=request.find('{',p))) return "{}";
	bool inString=false,escaped=false;
	int depth=0;
	for(auto e=p; e<request.size(); ++e)
	{
		if(inString)
		{
			if(escaped) escaped=false;
			else if('\\'==request[e]) escaped=true;
			else if('"'==request[e]) inString=false;
			continue;
		}
		if('"'==request[e]) inString=true;
		else if('{'==request[e]) ++depth;
		else if('}'==request[e] && 0==--depth) return request.substr(p,e-p+1);
	}
	return "{}";
}
bool IsSafeRelativePath(const std::string &text)
{
	const std::filesystem::path path=text;
	if(path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
	for(const auto &component : path)
	{
		if("."==component || ".."==component) return false;
	}
	return true;
}
bool IsPathWithin(const std::filesystem::path &candidate,const std::filesystem::path &root)
{
	std::error_code error;
	const auto normalizedCandidate=std::filesystem::weakly_canonical(candidate,error);
	if(error) return false;
	const auto normalizedRoot=std::filesystem::weakly_canonical(root,error);
	if(error) return false;
	auto relative=std::filesystem::relative(normalizedCandidate,normalizedRoot,error);
	if(error) return false;
	return (!relative.is_absolute() && (relative.empty() || "."==relative || ".."!=*relative.begin()));
}
std::string JsonEscape(const std::string &text)
{
	std::string escaped;
	for(auto c : text)
	{
		switch(c)
		{
		case '\\': escaped+="\\\\"; break;
		case '"': escaped+="\\\""; break;
		case '\n': escaped+="\\n"; break;
		case '\r': escaped+="\\r"; break;
		case '\t': escaped+="\\t"; break;
		default: escaped+=c; break;
		}
	}
	return escaped;
}

std::string HexBytes(const std::vector <unsigned char> &data)
{
	static const char hex[]="0123456789abcdef";
	std::string text;
	text.reserve(data.size()*2);
	for(auto byte : data)
	{
		text.push_back(hex[byte>>4]);
		text.push_back(hex[byte&15]);
	}
	return text;
}

std::string RgbHex(const Vec4ub &color)
{
	static const char hex[]="0123456789abcdef";
	std::string text="#";
	for(unsigned int component=0; component<3; ++component)
	{
		text.push_back(hex[color[component]>>4]);
		text.push_back(hex[color[component]&15]);
	}
	return text;
}

bool ParseHexBytes(const std::string &text,std::vector <unsigned char> &data)
{
	if(text.empty() || 0!=(text.size()&1)) return false;
	data.clear();
	data.reserve(text.size()/2);
	for(size_t i=0; i<text.size(); i+=2)
	{
		auto HexValue=[](char c)
		{
			if('0'<=c && c<='9') return c-'0';
			if('a'<=c && c<='f') return c-'a'+10;
			if('A'<=c && c<='F') return c-'A'+10;
			return -1;
		};
		const auto high=HexValue(text[i]);
		const auto low=HexValue(text[i+1]);
		if(0>high || 0>low) return false;
		data.push_back(static_cast<unsigned char>((high<<4)|low));
	}
	return true;
}

struct ArclOptions
{
	bool noWindow=false;
	bool mcp=false;
	bool profile=false;
	bool framesSpecified=false;
	uint64_t frames=60;
	std::set <std::string> mcpLayers={"control","l0"};
	std::filesystem::path outputDirectory="arcl-output";
	std::filesystem::path stateDirectory="arcl-state";
	std::vector <std::filesystem::path> allowRoots;
};

std::filesystem::path ExecutableDirectory(const char *argv0)
{
#ifdef _WIN32
	std::vector <wchar_t> modulePath(32768);
	const auto length=GetModuleFileNameW(nullptr,modulePath.data(),static_cast<DWORD>(modulePath.size()));
	if(0<length && length<modulePath.size())
	{
		return std::filesystem::path(std::wstring(modulePath.data(),length)).parent_path();
	}
#endif
	std::error_code error;
	const auto absolutePath=std::filesystem::absolute(argv0,error);
	if(!error) return absolutePath.parent_path();
	return std::filesystem::current_path(error);
}

enum ParseResult
{
	PARSE_OK,
	PARSE_ERROR,
	PARSE_HELP,
};

bool ParseMcpLayers(const std::string &text,std::set <std::string> &layers)
{
	if(text.empty() || ','==text.back()) return false;
	std::set <std::string> parsed;
	for(size_t begin=0; begin<text.size(); )
	{
		auto end=text.find(',',begin);
		if(std::string::npos==end) end=text.size();
		auto layer=text.substr(begin,end-begin);
		for(auto &c : layer) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if(layer.empty() || ("control"!=layer && "l0"!=layer && "l1"!=layer && "l2"!=layer && "l3"!=layer && "l4"!=layer))
		{
			return false;
		}
		parsed.insert(layer);
		begin=end+1;
	}
	if(parsed.empty()) return false;
	parsed.insert("control");
	layers=std::move(parsed);
	return true;
}

ParseResult ParseOptions(int ac,char *av[],ArclOptions &options,std::vector <char *> &townsArgv)
{
	townsArgv.push_back(av[0]);
	for(int i=1; i<ac; ++i)
	{
		const std::string arg=av[i];
		if("--no-window"==arg)
		{
			options.noWindow=true;
		}
		else if("--arcl-profile"==arg)
		{
			options.profile=true;
		}
		else if("--mcp"==arg)
		{
			options.mcp=true;
		}
		else if("--mcp-layers"==arg)
		{
			if(i+1==ac || !ParseMcpLayers(av[++i],options.mcpLayers))
			{
				std::cerr << "--mcp-layers requires a non-empty comma-separated subset of control,l0,l1,l2,l3,l4." << std::endl;
				return PARSE_ERROR;
			}
		}
		else if("--arcl-output-dir"==arg || "--arcl-state-dir"==arg || "--arcl-allow-root"==arg)
		{
			if(i+1==ac)
			{
				std::cerr << arg << " requires a path." << std::endl;
				return PARSE_ERROR;
			}
			const std::filesystem::path path=av[++i];
			if("--arcl-output-dir"==arg) options.outputDirectory=path;
			else if("--arcl-state-dir"==arg) options.stateDirectory=path;
			else options.allowRoots.push_back(path);
		}
		else if("--arcl-frames"==arg)
		{
			if(i+1==ac)
			{
				std::cerr << "--arcl-frames requires a non-negative integer." << std::endl;
				return PARSE_ERROR;
			}
			char *endPtr=nullptr;
			const auto parsed=std::strtoull(av[++i],&endPtr,10);
			if('\0'!=*endPtr)
			{
				std::cerr << "--arcl-frames requires a non-negative integer." << std::endl;
				return PARSE_ERROR;
			}
			options.frames=parsed;
			options.framesSpecified=true;
		}
		else if("--arcl-help"==arg)
		{
			std::cout << "Usage: arcl_windows_fmtowns [--mcp] [--mcp-layers control,l0,...] [--arcl-output-dir DIR] [--arcl-state-dir DIR] [--arcl-allow-root DIR] [--no-window] [--arcl-frames N] [--arcl-profile] <TOWNSEMU options>" << std::endl;
			std::cout << "With a GUI, runs until the window closes unless --arcl-frames N is specified." << std::endl;
			return PARSE_HELP;
		}
		else
		{
			townsArgv.push_back(av[i]);
		}
	}
	return PARSE_OK;
}

template <class TownsClass>
class ArclSession
{
	TownsClass &towns;
	Outside_World &outsideWorld;
	Outside_World::Sound &sound;
	Outside_World::WindowInterface &window;
	const ArclOptions &options;
	TownsRender render;
	bool started=false;
	bool windowServicesOnVmThread=true;
	bool running=false;
	std::chrono::steady_clock::time_point profileStart;
	uint64_t vmRunNanoseconds=0,periodicNanoseconds=0,soundNanoseconds=0,deviceNanoseconds=0,publishNanoseconds=0,vmSlices=0;
	uint64_t textMatchConsoleGeneration=UINT64_MAX;
	std::string textMatchConsole;
	std::set <unsigned int> heldKeys;
	std::set <unsigned int> guiKeys;
	bool mouseLeft=false,mouseRight=false;
	int mousePort=-1,mouseDx=0,mouseDy=0;
	struct JoypadInput
	{
		bool a=false,b=false,left=false,right=false,up=false,down=false,run=false,pause=false,zoom=false;
	};
	JoypadInput joypads[2];
	JoypadInput guiJoypad;
	struct MemoryWatchpoint
	{
		std::string type;
		unsigned int address=0;
		unsigned int length=0;
	};
	struct ExecutionBreakpoint
	{
		unsigned int cs=0;
		unsigned int address=0;
	};
	struct Snapshot
	{
		std::vector <uint8_t> state;
		uint64_t frame=0;
		uint64_t serial=0;
		std::set <unsigned int> heldKeys;
		bool mouseLeft=false,mouseRight=false;
		int mousePort=-1,mouseDx=0,mouseDy=0;
		JoypadInput joypads[2];
	};
	std::vector <MemoryWatchpoint> memoryWatchpoints;
	std::vector <ExecutionBreakpoint> executionBreakpoints;
	std::map <std::string,Snapshot> snapshots;
	uint64_t nextSnapshotSerial=0;
	std::deque <Snapshot> rewindHistory;
	unsigned int speedPercent=100;
	std::chrono::steady_clock::time_point nextContinuousDeadline;

public:
	ArclSession(TownsClass &towns,Outside_World &outsideWorld,Outside_World::Sound &sound,Outside_World::WindowInterface &window,const ArclOptions &options)
		: towns(towns),outsideWorld(outsideWorld),sound(sound),window(window),options(options)
	{
	}

	std::string LayersJson(void) const
	{
		std::string json="[";
		for(const auto &layer : options.mcpLayers)
		{
			if(1<json.size()) json+=',';
			json+="\""+("control"==layer ? std::string("Control") : layer)+"\"";
		}
		return json+"]";
	}

	bool IsToolVisible(const std::string &name) const
	{
		const bool control=(
			"arcl_status"==name || "arcl_run"==name || "arcl_pause"==name || "arcl_resume"==name ||
			"arcl_reset"==name || "arcl_save_state"==name || "arcl_load_state"==name);
		const bool l0=(
			"arcl_screenshot"==name || "arcl_key"==name || "arcl_type"==name || "arcl_mouse"==name ||
			"arcl_joypad"==name || "arcl_input_macro"==name || "arcl_input_state"==name || "arcl_clear_input"==name || "arcl_audio_record"==name);
		const bool l1=("arcl_console_read"==name || "arcl_command"==name || "arcl_host_dir"==name || "arcl_mount"==name);
		const bool l2=(
			"arcl_registers"==name || "arcl_write_registers"==name || "arcl_read_mem"==name || "arcl_write_mem"==name ||
			"arcl_disasm"==name || "arcl_stack"==name || "arcl_breakpoint"==name || "arcl_step"==name);
		const bool l4=("arcl_snapshot"==name || "arcl_rewind"==name || "arcl_speed"==name);
		const bool l3=("arcl_video"==name || "arcl_palette"==name || "arcl_irq"==name || "arcl_dma"==name || "arcl_sprites"==name || "arcl_vram"==name || "towns_ym2612"==name || "towns_rf5c68"==name);
		return (control ||
			(l0 && options.mcpLayers.end()!=options.mcpLayers.find("l0")) ||
			(l1 && options.mcpLayers.end()!=options.mcpLayers.find("l1")) ||
			(l2 && options.mcpLayers.end()!=options.mcpLayers.find("l2")) ||
			(l4 && options.mcpLayers.end()!=options.mcpLayers.find("l4")) ||
			(l3 && options.mcpLayers.end()!=options.mcpLayers.find("l3")));
	}

	std::string VideoJson(void) const
	{
		const auto &crtc=towns.crtc;
		std::string pages="[";
		for(unsigned int page=0; page<2; ++page)
		{
			TownsCrtcBase::Layer layer;
			crtc.MakePageLayerInfo(layer,static_cast<unsigned char>(page));
			if(1<pages.size()) pages+=',';
			pages+="{\"index\":"+std::to_string(page)+",\"visible\":"+(crtc.state.ShowPage(page) ? "true" : "false")+
				",\"bits_per_pixel\":"+std::to_string(layer.bitsPerPixel)+",\"vram_address\":"+std::to_string(layer.VRAMAddr)+
				",\"vram_offset\":"+std::to_string(layer.VRAMOffset)+",\"bytes_per_line\":"+std::to_string(layer.bytesPerLine)+
				",\"origin\":{\"x\":"+std::to_string(layer.originOnMonitor.x())+",\"y\":"+std::to_string(layer.originOnMonitor.y())+"}"+
				",\"display_size\":{\"width\":"+std::to_string(layer.sizeOnMonitor.x())+",\"height\":"+std::to_string(layer.sizeOnMonitor.y())+"}"+
				",\"vram_coverage\":{\"width\":"+std::to_string(layer.VRAMCoverage1X.x())+",\"height\":"+std::to_string(layer.VRAMCoverage1X.y())+"}"+
				",\"zoom_2x\":{\"x\":"+std::to_string(layer.zoom2x.x())+",\"y\":"+std::to_string(layer.zoom2x.y())+"}}";
		}
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"mode\":\""+
			(crtc.state.highResCRTCEnabled ? "high_resolution" : "standard")+"\",\"single_page\":"+(crtc.InSinglePageMode() ? "true" : "false")+
			",\"priority_page\":"+std::to_string(crtc.GetPriorityPage())+",\"effective_vram_bytes\":"+std::to_string(crtc.GetEffectiveVRAMSize())+
			",\"vsync\":"+std::string(crtc.state.VSYNC ? "true" : "false")+",\"pages\":"+pages+"]}";
	}

	std::string PaletteJson(const std::string &request) const
	{
		const auto &crtc=towns.crtc;
		const auto requested=JsonString(request,"format");
		const auto priorityPage=crtc.GetPriorityPage();
		const auto format=(requested.empty() || "active"==requested ? (8<=crtc.GetPageBitsPerPixel(priorityPage) ? std::string("256") : std::string("16")) : requested);
		const auto page=(HasJsonKey(request,"page") ? JsonUnsigned(request,"page") : priorityPage);
		if(("16"!=format && "256"!=format) || 1<page) return "{\"error\":\"invalid_palette_format\"}";
		const auto &palette=crtc.GetPalette();
		std::string entries="[";
		const auto count=("16"==format ? 16U : 256U);
		for(unsigned int index=0; index<count; ++index)
		{
			if(1<entries.size()) entries+=',';
			const auto &color=("16"==format ? palette.plt16[page][index] : palette.plt256[index]);
			entries+="{\"index\":"+std::to_string(index)+",\"rgb\":\""+RgbHex(color)+"\"}";
		}
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"format\":\""+format+"\",\"page\":"+std::to_string(page)+",\"entries\":"+entries+"]}";
	}

	std::string IrqJson(void) const
	{
		std::string chips="[";
		std::string pending="[",inService="[",masked="[";
		for(unsigned int chip=0; chip<2; ++chip)
		{
			const auto &pic=towns.pic.state.i8259A[chip];
			if(0<chip) chips+=",";
			std::string requests="[",services="[",masks="[";
			for(unsigned int line=0; line<8; ++line)
			{
				const auto irq=chip*8+line;
				const auto requested=towns.pic.GetInterruptRequestBit(irq);
				const auto servicing=(0!=(pic.ISR&(1<<line)));
				const auto isMasked=(0!=(pic.IMR&(1<<line)));
				if(requested)
				{
					if(1<requests.size()) requests+=",";
					if(1<pending.size()) pending+=",";
					requests+=std::to_string(line);
					pending+=std::to_string(irq);
				}
				if(servicing)
				{
					if(1<services.size()) services+=",";
					if(1<inService.size()) inService+=",";
					services+=std::to_string(line);
					inService+=std::to_string(irq);
				}
				if(isMasked)
				{
					if(1<masks.size()) masks+=",";
					if(1<masked.size()) masked+=",";
					masks+=std::to_string(line);
					masked+=std::to_string(irq);
				}
			}
			chips+="{\"index\":"+std::to_string(chip)+",\"vector_base\":"+std::to_string(pic.GetT())+",\"requests\":"+requests+"],\"in_service\":"+services+"],\"masked\":"+masks+"]}";
		}
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"cpu_interrupts_enabled\":"+(towns.CPU().GetIF() ? "true" : "false")+",\"pending_irqs\":"+pending+"],\"in_service_irqs\":"+inService+"],\"masked_irqs\":"+masked+"],\"chips\":"+chips+"]}";
	}

	std::string DmaJson(void) const
	{
		static const char *const deviceName[]={"floppy","scsi","printer","cdrom"};
		const auto &dmac=towns.dmac.state;
		std::string channels="[";
		for(unsigned int index=0; index<4; ++index)
		{
			const auto &channel=dmac.ch[index];
			if(0<index) channels+=",";
			const auto enabled=(0==(dmac.mask&(1<<index)));
			const auto requested=(0!=(dmac.req&(1<<index)));
			const auto remaining=channel.CountsAvailable();
			channels+="{\"index\":"+std::to_string(index)+",\"device\":\""+deviceName[index]+"\",\"enabled\":"+(enabled ? "true" : "false")+",\"requested\":"+(requested ? "true" : "false")+
				",\"terminal_count\":"+(channel.terminalCount ? "true" : "false")+",\"auto_initialize\":"+(channel.AUTI() ? "true" : "false")+",\"mode_control\":"+std::to_string(channel.modeCtrl)+
				",\"bytes_per_count\":"+std::to_string(channel.bytesPerCount)+",\"base_address\":"+std::to_string(channel.baseAddr)+",\"current_address\":"+std::to_string(channel.currentAddr)+
				",\"base_count\":"+std::to_string(channel.baseCount)+",\"current_count\":"+std::to_string(channel.currentCount)+",\"remaining_counts\":"+std::to_string(remaining)+",\"remaining_bytes\":"+std::to_string(remaining*channel.bytesPerCount)+"}";
		}
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"transfer_bits\":"+std::to_string(dmac.bitSize)+",\"selected_channel\":"+std::to_string(dmac.SELCH)+
			",\"base_register_selected\":"+(dmac.BASE ? "true" : "false")+",\"device_control\":["+std::to_string(dmac.devCtrl[0])+","+std::to_string(dmac.devCtrl[1])+"],\"request_mask\":"+std::to_string(dmac.req)+",\"channel_mask\":"+std::to_string(dmac.mask)+",\"channels\":"+channels+"]}";
	}

	std::string SpritesJson(const std::string &request) const
	{
		constexpr unsigned int MAX_SPRITES=128;
		const auto start=(HasJsonKey(request,"start") ? JsonUnsigned(request,"start") : 0);
		const auto count=(HasJsonKey(request,"count") ? JsonUnsigned(request,"count") : 32);
		if(TownsSprite::MAX_NUM_SPRITE_INDEX<=start || 0==count || MAX_SPRITES<count || TownsSprite::MAX_NUM_SPRITE_INDEX-start<count) return "{\"error\":\"invalid_sprite_range\"}";
		const auto &state=towns.sprite.state;
		const auto enabled=(0!=(state.reg[TownsSprite::REG_CONTROL1]&0x80));
		const auto firstSprite=((state.reg[TownsSprite::REG_CONTROL1]<<8)|state.reg[TownsSprite::REG_CONTROL0])&0x3ff;
		const auto hOffset=((state.reg[TownsSprite::REG_HORIZONTAL_OFFSET1]<<8)|state.reg[TownsSprite::REG_HORIZONTAL_OFFSET0])&0x1ff;
		const auto vOffset=((state.reg[TownsSprite::REG_VERTICAL_OFFSET1]<<8)|state.reg[TownsSprite::REG_VERTICAL_OFFSET0])&0x1ff;
		std::string sprites="[";
		for(unsigned int index=static_cast<unsigned int>(start); index<start+count; ++index)
		{
			const auto *entry=towns.physMem.state.spriteRAM+TownsSprite::SPRITERAM_INDEX_OFFSET+(index<<3);
			const auto x=static_cast<unsigned int>(entry[0])|(static_cast<unsigned int>(entry[1])<<8);
			const auto y=static_cast<unsigned int>(entry[2])|(static_cast<unsigned int>(entry[3])<<8);
			const auto attr=static_cast<unsigned int>(entry[4])|(static_cast<unsigned int>(entry[5])<<8);
			const auto palette=static_cast<unsigned int>(entry[6])|(static_cast<unsigned int>(entry[7])<<8);
			if(1<sprites.size()) sprites+=",";
			sprites+="{\"index\":"+std::to_string(index)+",\"x\":"+std::to_string(x&0x1ff)+",\"y\":"+std::to_string(y&0x1ff)+",\"pattern\":"+std::to_string(attr&TownsSprite::ATTR_PAT_MASK)+
				",\"rotation\":"+std::to_string((attr&TownsSprite::ATTR_ROT_MASK)>>TownsSprite::ATTR_ROT_SHIFT)+",\"offset_applied\":"+(0!=(attr&TownsSprite::ATTR_OFFS) ? "true" : "false")+
				",\"scale_x\":"+(0!=(attr&TownsSprite::ATTR_SUX) ? "true" : "false")+",\"scale_y\":"+(0!=(attr&TownsSprite::ATTR_SUY) ? "true" : "false")+
				",\"palette\":"+std::to_string(palette&TownsSprite::PALETTE_INDEX_MASK)+",\"hidden\":"+(0!=(palette&TownsSprite::PALETTE_DISP) ? "true" : "false")+
				",\"color_table_enabled\":"+(0!=(palette&TownsSprite::PALETTE_CTEN) ? "true" : "false")+",\"sprite_y_scroll\":"+(0!=(palette&TownsSprite::PALETTE_SPYS) ? "true" : "false")+"}";
		}
		const auto displayPage=(enabled ? !state.page : (0!=(state.reg[TownsSprite::REG_DISPLAY_PAGE]&0x80)));
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"enabled\":"+(enabled ? "true" : "false")+",\"active\":"+(enabled && state.screenModeAcceptsSprite ? "true" : "false")+
			",\"busy\":"+(state.spriteBusy ? "true" : "false")+",\"display_page\":"+(displayPage ? "true" : "false")+",\"first_sprite_index\":"+std::to_string(firstSprite)+",\"first_sprite_index_capture\":"+std::to_string(state.firstSpriteIndexCapture)+
			",\"horizontal_offset\":"+std::to_string(hOffset)+",\"vertical_offset\":"+std::to_string(vOffset)+",\"start\":"+std::to_string(start)+",\"count\":"+std::to_string(count)+",\"sprites\":"+sprites+"]}";
	}

	std::string VramJson(const std::string &request) const
	{
		constexpr uint64_t MAX_VRAM_BYTES=4096;
		uint64_t offset=0;
		const auto length=JsonUnsigned(request,"length");
		const auto vramBytes=towns.physMem.GetVRAMSize();
		if(!JsonUnsignedOrHex(request,"offset",offset) || 0==length || MAX_VRAM_BYTES<length || vramBytes<=offset || length>vramBytes-offset) return "{\"error\":\"invalid_vram_range\"}";
		std::vector <unsigned char> data;
		data.reserve(static_cast<size_t>(length));
		for(uint64_t i=0; i<length; ++i) data.push_back(towns.physMem.state.VRAM[offset+i]);
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"offset\":"+std::to_string(offset)+",\"length\":"+std::to_string(length)+",\"vram_bytes\":"+std::to_string(vramBytes)+
			",\"effective_vram_bytes\":"+std::to_string(towns.crtc.GetEffectiveVRAMSize())+",\"data_hex\":\""+HexBytes(data)+"\"}";
	}

	std::string Ym2612Json(void) const
	{
		const auto &ym=towns.sound.state.ym2612;
		std::string channels="[";
		for(unsigned int index=0; index<YM2612::NUM_CHANNELS; ++index)
		{
			const auto &channel=ym.state.channels[index];
			if(0<index) channels+=",";
			channels+="{\"index\":"+std::to_string(index)+",\"playing\":"+(0!=(ym.state.playingCh&(1<<index)) ? "true" : "false")+",\"frequency_number\":"+std::to_string(channel.F_NUM)+",\"block\":"+std::to_string(channel.BLOCK)+",\"note\":"+std::to_string(channel.Note())+",\"feedback\":"+std::to_string(channel.FB)+",\"connection\":"+std::to_string(channel.CONNECT)+",\"pan_left\":"+(0!=channel.L ? "true" : "false")+",\"pan_right\":"+(0!=channel.R ? "true" : "false")+",\"ams\":"+std::to_string(channel.AMS)+",\"pms\":"+std::to_string(channel.PMS)+",\"key_slots\":"+std::to_string(channel.usingSlot)+"}";
		}
		return "{\"machine\":\"towns\",\"frame\":"+std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"playing_mask\":"+std::to_string(ym.state.playingCh)+",\"lfo\":"+(ym.state.LFO ? "true" : "false")+",\"timer_up\":["+(ym.state.timerUp[0] ? "true" : "false")+","+(ym.state.timerUp[1] ? "true" : "false")+"],\"channels\":"+channels+"]}";
	}

	std::string Rf5c68Json(void) const
	{
		const auto &pcm=towns.sound.state.rf5c68;
		std::string channels="[";
		for(unsigned int index=0; index<RF5C68::NUM_CHANNELS; ++index)
		{
			const auto &channel=pcm.state.ch[index];
			if(0<index) channels+=",";
			channels+="{\"index\":"+std::to_string(index)+",\"enabled\":"+(0==(pcm.state.chOnOff&(1<<index)) ? "true" : "false")+",\"envelope\":"+std::to_string(channel.ENV)+",\"pan\":"+std::to_string(channel.PAN)+",\"frequency\":"+std::to_string(channel.FD)+",\"loop_start\":"+std::to_string(channel.LS)+",\"play_pointer\":"+std::to_string(channel.playPtr)+",\"repeat\":"+(channel.repeatAfterThisSegment ? "true" : "false")+",\"irq_after_playback\":"+(channel.IRQAfterThisPlayBack ? "true" : "false")+"}";
		}
		return "{\"machine\":\"towns\",\"frame\":"+std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"playing\":"+(pcm.state.playing ? "true" : "false")+",\"channel_off_mask\":"+std::to_string(pcm.state.chOnOff)+",\"irq\":"+(pcm.state.IRQ() ? "true" : "false")+",\"irq_bank\":"+std::to_string(pcm.state.IRQBank)+",\"irq_bank_mask\":"+std::to_string(pcm.state.IRQBankMask)+",\"channels\":"+channels+"]}";
	}

	std::string RegistersJson(void) const
	{
		const auto &cpu=towns.CPU();
		const auto &state=cpu.state;
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"registers\":{\"eax\":"+std::to_string(state.EAX())+",\"ebx\":"+std::to_string(state.EBX())+
			",\"ecx\":"+std::to_string(state.ECX())+",\"edx\":"+std::to_string(state.EDX())+",\"esi\":"+std::to_string(state.ESI())+",\"edi\":"+std::to_string(state.EDI())+
			",\"ebp\":"+std::to_string(state.EBP())+",\"esp\":"+std::to_string(state.ESP())+",\"eip\":"+std::to_string(state.EIP)+",\"eflags\":"+std::to_string(state.EFLAGS)+
			",\"cs\":"+std::to_string(state.CS().value)+",\"ds\":"+std::to_string(state.DS().value)+",\"es\":"+std::to_string(state.ES().value)+",\"fs\":"+std::to_string(state.FS().value)+
			",\"gs\":"+std::to_string(state.GS().value)+",\"ss\":"+std::to_string(state.SS().value)+",\"cr0\":"+std::to_string(state.GetCR(0))+",\"cr2\":"+std::to_string(state.GetCR(2))+",\"cr3\":"+std::to_string(state.GetCR(3))+"}}";
	}

	std::string WriteRegistersJson(const std::string &request)
	{
		auto &cpu=towns.CPU();
		uint64_t value=0;
		std::string updated;
		auto Write=[&](const char name[],unsigned int &reg)
		{
			if(!JsonUnsignedOrHex(request,name,value)) return true;
			if(0xffffffffULL<value) return false;
			reg=static_cast<unsigned int>(value);
			if(!updated.empty()) updated+=',';
			updated+="\""+std::string(name)+"\"";
			return true;
		};
		if(!Write("eax",cpu.state.EAX()) || !Write("ebx",cpu.state.EBX()) || !Write("ecx",cpu.state.ECX()) || !Write("edx",cpu.state.EDX()) ||
		   !Write("esi",cpu.state.ESI()) || !Write("edi",cpu.state.EDI()) || !Write("ebp",cpu.state.EBP()) || !Write("esp",cpu.state.ESP()) || !Write("eip",cpu.state.EIP) ||
		   !Write("eflags",cpu.state.EFLAGS)) return "{\"error\":\"invalid_register_value\"}";
		cpu.state.EFLAGS=(cpu.state.EFLAGS&i486DXCommon::EFLAGS_MASK)|i486DXCommon::EFLAGS_ALWAYS_ON;
		if(updated.empty()) return "{\"error\":\"no_writable_register\"}";
		const auto registers=RegistersJson();
		return registers.substr(0,registers.size()-1)+",\"updated\":["+updated+"]}";
	}

	std::string ReadMemoryJson(const std::string &request) const
	{
		constexpr uint64_t MAX_MEMORY_BYTES=4096;
		uint64_t address=0;
		const auto length=JsonUnsigned(request,"length");
		if(!JsonUnsignedOrHex(request,"address",address) || 0==length || MAX_MEMORY_BYTES<length || 0xffffffffULL<address || address+length-1>0xffffffffULL) return "{\"error\":\"invalid_memory_range\"}";
		std::vector <unsigned char> data;
		data.reserve(static_cast<size_t>(length));
		const auto debugBreakWasSet=towns.CheckDebugBreak();
		for(uint64_t i=0; i<length; ++i) data.push_back(static_cast<unsigned char>(towns.mem.FetchByte(static_cast<unsigned int>(address+i))));
		if(!debugBreakWasSet && towns.CheckDebugBreak()) towns.debugger.ClearStopFlag();
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"address\":"+std::to_string(address)+",\"length\":"+std::to_string(length)+",\"address_space\":\"physical\",\"data_hex\":\""+HexBytes(data)+"\"}";
	}

	std::string WriteMemoryJson(const std::string &request)
	{
		constexpr size_t MAX_MEMORY_BYTES=4096;
		uint64_t address=0;
		std::vector <unsigned char> data;
		if(!JsonUnsignedOrHex(request,"address",address) || !ParseHexBytes(JsonString(request,"data_hex"),data) || MAX_MEMORY_BYTES<data.size() || 0xffffffffULL<address || address+data.size()-1>0xffffffffULL) return "{\"error\":\"invalid_memory_write\"}";
		const auto debugBreakWasSet=towns.CheckDebugBreak();
		for(size_t i=0; i<data.size(); ++i) towns.mem.StoreByte(static_cast<unsigned int>(address+i),data[i]);
		if(!debugBreakWasSet && towns.CheckDebugBreak()) towns.debugger.ClearStopFlag();
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"address\":"+std::to_string(address)+",\"length\":"+std::to_string(data.size())+",\"address_space\":\"physical\"}";
	}

	std::string DisasmJson(const std::string &request) const
	{
		constexpr uint64_t MAX_DISASM_LINES=64;
		uint64_t offset=towns.CPU().state.EIP;
		uint64_t count=(HasJsonKey(request,"count") ? JsonUnsigned(request,"count") : 1);
		if(!JsonUnsignedOrHex(request,"address",offset) && HasJsonKey(request,"address")) return "{\"error\":\"invalid_disasm_address\"}";
		if(0xffffffffULL<offset || 0==count || MAX_DISASM_LINES<count) return "{\"error\":\"invalid_disasm_count\"}";
		const auto &cpu=towns.CPU();
		std::string lines="[";
		for(uint64_t i=0; i<count; ++i)
		{
			i486DXCommon::InstructionAndOperand instOp;
			MemoryAccess::ConstMemoryWindow memWin;
			cpu.DebugFetchInstruction(memWin,instOp,cpu.state.CS(),static_cast<unsigned int>(offset),towns.mem);
			std::vector <unsigned char> bytes;
			for(unsigned int byte=0; byte<instOp.inst.numBytes; ++byte) bytes.push_back(static_cast<unsigned char>(cpu.DebugFetchByte(cpu.state.CS().addressSize,cpu.state.CS(),static_cast<unsigned int>(offset+byte),towns.mem)));
			if(1<lines.size()) lines+=',';
			lines+="{\"cs\":"+std::to_string(cpu.state.CS().value)+",\"address\":"+std::to_string(offset)+",\"bytes\":\""+HexBytes(bytes)+"\",\"text\":\""+
				JsonEscape(cpu.Disassemble(instOp.inst,instOp.op1,instOp.op2,cpu.state.CS(),static_cast<unsigned int>(offset),towns.mem,towns.debugger.GetSymTable(),towns.debugger.GetIOTable()))+"\"}";
			if(0==instOp.inst.numBytes || offset+instOp.inst.numBytes>0xffffffffULL) break;
			offset+=instOp.inst.numBytes;
		}
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"address_space\":\"cs:eip\",\"lines\":"+lines+"]}";
	}

	std::string StackJson(const std::string &request) const
	{
		constexpr uint64_t MAX_STACK_BYTES=4096;
		const auto bytes=(HasJsonKey(request,"length") ? JsonUnsigned(request,"length") : 128);
		if(0==bytes || MAX_STACK_BYTES<bytes) return "{\"error\":\"invalid_stack_length\"}";
		std::string lines="[";
		for(const auto &line : towns.GetStackText(static_cast<unsigned int>(bytes)))
		{
			if(1<lines.size()) lines+=',';
			lines+="\""+JsonEscape(line)+"\"";
		}
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"ss\":"+std::to_string(towns.CPU().state.SS().value)+",\"esp\":"+std::to_string(towns.CPU().state.ESP())+",\"length\":"+std::to_string(bytes)+",\"lines\":"+lines+"]}";
	}

	void RebuildMemoryWatchpoints(void)
	{
		auto &cpu=towns.CPU();
		cpu.state.CSEIPWindow.CleanUp();
		cpu.state.SSESPWindow.CleanUp();
		for(const auto &watch : memoryWatchpoints)
		{
			for(unsigned int i=0; i<watch.length; ++i)
			{
				if("read"==watch.type || "access"==watch.type) i486DebugMemoryAccess::ClearBreakOnMemRead(towns.mem,watch.address+i);
				if("write"==watch.type || "access"==watch.type) i486DebugMemoryAccess::ClearBreakOnMemWrite(towns.mem,watch.address+i);
			}
		}
		for(const auto &watch : memoryWatchpoints)
		{
			for(unsigned int i=0; i<watch.length; ++i)
			{
				if("read"==watch.type || "access"==watch.type) i486DebugMemoryAccess::SetBreakOnMemRead(towns.mem,towns.debugger,watch.address+i,false);
				if("write"==watch.type || "access"==watch.type) i486DebugMemoryAccess::SetBreakOnMemWrite(towns.mem,towns.debugger,watch.address+i,false);
			}
		}
	}

	std::string BreakpointListJson(void) const
	{
		std::string breakpoints="[";
		for(const auto &bp : executionBreakpoints)
		{
			if(1<breakpoints.size()) breakpoints+=',';
			breakpoints+="{\"type\":\"exec\",\"cs\":"+std::to_string(bp.cs)+",\"address\":"+std::to_string(bp.address)+",\"length\":1}";
		}
		for(const auto &watch : memoryWatchpoints)
		{
			if(1<breakpoints.size()) breakpoints+=',';
			breakpoints+="{\"type\":\""+watch.type+"\",\"address\":"+std::to_string(watch.address)+",\"length\":"+std::to_string(watch.length)+",\"address_space\":\"physical\"}";
		}
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"breakpoints\":"+breakpoints+"]}";
	}

	std::string BreakpointJson(const std::string &request)
	{
		const auto action=JsonString(request,"action");
		if("list"==action) return BreakpointListJson();
		if("clear"==action)
		{
			for(const auto &bp : executionBreakpoints)
			{
				i486Debugger::CS_EIP cseip;
				cseip.SEG=bp.cs;
				cseip.OFFSET=bp.address;
				towns.debugger.RemoveBreakPoint(cseip);
			}
			executionBreakpoints.clear();
			for(const auto &watch : memoryWatchpoints)
			{
				for(unsigned int i=0; i<watch.length; ++i)
				{
					if("read"==watch.type || "access"==watch.type) i486DebugMemoryAccess::ClearBreakOnMemRead(towns.mem,watch.address+i);
					if("write"==watch.type || "access"==watch.type) i486DebugMemoryAccess::ClearBreakOnMemWrite(towns.mem,watch.address+i);
				}
			}
			memoryWatchpoints.clear();
			towns.debugger.ClearStopFlag();
			return BreakpointListJson();
		}

		uint64_t address=0,length=1,cs=towns.CPU().state.CS().value;
		const auto type=JsonString(request,"type");
		if(("add"!=action && "remove"!=action) || ("exec"!=type && "read"!=type && "write"!=type && "access"!=type) ||
		   !JsonUnsignedOrHex(request,"address",address) || (HasJsonKey(request,"length") && !JsonUnsignedOrHex(request,"length",length)) ||
		   (HasJsonKey(request,"cs") && !JsonUnsignedOrHex(request,"cs",cs)) ||
		   0xffffffffULL<address || 0xffffULL<cs || 0==length || 4096<length || address+length-1>0xffffffffULL)
		{
			return "{\"error\":\"invalid_breakpoint\"}";
		}
		if("exec"==type && 1!=length) return "{\"error\":\"exec_breakpoint_length_must_be_one\"}";
		if("add"==action)
		{
			if("exec"==type)
			{
				for(const auto &bp : executionBreakpoints)
				{
					if(bp.cs==cs && bp.address==address) return BreakpointListJson();
				}
				i486Debugger::CS_EIP cseip;
				cseip.SEG=static_cast<unsigned int>(cs);
				cseip.OFFSET=static_cast<unsigned int>(address);
				i486Debugger::BreakPointInfo info;
				towns.debugger.AddBreakPoint(cseip,info);
				executionBreakpoints.push_back({static_cast<unsigned int>(cs),static_cast<unsigned int>(address)});
			}
			else
			{
				for(const auto &watch : memoryWatchpoints)
				{
					if(watch.type==type && watch.address==address && watch.length==length) return BreakpointListJson();
				}
				memoryWatchpoints.push_back({type,static_cast<unsigned int>(address),static_cast<unsigned int>(length)});
				RebuildMemoryWatchpoints();
			}
		}
		else if("exec"==type)
		{
			auto found=std::find_if(executionBreakpoints.begin(),executionBreakpoints.end(),[&](const ExecutionBreakpoint &bp)
			{
				return bp.cs==cs && bp.address==address;
			});
			if(executionBreakpoints.end()==found) return "{\"error\":\"breakpoint_not_found\"}";
			i486Debugger::CS_EIP cseip;
			cseip.SEG=static_cast<unsigned int>(cs);
			cseip.OFFSET=static_cast<unsigned int>(address);
			towns.debugger.RemoveBreakPoint(cseip);
			executionBreakpoints.erase(found);
		}
		else
		{
			auto found=std::find_if(memoryWatchpoints.begin(),memoryWatchpoints.end(),[&](const MemoryWatchpoint &watch)
			{
				return watch.type==type && watch.address==address && watch.length==length;
			});
			if(memoryWatchpoints.end()==found) return "{\"error\":\"breakpoint_not_found\"}";
			for(unsigned int i=0; i<found->length; ++i)
			{
				if("read"==found->type || "access"==found->type) i486DebugMemoryAccess::ClearBreakOnMemRead(towns.mem,found->address+i);
				if("write"==found->type || "access"==found->type) i486DebugMemoryAccess::ClearBreakOnMemWrite(towns.mem,found->address+i);
			}
			memoryWatchpoints.erase(found);
			RebuildMemoryWatchpoints();
		}
		towns.debugger.ClearStopFlag();
		return BreakpointListJson();
	}

	std::string BreakStopJson(void) const
	{
		std::string message=towns.debugger.externalBreakReason;
		if(message.empty()) message="Execution breakpoint reached.";
		const auto &state=towns.CPU().state;
		return ",\"break\":{\"cs\":"+std::to_string(state.CS().value)+",\"eip\":"+std::to_string(state.EIP)+",\"message\":\""+JsonEscape(message)+"\"}";
	}

	std::string StepJson(void)
	{
		running=false;
		if(towns.CheckDebugBreak()) towns.debugger.ClearStopFlag();
		const auto townsTimeBefore=towns.state.townsTime;
		towns.RunOneInstruction();
		towns.pic.ProcessIRQ(towns.CPU(),towns.mem);
		const auto registers=RegistersJson();
		return registers.substr(0,registers.size()-1)+",\"instructions\":1,\"towns_time_before\":"+std::to_string(townsTimeBefore)+",\"towns_time\":"+std::to_string(towns.state.townsTime)+"}";
	}

	std::string ConsoleLinesJson(bool clear) const
	{
		const auto text=towns.ConsoleRead(clear);
		std::string lines="[";
		for(size_t begin=0; begin<text.size(); )
		{
			auto end=text.find('\n',begin);
			if(std::string::npos==end) end=text.size();
			if(end>begin || end<text.size())
			{
				if(1<lines.size()) lines+=',';
				lines+="\""+JsonEscape(text.substr(begin,end-begin))+"\"";
			}
			if(end==text.size()) break;
			begin=end+1;
		}
		return lines+"]";
	}

	std::string ConsoleReadJson(const std::string &request) const
	{
		const auto clear=(HasJsonKey(request,"clear") && JsonBoolean(request,"clear"));
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"lines\":"+ConsoleLinesJson(clear)+"}";
	}

	void Start(bool startWindow=true)
	{
		if(started) return;
		windowServicesOnVmThread=startWindow;
		profileStart=std::chrono::steady_clock::now();
	outsideWorld.Start();
	if(""!=towns.var.startUpStateFName)
	{
		towns.LoadState(towns.var.startUpStateFName);
	}

	towns.sound.SetOutsideWorld(&sound);
	towns.sound.SetCDROMPointer(&towns.cdrom);
	towns.sound.SetSCSIPointer(&towns.scsi);
	sound.Start();
	if(startWindow) window.Start();
		towns.ForceRender(render,outsideWorld,window);
		started=true;
	}

	void Stop(bool stopWindow=true)
	{
		if(!started) return;
		if(stopWindow) window.Stop();
		sound.Stop();
		outsideWorld.Stop();
		started=false;
	}

	std::string StatusJson(void) const
	{
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"model\":\""+TownsTypeToStr(towns.townsType)+"\",\"window\":\""+
			(dynamic_cast <ArclNativeWindow *>(&window)!=nullptr ? (outsideWorld.closeWindow ? "closed" : "open") : "headless")+"\",\"layers\":"+LayersJson()+"}";
	}

	void RunPeriodicServices(void)
	{
		const auto periodicStart=std::chrono::steady_clock::now();
		const auto soundStart=periodicStart;
		towns.ProcessSound(&outsideWorld);
		soundNanoseconds+=std::chrono::duration_cast <std::chrono::nanoseconds>(std::chrono::steady_clock::now()-soundStart).count();
		towns.cdrom.UpdateCDDAState(towns.state.townsTime);
		outsideWorld.ProcessAppSpecific(towns);
		if(towns.state.nextDevicePollingTime<towns.state.townsTime)
		{
			const auto deviceStart=std::chrono::steady_clock::now();
			outsideWorld.UpdateStatusBarInfo(towns);
			window.Communicate(&outsideWorld);
			outsideWorld.DevicePolling(towns);
			sound.Polling();
			towns.rex3586.Polling();
			towns.state.nextDevicePollingTime=towns.state.townsTime+FMTownsCommon::DEVICE_POLLING_INTERVAL;
			deviceNanoseconds+=std::chrono::duration_cast <std::chrono::nanoseconds>(std::chrono::steady_clock::now()-deviceStart).count();
		}
		towns.eventLog.Interval(towns);
		if(windowServicesOnVmThread) window.Interval();
		ApplyGuiInput();
		periodicNanoseconds+=std::chrono::duration_cast <std::chrono::nanoseconds>(std::chrono::steady_clock::now()-periodicStart).count();
	}

	std::string RunFrames(uint64_t frames,const std::string &textMatch="",bool ignoreVmPause=false,bool noRender=false)
	{
		// Rewind history is opt-in through arcl_rewind(checkpoint).  Saving a
		// complete VM state is never an implicit side effect of running.
	running=false;
	if(towns.CheckDebugBreak()) towns.debugger.ClearStopFlag();
	const auto previousIgnoreVmPause=towns.var.ignoreVMPauseRequest;
	const auto ignoredVmPausesBefore=towns.var.ignoredVMPauseRequests;
	towns.var.ignoreVMPauseRequest=ignoreVmPause;
	ArclVmRunner runner;
	ArclVmRunner::Result result;
	result.frame=ArclVmRunner::FrameFromTownsTime(towns.state.townsTime);
	result.townsTime=towns.state.townsTime;
	uint64_t framesUsed=0;
	std::string stopReason="frames";
	for(uint64_t i=0; i<frames; ++i)
	{
		const auto frameEnd=ArclVmRunner::NextFrameBoundary(towns.state.townsTime);
		while(towns.state.townsTime<frameEnd)
		{
			const auto sliceEnd=std::min<uint64_t>(frameEnd,towns.state.townsTime+1000000ULL);
			const auto vmRunStart=std::chrono::steady_clock::now();
			result=runner.RunToTime(towns,sliceEnd);
			vmRunNanoseconds+=std::chrono::duration_cast <std::chrono::nanoseconds>(std::chrono::steady_clock::now()-vmRunStart).count();
			++vmSlices;
			RunPeriodicServices();

			if(!textMatch.empty() && ConsoleMatches(textMatch))
			{
				stopReason="text_match";
				break;
			}
			if(ArclVmRunner::STOP_NONE!=result.stopReason || true==outsideWorld.closeWindow || true==towns.var.powerOff)
			{
				stopReason=(ArclVmRunner::STOP_DEBUG_BREAK==result.stopReason ? "breakpoint" : "stop");
				break;
			}
		}
		if("frames"!=stopReason) break;

		++framesUsed;
		if(noRender)
		{
			// Diagnostic runs may inspect CRTC state before a possibly-invalid
			// guest display geometry is composed into a host framebuffer.
		}
		else if(nullptr!=dynamic_cast <ArclNativeWindow *>(&window))
		{
			// The VM only snapshots VRAM.  The GUI thread turns it into RGBA,
			// so expensive composition and OpenGL work cannot slow the CPU.
			const auto publishStart=std::chrono::steady_clock::now();
			window.SendNewImage(towns,outsideWorld.ImageNeedsFlip());
			publishNanoseconds+=std::chrono::duration_cast <std::chrono::nanoseconds>(std::chrono::steady_clock::now()-publishStart).count();
		}
		else
		{
			towns.ForceRender(render,outsideWorld,window);
		}
	}

	towns.var.ignoreVMPauseRequest=previousIgnoreVmPause;
	const auto ignoredVmPauses=towns.var.ignoredVMPauseRequests-ignoredVmPausesBefore;
	return "{\"machine\":\"towns\",\"state\":\"paused\",\"running\":false,\"frame\":"+
		std::to_string(result.frame)+",\"frames_used\":"+std::to_string(framesUsed)+
		",\"towns_time\":"+std::to_string(result.townsTime)+",\"stop_reason\":\""+stopReason+"\",\"ignored_vm_pauses\":"+std::to_string(ignoredVmPauses)+
		("breakpoint"==stopReason ? BreakStopJson() : "")+"}";
	}

	bool ConsoleMatches(const std::string &textMatch)
	{
		const auto generation=towns.ConsoleGeneration();
		if(generation!=textMatchConsoleGeneration)
		{
			textMatchConsoleGeneration=generation;
			textMatchConsole=towns.ConsoleRead(false);
		}
		return std::string::npos!=textMatchConsole.find(textMatch);
	}

	std::string PauseJson(void)
	{
		running=false;
		return StatusJson();
	}

	std::string ResumeJson(void)
	{
		running=true;
		nextContinuousDeadline=std::chrono::steady_clock::now();
		return StatusJson();
	}

	bool IsRunning(void) const
	{
		return running;
	}

	std::string ProfileJson(void) const
	{
		const auto wallNanoseconds=std::chrono::duration_cast <std::chrono::nanoseconds>(std::chrono::steady_clock::now()-profileStart).count();
		auto milliseconds=[](uint64_t nanoseconds)
		{
			return std::to_string(nanoseconds/1000000ULL);
		};
		std::string gui="null";
		if(auto *nativeWindow=dynamic_cast <ArclNativeWindow *>(&window); nullptr!=nativeWindow)
		{
			const auto timing=nativeWindow->CopyTiming();
			gui="{\"intervals\":"+std::to_string(timing.intervals)+",\"interval_ms\":"+milliseconds(timing.intervalNanoseconds)+",\"renders\":"+std::to_string(timing.renders)+",\"render_ms\":"+milliseconds(timing.renderNanoseconds)+"}";
		}
		return "{\"wall_ms\":"+milliseconds(wallNanoseconds)+",\"vm_run_ms\":"+milliseconds(vmRunNanoseconds)+",\"periodic_ms\":"+milliseconds(periodicNanoseconds)+",\"sound_ms\":"+milliseconds(soundNanoseconds)+",\"device_ms\":"+milliseconds(deviceNanoseconds)+",\"publish_ms\":"+milliseconds(publishNanoseconds)+",\"vm_slices\":"+std::to_string(vmSlices)+",\"gui\":"+gui+"}";
	}

	void AdvanceContinuous(void)
	{
		const auto result=RunFrames(1);
		running=(std::string::npos!=result.find("\"stop_reason\":\"frames\"") && !outsideWorld.closeWindow && !towns.var.powerOff);
		if(running)
		{
			const auto frameDuration=std::chrono::nanoseconds(100000000000LL/(60*speedPercent));
			const auto now=std::chrono::steady_clock::now();
			nextContinuousDeadline+=frameDuration;
			if(now<nextContinuousDeadline)
			{
				std::this_thread::sleep_until(nextContinuousDeadline);
			}
			else
			{
				// Do not add a fresh full frame wait after a late VM iteration.
				nextContinuousDeadline=now;
			}
		}
	}

	std::string ScreenshotJson(const std::string &request) const
	{
		auto *capture=dynamic_cast <ArclCaptureHost::CaptureWindow *>(&window);
		ArclCaptureHost::CaptureWindow::FrameInfo frameInfo;
		if(nullptr==capture || !capture->CopyLatestFrameInfo(frameInfo)) return "{\"error\":\"capture_busy_or_empty\"}";
		// Do not copy an arbitrary renderer buffer into the RPC path.  A malformed
		// CRTC state can transiently describe a very large image while an EGB call
		// is in progress; preserve the VM and report that state instead.
		constexpr uint64_t MAX_SOURCE_PIXELS=32ULL*1024ULL*1024ULL;
		if(0==frameInfo.width || 0==frameInfo.height ||
		   MAX_SOURCE_PIXELS<static_cast<uint64_t>(frameInfo.width)*frameInfo.height ||
		   static_cast<uint64_t>(frameInfo.rgbaBytes)!=static_cast<uint64_t>(frameInfo.width)*frameInfo.height*4)
		{
			return "{\"error\":\"invalid_capture_frame\",\"source_width\":"+std::to_string(frameInfo.width)+
				",\"source_height\":"+std::to_string(frameInfo.height)+",\"rgba_bytes\":"+std::to_string(frameInfo.rgbaBytes)+"}";
		}
		ArclCaptureHost::CaptureWindow::Frame frame;
		if(!capture->CopyLatestFrame(frame)) return "{\"error\":\"capture_busy\"}";

		const auto x=JsonUnsigned(request,"x");
		const auto y=JsonUnsigned(request,"y");
		const auto width=(HasJsonKey(request,"w") ? JsonUnsigned(request,"w") : frame.width);
		const auto height=(HasJsonKey(request,"h") ? JsonUnsigned(request,"h") : frame.height);
		const auto scale=(HasJsonKey(request,"scale") ? JsonUnsigned(request,"scale") : 1);
		const auto grid=(HasJsonKey(request,"grid") && JsonBoolean(request,"grid"));
		const auto gridSize=(HasJsonKey(request,"grid_size") ? JsonUnsigned(request,"grid_size") : 8);
		const auto relativePath=JsonString(request,"path");
		// A managed path is normally requested to avoid returning an expensive
		// inline PNG.  Preserve inline-by-default only when there is no path.
		const auto inlineImage=(HasJsonKey(request,"inline") ? JsonBoolean(request,"inline") : relativePath.empty());
		constexpr uint64_t MAX_OUTPUT_PIXELS=16ULL*1024ULL*1024ULL;
		if(0==width || 0==height || 0==scale ||
			(grid && 0==gridSize) ||
		   x>=frame.width || y>=frame.height ||
		   width>frame.width-x || height>frame.height-y ||
		   width>UINT64_MAX/scale || height>UINT64_MAX/scale ||
		   width*scale>UINT64_MAX/(height*scale) ||
		   MAX_OUTPUT_PIXELS<width*scale*height*scale)
		{
			return "{\"error\":\"invalid_screenshot_region\"}";
		}

		const auto outputWidth=static_cast<unsigned int>(width*scale);
		const auto outputHeight=static_cast<unsigned int>(height*scale);
		std::vector <unsigned char> output(outputWidth*outputHeight*4);
		for(uint64_t outY=0; outY<outputHeight; ++outY)
		{
			for(uint64_t outX=0; outX<outputWidth; ++outX)
			{
				const auto source=((y+outY/scale)*frame.width+x+outX/scale)*4;
				const auto destination=(outY*outputWidth+outX)*4;
				for(unsigned int c=0; c<4; ++c) output[destination+c]=frame.rgba[source+c];
			}
		}
		if(grid)
		{
			for(uint64_t outY=0; outY<outputHeight; ++outY)
			{
				for(uint64_t outX=0; outX<outputWidth; ++outX)
				{
					if(0==(x+outX/scale)%gridSize || 0==(y+outY/scale)%gridSize)
					{
						const auto pixel=(outY*outputWidth+outX)*4;
						output[pixel]=255;
						output[pixel+1]=0;
						output[pixel+2]=255;
						output[pixel+3]=255;
					}
				}
			}
		}
		YsMemoryPngEncoder encoder;
		// The bundled encoder's repetition-reduction pass can take an unbounded
		// amount of time on display buffers with long uniform runs.  Screenshots
		// favor predictable RPC latency over PNG compression ratio.
		encoder.SetDontCompress(YSTRUE);
		if(YSOK!=encoder.Encode(outputWidth,outputHeight,8,6,output.data())) return "{\"error\":\"png_encode_failed\"}";
		if(!relativePath.empty())
		{
			if(!IsSafeRelativePath(relativePath)) return "{\"error\":\"invalid_screenshot_path\"}";
			const auto path=options.outputDirectory/std::filesystem::path(relativePath);
			std::filesystem::create_directories(path.parent_path());
			std::ofstream outputFile(path,std::ios::binary);
			outputFile.write(reinterpret_cast<const char *>(encoder.GetByteData()),static_cast<std::streamsize>(encoder.GetLength()));
			if(!outputFile.good()) return "{\"error\":\"screenshot_io_failed\"}";
		}
		if(!inlineImage && relativePath.empty()) return "{\"error\":\"screenshot_requires_path_or_inline\"}";
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"source_width\":"+std::to_string(frame.width)+
			",\"source_height\":"+std::to_string(frame.height)+",\"x\":"+std::to_string(x)+",\"y\":"+std::to_string(y)+
			",\"w\":"+std::to_string(width)+",\"h\":"+std::to_string(height)+",\"scale\":"+std::to_string(scale)+
			",\"grid\":"+(grid ? "true" : "false")+",\"width\":"+std::to_string(outputWidth)+",\"height\":"+std::to_string(outputHeight)+",\"mime_type\":\"image/png\""+
			(relativePath.empty() ? "" : ",\"managed_path\":\""+relativePath+"\"")+
			(inlineImage ? ",\"data_base64\":\""+Base64(encoder.GetByteData(),static_cast<size_t>(encoder.GetLength()))+"\"" : "")+"}";
	}

	std::string KeyJson(const std::string &key,const std::string &action)
	{
		auto keyCode=TownsStrToKeyCode(key);
		if(TOWNS_JISKEY_NULL==keyCode)
		{
			keyCode=TownsStrToKeyCode("TOWNS_JISKEY_"+key);
		}
		if(TOWNS_JISKEY_NULL==keyCode || ("press"!=action && "release"!=action && "tap"!=action)) return "{\"error\":\"invalid_key_or_action\"}";
		if("press"==action && heldKeys.insert(keyCode).second && guiKeys.end()==guiKeys.find(keyCode)) towns.keyboard.PushFifo(TOWNS_KEYFLAG_JIS_PRESS,keyCode);
		if("release"==action && heldKeys.erase(keyCode) && guiKeys.end()==guiKeys.find(keyCode)) towns.keyboard.PushFifo(TOWNS_KEYFLAG_JIS_RELEASE,keyCode);
		if("tap"==action)
		{
			towns.keyboard.PushFifo(TOWNS_KEYFLAG_JIS_PRESS,keyCode);
			towns.keyboard.PushFifo(TOWNS_KEYFLAG_JIS_RELEASE,keyCode);
		}
		return InputStateJson();
	}

	std::string InputStateJson(void) const
	{
		std::string keys;
		for(auto key : heldKeys)
		{
			if(!keys.empty()) keys+=",";
			keys+="\""+TownsKeyCodeToStr(key)+"\"";
		}
		std::string pads;
		for(unsigned int port=0; port<2; ++port)
		{
			const auto &pad=joypads[port];
			if(!pads.empty()) pads+=",";
			pads+="{\"port\":"+std::to_string(port)+",\"a\":"+(pad.a ? "true" : "false")+",\"b\":"+(pad.b ? "true" : "false")+",\"left\":"+(pad.left ? "true" : "false")+",\"right\":"+(pad.right ? "true" : "false")+",\"up\":"+(pad.up ? "true" : "false")+",\"down\":"+(pad.down ? "true" : "false")+",\"run\":"+(pad.run ? "true" : "false")+",\"pause\":"+(pad.pause ? "true" : "false")+",\"zoom\":"+(pad.zoom ? "true" : "false")+"}";
		}
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"keys\":["+keys+"],\"mouse\":{\"port\":"+std::to_string(mousePort)+",\"dx\":"+std::to_string(mouseDx)+",\"dy\":"+std::to_string(mouseDy)+",\"left\":"+(mouseLeft ? "true" : "false")+",\"right\":"+(mouseRight ? "true" : "false")+"},\"joypads\":["+pads+"]}";
	}

	std::string ClearInputJson(void)
	{
		for(auto key : heldKeys) if(guiKeys.end()==guiKeys.find(key)) towns.keyboard.PushFifo(TOWNS_KEYFLAG_JIS_RELEASE,key);
		heldKeys.clear();
		mouseLeft=false;
		mouseRight=false;
		mouseDx=0;
		mouseDy=0;
		towns.SetMouseButtonState(false,false);
		towns.SetMouseMotion(0,0,0);
		towns.SetMouseMotion(1,0,0);
		for(unsigned int port=0; port<2; ++port)
		{
			joypads[port]=JoypadInput();
			ApplyGamePad(port);
		}
		return InputStateJson();
	}

	std::string MouseJson(const std::string &request)
	{
		const auto dx=JsonInteger(request,"dx");
		const auto dy=JsonInteger(request,"dy");
		if(dx<-32767 || 32767<dx || dy<-32767 || 32767<dy) return "{\"error\":\"invalid_mouse_motion\"}";
		if(0>mousePort)
		{
			for(int port=0; port<2; ++port)
			{
				if(TownsGamePort::MOUSE==towns.gameport.state.ports[port].device)
				{
					mousePort=port;
					break;
				}
			}
		}
		if(0>mousePort) return "{\"error\":\"unsupported_mouse_device\"}";
		if(HasJsonKey(request,"left")) mouseLeft=JsonBoolean(request,"left");
		if(HasJsonKey(request,"right")) mouseRight=JsonBoolean(request,"right");
		mouseDx=dx;
		mouseDy=dy;
		towns.SetMouseButtonState(mouseLeft,mouseRight);
		towns.SetMouseMotion(mousePort,dx,dy);
		return InputStateJson();
	}

	std::string JoypadJson(const std::string &request)
	{
		const auto port=JsonUnsigned(request,"port");
		if(1<port) return "{\"error\":\"invalid_joypad_port\"}";
		const auto device=towns.gameport.state.ports[port].device;
		if(TownsGamePort::GAMEPAD!=device && TownsGamePort::GAMEPAD_6BTN!=device && TownsGamePort::MARTYPAD!=device) return "{\"error\":\"unsupported_joypad_device\"}";
		auto &pad=joypads[port];
		if(HasJsonKey(request,"a")) pad.a=JsonBoolean(request,"a");
		if(HasJsonKey(request,"b")) pad.b=JsonBoolean(request,"b");
		if(HasJsonKey(request,"left")) pad.left=JsonBoolean(request,"left");
		if(HasJsonKey(request,"right")) pad.right=JsonBoolean(request,"right");
		if(HasJsonKey(request,"up")) pad.up=JsonBoolean(request,"up");
		if(HasJsonKey(request,"down")) pad.down=JsonBoolean(request,"down");
		if(HasJsonKey(request,"run")) pad.run=JsonBoolean(request,"run");
		if(HasJsonKey(request,"pause")) pad.pause=JsonBoolean(request,"pause");
		if(HasJsonKey(request,"zoom")) pad.zoom=JsonBoolean(request,"zoom");
		ApplyGamePad(port);
		return InputStateJson();
	}

	std::string TypeJson(const std::string &text)
	{
		// The TOWNS keyboard FIFO is a fixed 32-byte hardware buffer (TownsKeyboard::FIFO_BUF_LEN).
		// A plain key costs 4 bytes (press+release); a shifted or ctrl'd key costs up to 12 bytes
		// (one extra press+release pair per modifier). PushFifo() silently drops pushes once the
		// FIFO is full, and arcl_type queues the whole string synchronously without advancing any
		// VM frames in between, so the guest never gets a chance to drain it mid-string. Stop
		// queuing before we would overflow, rather than silently losing trailing characters.
		size_t queued=0;
		for(auto c : text)
		{
			if(0x7f<static_cast<unsigned char>(c)) return "{\"error\":\"unsupported_character\"}";
			unsigned char keyCode[2];
			if(0==TownsKeyboard::TranslateChar(keyCode,static_cast<unsigned char>(c))) return "{\"error\":\"unsupported_character\"}";

			unsigned int neededBytes=4;
			if(0!=(keyCode[0]&TOWNS_KEYFLAG_SHIFT)) neededBytes+=4;
			if(0!=(keyCode[0]&TOWNS_KEYFLAG_CTRL)) neededBytes+=4;
			if(TownsKeyboard::FIFO_BUF_LEN<towns.keyboard.nFifoFilled+neededBytes) break;

			towns.keyboard.TypeToFifo(keyCode);
			++queued;
		}
		std::string result="{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"characters_queued\":"+std::to_string(queued);
		if(queued<text.size())
		{
			result+=",\"characters_dropped\":"+std::to_string(text.size()-queued)+",\"warning\":\"keyboard_fifo_full\"";
		}
		return result+"}";
	}

	std::string CommandJson(const std::string &text,uint64_t frames,const std::string &request)
	{
		if(text.empty()) return "{\"error\":\"empty_command\"}";
		const auto typed=TypeJson(text+"\r");
		if(std::string::npos!=typed.find("\"error\"")) return typed;
		// FreeTOWNSOS consumes this queue through its documented VM host interface.
		// Keyboard injection above remains the portable fallback for other guests.
		towns.var.consoleCmd.push_back(text+"\r");
		const auto result=RunFrames((0<frames ? frames : 60),JsonString(request,"text_match"));
		return result.substr(0,result.size()-1)+",\"command\":\""+JsonEscape(text)+"\",\"lines\":"+ConsoleLinesJson(false)+"}";
	}

	std::string HostDirJson(void) const
	{
		if(options.allowRoots.empty()) return "{\"error\":\"no_allow_root\"}";
		std::string directories="[";
		for(unsigned int i=0; i<TownsVnDrv::MAX_NUM_SHARED_DIRECTORIES; ++i)
		{
			const auto &directory=towns.vndrv.sharedDir[i];
			if(!directory.linked || directory.hostPath.empty()) continue;
			bool allowed=false;
			for(const auto &root : options.allowRoots)
			{
				if(IsPathWithin(directory.hostPath,root))
				{
					allowed=true;
					break;
				}
			}
			if(!allowed) continue;
			if(1<directories.size()) directories+=',';
			directories+="{\"index\":"+std::to_string(i)+",\"path\":\""+JsonEscape(directory.hostPath)+"\"}";
		}
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"directories\":"+directories+"]}";
	}

	std::string ResolveAllowedPath(const std::string &text) const
	{
		if(text.empty() || options.allowRoots.empty()) return "";
		const std::filesystem::path requested(text);
		for(const auto &root : options.allowRoots)
		{
			const auto candidate=(requested.is_absolute() ? requested : root/requested);
			if(IsPathWithin(candidate,root) && std::filesystem::is_regular_file(candidate))
			{
				return std::filesystem::weakly_canonical(candidate).string();
			}
		}
		return "";
	}

	std::string MountJson(const std::string &request)
	{
		running=false;
		const auto kind=JsonString(request,"kind");
		const auto action=JsonString(request,"action");
		if("floppy"!=kind && "cd"!=kind) return "{\"error\":\"unsupported_media_kind\"}";
		if("eject"==action)
		{
			if("floppy"==kind)
			{
				const auto drive=JsonUnsigned(request,"drive");
				if(3<drive) return "{\"error\":\"invalid_floppy_drive\"}";
				towns.fdc.Eject(static_cast<unsigned int>(drive));
			}
			else
			{
				towns.cdrom.Eject();
			}
		}
		else if("mount"==action)
		{
			const auto path=ResolveAllowedPath(JsonString(request,"path"));
			if(path.empty()) return "{\"error\":\"media_path_not_allowed\"}";
			bool mounted=false;
			if("floppy"==kind)
			{
				const auto drive=JsonUnsigned(request,"drive");
				if(3<drive) return "{\"error\":\"invalid_floppy_drive\"}";
				mounted=towns.fdc.LoadD77orRDDorRAW(static_cast<unsigned int>(drive),path.c_str(),towns.state.townsTime,false);
			}
			else
			{
				mounted=(DiscImage::ERROR_NOERROR==towns.cdrom.LoadDiscImage(path));
			}
			if(!mounted) return "{\"error\":\"media_mount_failed\"}";
		}
		else return "{\"error\":\"invalid_mount_action\"}";
		return "{\"machine\":\"towns\",\"state\":\"paused\",\"running\":false,\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"kind\":\""+kind+"\",\"action\":\""+action+"\"}";
	}

	std::string AudioRecordJson(uint64_t frames,const std::string &request)
	{
		constexpr uint64_t MAX_RECORD_FRAMES=3600;
		if(0==frames || MAX_RECORD_FRAMES<frames) return "{\"error\":\"invalid_audio_record_frames\"}";
		const auto relativePath=JsonString(request,"path");
		if(!relativePath.empty() && !IsSafeRelativePath(relativePath)) return "{\"error\":\"invalid_audio_path\"}";
		towns.sound.StartRecording();
		const auto runResult=RunFrames(frames);
		towns.sound.EndRecording();
		const auto &wave=towns.sound.FMPCMrecording;
		uint64_t peak=0;
		long double sumSquares=0;
		for(size_t i=0; i+1<wave.size(); i+=2)
		{
			const auto sample=static_cast<int16_t>(unsigned(wave[i])|(unsigned(wave[i+1])<<8));
			const auto magnitude=static_cast<uint64_t>(sample<0 ? -static_cast<int32_t>(sample) : sample);
			peak=std::max(peak,magnitude);
			sumSquares+=static_cast<long double>(sample)*sample;
		}
		const auto samples=wave.size()/4;
		const auto rms=(wave.empty() ? 0.0 : std::sqrt(static_cast<double>(sumSquares/(wave.size()/2))));
		if(!relativePath.empty())
		{
			const auto path=options.outputDirectory/std::filesystem::path(relativePath);
			std::filesystem::create_directories(path.parent_path());
			if(!towns.sound.SaveRecording(path.string())) return "{\"error\":\"audio_write_failed\"}";
		}
		return runResult.substr(0,runResult.size()-1)+",\"sample_rate\":44100,\"channels\":2,\"samples\":"+
			std::to_string(samples)+",\"peak\":"+std::to_string(peak)+",\"rms\":"+std::to_string(rms)+",\"silent\":"+(0==peak ? "true" : "false")+
			(relativePath.empty() ? "" : ",\"managed_path\":\""+relativePath+"\"")+"}";
	}

	std::string StateJson(const std::string &slot,bool load)
	{
		running=false;
		if(slot.empty() || 64<slot.size() || slot.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-")!=std::string::npos) return "{\"error\":\"invalid_slot\"}";
		std::filesystem::create_directories(options.stateDirectory);
		const auto path=(options.stateDirectory/(slot+".tstate")).string();
		const auto ok=(load ? towns.LoadState(path) : towns.SaveState(path));
		if(!ok) return "{\"error\":\"state_io_failed\"}";
		if(load) towns.ForceRender(render,outsideWorld,window);
		return "{\"machine\":\"towns\",\"state\":\"paused\",\"running\":false,\"frame\":"+std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"slot\":\""+slot+"\"}";
	}

	uint64_t SnapshotBytes(void) const
	{
		uint64_t bytes=0;
		for(const auto &snapshot : snapshots) bytes+=snapshot.second.state.size();
		return bytes;
	}

	uint64_t RewindBytes(void) const
	{
		uint64_t bytes=0;
		for(const auto &snapshot : rewindHistory) bytes+=snapshot.state.size();
		return bytes;
	}

	Snapshot CaptureSnapshot(void)
	{
		Snapshot snapshot;
		snapshot.state=towns.SaveStateMem();
		snapshot.frame=ArclVmRunner::FrameFromTownsTime(towns.state.townsTime);
		snapshot.serial=++nextSnapshotSerial;
		snapshot.heldKeys=heldKeys;
		snapshot.mouseLeft=mouseLeft;
		snapshot.mouseRight=mouseRight;
		snapshot.mousePort=mousePort;
		snapshot.mouseDx=mouseDx;
		snapshot.mouseDy=mouseDy;
		for(unsigned int port=0; port<2; ++port) snapshot.joypads[port]=joypads[port];
		return snapshot;
	}

	bool RecordRewindCheckpoint(std::vector <uint64_t> *evicted=nullptr)
	{
		constexpr uint64_t MAX_REWIND_BYTES=256ULL*1024ULL*1024ULL;
		constexpr size_t MAX_REWIND_SNAPSHOTS=8;
		auto snapshot=CaptureSnapshot();
		if(snapshot.state.empty() || MAX_REWIND_BYTES<snapshot.state.size()) return false;
		while(!rewindHistory.empty() && (MAX_REWIND_SNAPSHOTS<=rewindHistory.size() || MAX_REWIND_BYTES-RewindBytes()<snapshot.state.size()))
		{
			if(nullptr!=evicted) evicted->push_back(rewindHistory.front().frame);
			rewindHistory.pop_front();
		}
		if(MAX_REWIND_BYTES-RewindBytes()<snapshot.state.size()) return false;
		rewindHistory.push_back(std::move(snapshot));
		return true;
	}

	std::string RewindSummaryJson(const std::vector <uint64_t> &evicted={}) const
	{
		constexpr uint64_t MAX_REWIND_BYTES=256ULL*1024ULL*1024ULL;
		constexpr size_t MAX_REWIND_SNAPSHOTS=8;
		std::string history="[",evictedFrames="[";
		for(const auto &snapshot : rewindHistory)
		{
			if(1<history.size()) history+=",";
			history+="{\"frame\":"+std::to_string(snapshot.frame)+",\"bytes\":"+std::to_string(snapshot.state.size())+"}";
		}
		for(const auto frame : evicted)
		{
			if(1<evictedFrames.size()) evictedFrames+=",";
			evictedFrames+=std::to_string(frame);
		}
		return "{\"machine\":\"towns\",\"state\":\"paused\",\"running\":false,\"frame\":"+std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+
			",\"history\":"+history+"],\"used_bytes\":"+std::to_string(RewindBytes())+",\"max_bytes\":"+std::to_string(MAX_REWIND_BYTES)+",\"max_slots\":"+std::to_string(MAX_REWIND_SNAPSHOTS)+",\"evicted_frames\":"+evictedFrames+"]}";
	}

	std::string RewindJson(const std::string &request)
	{
		running=false;
		const auto action=JsonString(request,"action");
		if("list"==action) return RewindSummaryJson();
		if("clear"==action)
		{
			rewindHistory.clear();
			return RewindSummaryJson();
		}
		if("checkpoint"==action)
		{
			std::vector <uint64_t> evicted;
			if(!RecordRewindCheckpoint(&evicted)) return "{\"error\":\"rewind_capacity_exceeded\"}";
			return RewindSummaryJson(evicted);
		}
		const auto steps=(HasJsonKey(request,"steps") ? JsonUnsigned(request,"steps") : 1);
		if("rewind"!=action || 0==steps || rewindHistory.size()<steps) return "{\"error\":\"invalid_rewind_request\"}";
		const auto target=rewindHistory.size()-static_cast<size_t>(steps);
		if(!towns.LoadStateMem(rewindHistory[target].state)) return "{\"error\":\"rewind_load_failed\"}";
		RestoreSnapshotInput(rewindHistory[target]);
		towns.debugger.ClearStopFlag();
		towns.ForceRender(render,outsideWorld,window);
		const auto restoredFrame=rewindHistory[target].frame;
		rewindHistory.erase(rewindHistory.begin()+static_cast<std::ptrdiff_t>(target),rewindHistory.end());
		const auto summary=RewindSummaryJson();
		return summary.substr(0,summary.size()-1)+",\"restored_frame\":"+std::to_string(restoredFrame)+"}";
	}

	std::string SpeedJson(const std::string &request)
	{
		if(HasJsonKey(request,"percent"))
		{
			const auto percent=JsonUnsigned(request,"percent");
			if(0==percent || 1000<percent) return "{\"error\":\"invalid_speed_percent\"}";
			speedPercent=static_cast<unsigned int>(percent);
			nextContinuousDeadline=std::chrono::steady_clock::now();
		}
		return "{\"machine\":\"towns\",\"state\":\""+std::string(running ? "running" : "paused")+"\",\"running\":"+(running ? "true" : "false")+",\"frame\":"+std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"percent\":"+std::to_string(speedPercent)+"}";
	}

	std::string SnapshotSummaryJson(const std::vector <std::string> &evicted={}) const
	{
		constexpr uint64_t MAX_SNAPSHOT_BYTES=256ULL*1024ULL*1024ULL;
		constexpr size_t MAX_SNAPSHOTS=8;
		std::string slots="[";
		for(const auto &snapshot : snapshots)
		{
			if(1<slots.size()) slots+=',';
			slots+="{\"name\":\""+JsonEscape(snapshot.first)+"\",\"frame\":"+std::to_string(snapshot.second.frame)+",\"bytes\":"+std::to_string(snapshot.second.state.size())+"}";
		}
		std::string evictedJson="[";
		for(const auto &name : evicted)
		{
			if(1<evictedJson.size()) evictedJson+=',';
			evictedJson+="\""+JsonEscape(name)+"\"";
		}
		return "{\"machine\":\"towns\",\"state\":\"paused\",\"running\":false,\"frame\":"+
			std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"slots\":"+slots+"],\"used_bytes\":"+std::to_string(SnapshotBytes())+
			",\"max_bytes\":"+std::to_string(MAX_SNAPSHOT_BYTES)+",\"max_slots\":"+std::to_string(MAX_SNAPSHOTS)+",\"evicted\":"+evictedJson+"]}";
	}

	void RestoreSnapshotInput(const Snapshot &snapshot)
	{
		heldKeys=snapshot.heldKeys;
		mouseLeft=snapshot.mouseLeft;
		mouseRight=snapshot.mouseRight;
		mousePort=snapshot.mousePort;
		mouseDx=snapshot.mouseDx;
		mouseDy=snapshot.mouseDy;
		towns.SetMouseButtonState(mouseLeft,mouseRight);
		if(0<=mousePort) towns.SetMouseMotion(mousePort,mouseDx,mouseDy);
		for(unsigned int port=0; port<2; ++port)
		{
			joypads[port]=snapshot.joypads[port];
			ApplyGamePad(port);
		}
	}

	std::string SnapshotJson(const std::string &request)
	{
		constexpr uint64_t MAX_SNAPSHOT_BYTES=256ULL*1024ULL*1024ULL;
		constexpr size_t MAX_SNAPSHOTS=8;
		running=false;
		const auto action=JsonString(request,"action");
		if("list"==action) return SnapshotSummaryJson();
		const auto name=JsonString(request,"name");
		if(name.empty() || 64<name.size() || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-")!=std::string::npos)
		{
			return "{\"error\":\"invalid_snapshot_name\"}";
		}
		if("delete"==action)
		{
			if(snapshots.end()==snapshots.find(name)) return "{\"error\":\"snapshot_not_found\"}";
			snapshots.erase(name);
			return SnapshotSummaryJson();
		}
		if("load"==action)
		{
			const auto found=snapshots.find(name);
			if(snapshots.end()==found) return "{\"error\":\"snapshot_not_found\"}";
			if(!towns.LoadStateMem(found->second.state)) return "{\"error\":\"snapshot_load_failed\"}";
			RestoreSnapshotInput(found->second);
			towns.debugger.ClearStopFlag();
			towns.ForceRender(render,outsideWorld,window);
			return SnapshotSummaryJson();
		}
		if("save"!=action) return "{\"error\":\"invalid_snapshot_action\"}";

		Snapshot snapshot;
		snapshot.state=towns.SaveStateMem();
		if(snapshot.state.empty() || MAX_SNAPSHOT_BYTES<snapshot.state.size()) return "{\"error\":\"snapshot_too_large\"}";
		snapshot.frame=ArclVmRunner::FrameFromTownsTime(towns.state.townsTime);
		snapshot.serial=++nextSnapshotSerial;
		snapshot.heldKeys=heldKeys;
		snapshot.mouseLeft=mouseLeft;
		snapshot.mouseRight=mouseRight;
		snapshot.mousePort=mousePort;
		snapshot.mouseDx=mouseDx;
		snapshot.mouseDy=mouseDy;
		for(unsigned int port=0; port<2; ++port) snapshot.joypads[port]=joypads[port];

		std::vector <std::string> evicted;
		auto old=snapshots.find(name);
		if(snapshots.end()!=old) snapshots.erase(old);
		while(!snapshots.empty() && (MAX_SNAPSHOTS<=snapshots.size() || MAX_SNAPSHOT_BYTES-SnapshotBytes()<snapshot.state.size()))
		{
			auto victim=snapshots.begin();
			for(auto found=snapshots.begin(); found!=snapshots.end(); ++found)
			{
				if(found->second.serial<victim->second.serial) victim=found;
			}
			evicted.push_back(victim->first);
			snapshots.erase(victim);
		}
		if(MAX_SNAPSHOT_BYTES-SnapshotBytes()<snapshot.state.size()) return "{\"error\":\"snapshot_capacity_exceeded\"}";
		snapshots[name]=std::move(snapshot);
		return SnapshotSummaryJson(evicted);
	}

	std::string ResetJson(void)
	{
		running=false;
		ClearInputJson();
		towns.Reset();
		towns.ForceRender(render,outsideWorld,window);
		return StatusJson();
	}

	std::string MacroJson(const std::string &request)
	{
		auto p=request.find("\"steps\"");
		if(std::string::npos==p || std::string::npos==(p=request.find('[',p))) return "{\"error\":\"missing_steps\"}";
		uint64_t completed=0,framesUsed=0;
		constexpr uint64_t MAX_STEPS=128;
		constexpr uint64_t MAX_TOTAL_FRAMES=36000;
		for(++p; p<request.size(); )
		{
			p=request.find('{',p);
			if(std::string::npos==p) break;
			if(MAX_STEPS<=completed) return "{\"error\":\"macro_step_limit\",\"steps_completed\":"+std::to_string(completed)+",\"failed_step\":"+std::to_string(completed)+",\"frames_used\":"+std::to_string(framesUsed)+"}";
			int depth=0; auto e=p;
			for(; e<request.size(); ++e) { if('{'==request[e]) ++depth; if('}'==request[e] && 0==--depth) break; }
			if(e==request.size()) return "{\"error\":\"invalid_steps\",\"steps_completed\":"+std::to_string(completed)+",\"failed_step\":"+std::to_string(completed)+"}";
			auto step=request.substr(p,e-p+1);
			auto tool=JsonString(step,"tool"); if(tool.empty()) tool=JsonString(step,"name");
			std::string stepResult;
			if("arcl_key"==tool) stepResult=KeyJson(JsonString(step,"key"),JsonString(step,"action"));
			else if("arcl_type"==tool) stepResult=TypeJson(JsonString(step,"text"));
			else if("arcl_mouse"==tool) stepResult=MouseJson(step);
			else if("arcl_joypad"==tool) stepResult=JoypadJson(step);
			else if("arcl_run"==tool)
			{
				const auto n=JsonUnsigned(step,"frames");
				if(MAX_TOTAL_FRAMES-framesUsed<n) return "{\"error\":\"macro_frame_limit\",\"steps_completed\":"+std::to_string(completed)+",\"frames_used\":"+std::to_string(framesUsed)+"}";
				stepResult=RunFrames(n);
				framesUsed+=JsonUnsigned(stepResult,"frames_used");
			}
			else return "{\"error\":\"unsupported_macro_step\",\"steps_completed\":"+std::to_string(completed)+",\"failed_step\":"+std::to_string(completed)+"}";
			if(std::string::npos!=stepResult.find("\"error\"")) return "{\"error\":\"macro_step_failed\",\"steps_completed\":"+std::to_string(completed)+",\"failed_step\":"+std::to_string(completed)+",\"frames_used\":"+std::to_string(framesUsed)+",\"detail\":"+stepResult+"}";
			++completed; p=e+1;
		}
		return "{\"machine\":\"towns\",\"state\":\"paused\",\"running\":false,\"frame\":"+std::to_string(ArclVmRunner::FrameFromTownsTime(towns.state.townsTime))+",\"frames_used\":"+std::to_string(framesUsed)+",\"steps_completed\":"+std::to_string(completed)+"}";
	}

	void ApplyGamePad(unsigned int port)
	{
		const auto &mcp=joypads[port];
		const auto &gui=(0==port ? guiJoypad : JoypadInput());
		towns.SetGamePadState(port,mcp.a||gui.a,mcp.b||gui.b,mcp.left||gui.left,mcp.right||gui.right,mcp.up||gui.up,mcp.down||gui.down,mcp.run||gui.run,mcp.pause||gui.pause,mcp.zoom||gui.zoom);
	}

	void ApplyGuiInput(void)
	{
		auto *nativeWindow=dynamic_cast <ArclNativeWindow *>(&window);
		if(nullptr==nativeWindow) return;
		const auto input=nativeWindow->CopyGuiInput();
		for(const auto key : guiKeys)
		{
			if(input.townsKeys.end()==input.townsKeys.find(key) && heldKeys.end()==heldKeys.find(key)) towns.keyboard.PushFifo(TOWNS_KEYFLAG_JIS_RELEASE,key);
		}
		for(const auto key : input.townsKeys)
		{
			if(guiKeys.end()==guiKeys.find(key) && heldKeys.end()==heldKeys.find(key)) towns.keyboard.PushFifo(TOWNS_KEYFLAG_JIS_PRESS,key);
		}
		guiKeys=input.townsKeys;
		guiJoypad.a=input.a;
		guiJoypad.b=input.b;
		guiJoypad.left=input.left;
		guiJoypad.right=input.right;
		guiJoypad.up=input.up;
		guiJoypad.down=input.down;
		guiJoypad.run=input.run;
		guiJoypad.pause=input.pause;
		guiJoypad.zoom=input.zoom;
		ApplyGamePad(0);
	}

	void PollWindow(void)
	{
		if(windowServicesOnVmThread) window.Interval();
		ApplyGuiInput();
	}
};

template <class TownsClass>
int Run(TownsClass &towns,Outside_World &outsideWorld,Outside_World::Sound &sound,Outside_World::WindowInterface &window,uint64_t frames)
{
	towns.DisableDebugger();
	ArclOptions options;
	options.frames=frames;
	ArclSession <TownsClass> session(towns,outsideWorld,sound,window,options);
	session.Start();
	auto result=session.RunFrames(frames);
	session.Stop();
	std::cout << "ARCL " << result << std::endl;
	return 0;
}

template <class TownsClass>
int RunInteractive(TownsClass &towns,Outside_World &outsideWorld,Outside_World::Sound &sound,Outside_World::WindowInterface &window,const ArclOptions &options)
{
	towns.DisableDebugger();
	ArclSession <TownsClass> session(towns,outsideWorld,sound,window,options);
	auto *nativeWindow=dynamic_cast <ArclNativeWindow *>(&window);
	if(nullptr==nativeWindow)
	{
		session.Start();
		session.ResumeJson();
		while(session.IsRunning()) session.AdvanceContinuous();
		session.Stop();
		if(options.profile) std::cout << "ARCL_PROFILE " << session.ProfileJson() << std::endl;
		return 0;
	}

	std::atomic <bool> guiReady=false;
	std::atomic <bool> stopGui=false;
	std::thread guiThread([&]
	{
		nativeWindow->Start();
		guiReady=true;
		auto deadline=std::chrono::steady_clock::now();
		while(!stopGui)
		{
			nativeWindow->Interval();
			deadline+=std::chrono::milliseconds(16);
			const auto now=std::chrono::steady_clock::now();
			if(deadline<now) deadline=now;
			std::this_thread::sleep_until(deadline);
		}
		nativeWindow->Stop();
	});
	while(!guiReady) std::this_thread::yield();

	session.Start(false);
	session.ResumeJson();
	while(session.IsRunning()) session.AdvanceContinuous();
	stopGui=true;
	guiThread.join();
	session.Stop(false);
	if(options.profile) std::cout << "ARCL_PROFILE " << session.ProfileJson() << std::endl;
	return 0;
}

template <class TownsClass>
int RunMcp(TownsClass &towns,Outside_World &outsideWorld,Outside_World::Sound &sound,Outside_World::WindowInterface &window,const ArclOptions &options,FILE *mcpOutput)
{
	if(options.mcpLayers.end()!=options.mcpLayers.find("l2")) towns.EnableDebugger();
	else towns.DisableDebugger();
	ArclSession <TownsClass> session(towns,outsideWorld,sound,window,options);
	auto *nativeWindow=dynamic_cast <ArclNativeWindow *>(&window);
	std::atomic <bool> guiReady=false;
	std::atomic <bool> stopGui=false;
	std::thread guiThread;
	if(nullptr!=nativeWindow)
	{
		guiThread=std::thread([&]
		{
			nativeWindow->Start();
			guiReady=true;
			auto deadline=std::chrono::steady_clock::now();
			while(!stopGui)
			{
				nativeWindow->Interval();
				deadline+=std::chrono::milliseconds(16);
				const auto now=std::chrono::steady_clock::now();
				if(deadline<now) deadline=now;
				std::this_thread::sleep_until(deadline);
			}
			nativeWindow->Stop();
		});
		while(!guiReady) std::this_thread::yield();
		session.Start(false);
	}
	else
	{
		session.Start();
	}

	std::mutex queueLock;
	std::deque <std::string> requestQueue;
	std::atomic <bool> inputClosed=false;
	std::thread inputThread([&]
	{
		std::string line;
		while(std::getline(std::cin,line))
		{
			std::lock_guard <std::mutex> lock(queueLock);
			requestQueue.push_back(line);
		}
		inputClosed=true;
	});

	ArclMcpServer server([&](const std::string &name,uint64_t frames,const std::string &key,const std::string &action,const std::string &text,const std::string &slot,const std::string &request)
	{
		if("arcl_status"==name) return session.StatusJson();
		if("arcl_video"==name) return session.VideoJson();
		if("arcl_palette"==name) return session.PaletteJson(request);
		if("arcl_irq"==name) return session.IrqJson();
		if("arcl_dma"==name) return session.DmaJson();
		if("arcl_sprites"==name) return session.SpritesJson(request);
		if("arcl_vram"==name) return session.VramJson(request);
		if("towns_ym2612"==name) return session.Ym2612Json();
		if("towns_rf5c68"==name) return session.Rf5c68Json();
		if("arcl_registers"==name) return session.RegistersJson();
		if("arcl_write_registers"==name) return session.WriteRegistersJson(request);
		if("arcl_read_mem"==name) return session.ReadMemoryJson(request);
		if("arcl_write_mem"==name) return session.WriteMemoryJson(request);
		if("arcl_disasm"==name) return session.DisasmJson(request);
		if("arcl_stack"==name) return session.StackJson(request);
		if("arcl_breakpoint"==name) return session.BreakpointJson(request);
		if("arcl_step"==name) return session.StepJson();
		if("arcl_console_read"==name) return session.ConsoleReadJson(request);
		if("arcl_command"==name) return session.CommandJson(text,frames,request);
		if("arcl_host_dir"==name) return session.HostDirJson();
		if("arcl_mount"==name) return session.MountJson(request);
		if("arcl_audio_record"==name) return session.AudioRecordJson(frames,request);
		if("arcl_pause"==name) return session.PauseJson();
		if("arcl_resume"==name) return session.ResumeJson();
		if("arcl_screenshot"==name) return session.ScreenshotJson(request);
		if("arcl_key"==name) return session.KeyJson(key,action);
		if("arcl_mouse"==name) return session.MouseJson(request);
		if("arcl_joypad"==name) return session.JoypadJson(request);
		if("arcl_type"==name) return session.TypeJson(text);
		if("arcl_input_state"==name) return session.InputStateJson();
		if("arcl_clear_input"==name) return session.ClearInputJson();
		if("arcl_save_state"==name) return session.StateJson(slot,false);
		if("arcl_load_state"==name) return session.StateJson(slot,true);
		if("arcl_snapshot"==name) return session.SnapshotJson(JsonArguments(request));
		if("arcl_rewind"==name) return session.RewindJson(request);
		if("arcl_speed"==name) return session.SpeedJson(request);
		if("arcl_reset"==name) return session.ResetJson();
		if("arcl_input_macro"==name) return session.MacroJson(request);
		const auto textMatch=JsonString(request,"text_match");
		if(!textMatch.empty() && !session.IsToolVisible("arcl_console_read")) return std::string("{\"error\":\"text_match_requires_l1\"}");
		const auto untilBreak=(HasJsonKey(request,"until_break") && JsonBoolean(request,"until_break"));
		if(untilBreak && !session.IsToolVisible("arcl_breakpoint")) return std::string("{\"error\":\"until_break_requires_l2\"}");
		const auto ignoreVmPause=(HasJsonKey(request,"ignore_vm_pause") && JsonBoolean(request,"ignore_vm_pause"));
		const auto noRender=(HasJsonKey(request,"no_render") && JsonBoolean(request,"no_render"));
		return session.RunFrames(frames,textMatch,ignoreVmPause,noRender);
	},[&](const std::string &name)
	{
		return session.IsToolVisible(name);
	});
	for(;;)
	{
		std::string request;
		{
			std::lock_guard <std::mutex> lock(queueLock);
			if(!requestQueue.empty())
			{
				request=std::move(requestQueue.front());
				requestQueue.pop_front();
			}
		}
		if(!request.empty())
		{
			auto response=server.HandleLine(request);
			if(response.has_value())
			{
				std::fputs((response.value()+"\n").c_str(),mcpOutput);
				std::fflush(mcpOutput);
			}
		}
		else if(inputClosed)
		{
			break;
		}
		if(session.IsRunning())
		{
			session.AdvanceContinuous();
		}
		else
		{
			session.PollWindow();
			std::this_thread::sleep_for(std::chrono::milliseconds(8));
		}
	}
	inputThread.join();
	session.Stop(nullptr==nativeWindow);
	if(nullptr!=nativeWindow)
	{
		stopGui=true;
		guiThread.join();
	}
	return 0;
}

FILE *OpenMcpOutput(void)
{
	// The legacy core uses both C++ iostreams and C stdout.  Duplicate the
	// original stdout pipe for JSON-RPC first, then redirect its public C/C++
	// stdout streams to stderr so a core diagnostic cannot corrupt a response.
	std::cout.rdbuf(std::cerr.rdbuf());
#ifdef _WIN32
	const auto mcpDescriptor=_dup(_fileno(stdout));
	if(0>mcpDescriptor || 0!=_dup2(_fileno(stderr),_fileno(stdout)))
	{
		if(0<=mcpDescriptor)
		{
			_close(mcpDescriptor);
		}
		return nullptr;
	}
	auto mcpOutput=_fdopen(mcpDescriptor,"w");
	if(nullptr==mcpOutput)
	{
		_close(mcpDescriptor);
		return nullptr;
	}
	return mcpOutput;
#else
	return stdout;
#endif
}

void CloseMcpOutput(FILE *mcpOutput)
{
#ifdef _WIN32
	if(nullptr!=mcpOutput)
	{
		std::fclose(mcpOutput);
	}
#else
	(void)mcpOutput;
#endif
}
}

int main(int ac,char *av[])
{
	if(sizeof(void *)<8 || sizeof(long long int)<8)
	{
		std::cerr << "arcl_windows_fmtowns requires a 64-bit build." << std::endl;
		return 1;
	}

	ArclOptions options;
	const auto executableDirectory=ExecutableDirectory(av[0]);
	options.outputDirectory=executableDirectory/"arcl-output";
	options.stateDirectory=executableDirectory/"arcl-state";
	std::vector <char *> townsArgv;
	auto parseResult=ParseOptions(ac,av,options,townsArgv);
	if(PARSE_OK!=parseResult)
	{
		return (PARSE_HELP==parseResult ? 0 : 1);
	}
	FILE *mcpOutput=nullptr;
	if(options.mcp && nullptr==(mcpOutput=OpenMcpOutput()))
	{
		std::cerr << "Cannot reserve stdout for MCP JSON-RPC." << std::endl;
		return 1;
	}

	TownsARGV argv;
	if(true!=argv.AnalyzeCommandParameter(static_cast<int>(townsArgv.size()),townsArgv.data()))
	{
		return 1;
	}

	std::unique_ptr <ArclCaptureHost> outsideWorld;
	if(true==options.noWindow)
	{
		outsideWorld=std::make_unique <ArclCaptureHost>();
	}
	else
	{
		outsideWorld=std::make_unique <ArclWindowedHost>();
	}
	std::unique_ptr <Outside_World::Sound,OutsideWorldSoundDeleter> sound(
		outsideWorld->CreateSound(),OutsideWorldSoundDeleter {outsideWorld.get()});
	std::unique_ptr <Outside_World::WindowInterface,OutsideWorldWindowDeleter> window(
		outsideWorld->CreateWindowInterface(),OutsideWorldWindowDeleter {outsideWorld.get()});

	if(i486DXCommon::HIGH_FIDELITY==argv.CPUFidelityLevel)
	{
		static FMTownsTemplate <i486DXHighFidelity> towns;
		if(true!=FMTownsCommon::Setup(towns,outsideWorld.get(),window.get(),argv))
		{
			return 1;
		}
		if(options.mcp)
		{
			const auto result=RunMcp(towns,*outsideWorld,*sound,*window,options,mcpOutput);
			CloseMcpOutput(mcpOutput);
			return result;
		}
		return (!options.noWindow && !options.framesSpecified ? RunInteractive(towns,*outsideWorld,*sound,*window,options) : Run(towns,*outsideWorld,*sound,*window,options.frames));
	}

	static FMTownsTemplate <i486DXDefaultFidelity> towns;
	if(true!=FMTownsCommon::Setup(towns,outsideWorld.get(),window.get(),argv))
	{
		return 1;
	}
	if(options.mcp)
	{
		const auto result=RunMcp(towns,*outsideWorld,*sound,*window,options,mcpOutput);
		CloseMcpOutput(mcpOutput);
		return result;
	}
	return (!options.noWindow && !options.framesSpecified ? RunInteractive(towns,*outsideWorld,*sound,*window,options) : Run(towns,*outsideWorld,*sound,*window,options.frames));
}

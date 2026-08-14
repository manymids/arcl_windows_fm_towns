#include <cctype>

#include "arcl_mcp_server.h"

namespace
{
struct ToolDefinition
{
	const char *name;
	const char *json;
};

const ToolDefinition TOOLS[]=
{
	{"arcl_status","{\"name\":\"arcl_status\",\"description\":\"Returns emulator status without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_run","{\"name\":\"arcl_run\",\"description\":\"Advances a paused emulator by virtual frames, optionally stopping at an L1 console text match or an L2 breakpoint/watchpoint. ignore_vm_pause is opt-in and ignores only the FreeTOWNSOS/Tsugaru VM pause request. no_render is a diagnostic option that advances the VM without composing a framebuffer.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"frames\":{\"type\":\"integer\",\"minimum\":0},\"text_match\":{\"type\":\"string\"},\"until_break\":{\"type\":\"boolean\"},\"ignore_vm_pause\":{\"type\":\"boolean\"},\"no_render\":{\"type\":\"boolean\"}},\"required\":[\"frames\"]}}"},
	{"arcl_pause","{\"name\":\"arcl_pause\",\"description\":\"Pauses continuous emulation.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_resume","{\"name\":\"arcl_resume\",\"description\":\"Starts continuous emulation.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_reset","{\"name\":\"arcl_reset\",\"description\":\"Resets the machine while retaining its virtual timeline.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_save_state","{\"name\":\"arcl_save_state\",\"description\":\"Saves a named state under arcl-state.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"string\"}},\"required\":[\"slot\"]}}"},
	{"arcl_load_state","{\"name\":\"arcl_load_state\",\"description\":\"Loads a named state from arcl-state.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"string\"}},\"required\":[\"slot\"]}}"},
	{"arcl_snapshot","{\"name\":\"arcl_snapshot\",\"description\":\"Common (arcl) L4 Control. Saves, loads, deletes, or lists bounded in-memory experiment snapshots; a load restores the virtual timeline and screen.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"save\",\"load\",\"delete\",\"list\"]},\"name\":{\"type\":\"string\"}},\"required\":[\"action\"]}}"},
	{"arcl_rewind","{\"name\":\"arcl_rewind\",\"description\":\"Common (arcl) L4 Control. Lists, checkpoints, clears, or rewinds the bounded in-memory execution history.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"list\",\"checkpoint\",\"rewind\",\"clear\"]},\"steps\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":8}},\"required\":[\"action\"]}}"},
	{"arcl_speed","{\"name\":\"arcl_speed\",\"description\":\"Common (arcl) L4 Control. Gets or sets continuous-resume host pacing as a real-time percentage without changing virtual frame semantics.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"percent\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000}}}}"},
	{"arcl_video","{\"name\":\"arcl_video\",\"description\":\"Common (arcl) L3 Observation. Returns decoded TOWNS CRTC mode, visible pages, display geometry, and VRAM layout without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_palette","{\"name\":\"arcl_palette\",\"description\":\"Common (arcl) L3 Observation. Returns decoded active, 16-colour, or 256-colour CRTC RGB palette entries without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"format\":{\"type\":\"string\",\"enum\":[\"active\",\"16\",\"256\"]},\"page\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":1}}}}"},
	{"arcl_irq","{\"name\":\"arcl_irq\",\"description\":\"Common (arcl) L3 Observation. Returns decoded master/slave PIC request, in-service, mask, and vector state without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_dma","{\"name\":\"arcl_dma\",\"description\":\"Common (arcl) L3 Observation. Returns decoded TOWNS primary DMA controller and all channel transfer state without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_sprites","{\"name\":\"arcl_sprites\",\"description\":\"Common (arcl) L3 Observation. Returns bounded decoded TOWNS sprite attributes without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"start\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":1023},\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":128}}}}"},
	{"arcl_vram","{\"name\":\"arcl_vram\",\"description\":\"Common (arcl) L3 Observation. Reads a bounded raw linear TOWNS VRAM range without advancing virtual time; use arcl_video for decoded page layout.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"offset\":{},\"length\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":4096}},\"required\":[\"offset\",\"length\"]}}"},
	{"towns_ym2612","{\"name\":\"towns_ym2612\",\"description\":\"TOWNS L3 Observation. Returns decoded YM2612 channel, key, pitch, pan, modulation, and timer state without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"towns_rf5c68","{\"name\":\"towns_rf5c68\",\"description\":\"TOWNS L3 Observation. Returns decoded RF5C68 PCM channel, pitch, pan, envelope, loop, and IRQ state without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_screenshot","{\"name\":\"arcl_screenshot\",\"description\":\"Returns a cropped, nearest-neighbour scaled composed framebuffer as PNG; optional grid and managed path avoid visual ambiguity and large inline replies.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"integer\",\"minimum\":0},\"y\":{\"type\":\"integer\",\"minimum\":0},\"w\":{\"type\":\"integer\",\"minimum\":1},\"h\":{\"type\":\"integer\",\"minimum\":1},\"scale\":{\"type\":\"integer\",\"minimum\":1},\"grid\":{\"type\":\"boolean\"},\"grid_size\":{\"type\":\"integer\",\"minimum\":1},\"path\":{\"type\":\"string\"},\"inline\":{\"type\":\"boolean\"}}}}"},
	{"arcl_key","{\"name\":\"arcl_key\",\"description\":\"Presses, releases, or taps a TOWNS JIS key.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"key\":{\"type\":\"string\"},\"action\":{\"type\":\"string\",\"enum\":[\"press\",\"release\",\"tap\"]}},\"required\":[\"key\",\"action\"]}}"},
	{"arcl_type","{\"name\":\"arcl_type\",\"description\":\"Queues ASCII text as TOWNS keyboard events.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}},\"required\":[\"text\"]}}"},
	{"arcl_mouse","{\"name\":\"arcl_mouse\",\"description\":\"Applies relative mouse motion and optional button state.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"dx\":{\"type\":\"integer\"},\"dy\":{\"type\":\"integer\"},\"left\":{\"type\":\"boolean\"},\"right\":{\"type\":\"boolean\"}}}}"},
	{"arcl_joypad","{\"name\":\"arcl_joypad\",\"description\":\"Updates a configured TOWNS game-pad port.  Omitted buttons retain their current state.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"port\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":1},\"a\":{\"type\":\"boolean\"},\"b\":{\"type\":\"boolean\"},\"left\":{\"type\":\"boolean\"},\"right\":{\"type\":\"boolean\"},\"up\":{\"type\":\"boolean\"},\"down\":{\"type\":\"boolean\"},\"run\":{\"type\":\"boolean\"},\"pause\":{\"type\":\"boolean\"},\"zoom\":{\"type\":\"boolean\"}}}}"},
	{"arcl_input_macro","{\"name\":\"arcl_input_macro\",\"description\":\"Executes a bounded key, text, mouse, joypad, and frame timeline.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"steps\":{\"type\":\"array\"}},\"required\":[\"steps\"]}}"},
	{"arcl_input_state","{\"name\":\"arcl_input_state\",\"description\":\"Returns ARCL-held input state.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_clear_input","{\"name\":\"arcl_clear_input\",\"description\":\"Releases all ARCL-held input.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_audio_record","{\"name\":\"arcl_audio_record\",\"description\":\"Runs a bounded virtual interval while capturing generated 44.1kHz stereo PCM, returning levels and optional managed WAV.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"frames\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":3600},\"path\":{\"type\":\"string\"}},\"required\":[\"frames\"]}}"},
	{"arcl_console_read","{\"name\":\"arcl_console_read\",\"description\":\"Returns UTF-8 output captured from the TOWNS host-console bridge.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"clear\":{\"type\":\"boolean\"}}}}"},
	{"arcl_command","{\"name\":\"arcl_command\",\"description\":\"Queues a shell command through keyboard and FreeTOWNSOS console channels, then runs virtual frames.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"},\"frames\":{\"type\":\"integer\",\"minimum\":0},\"text_match\":{\"type\":\"string\"}},\"required\":[\"text\"]}}"},
	{"arcl_host_dir","{\"name\":\"arcl_host_dir\",\"description\":\"Lists configured TOWNS shared directories that are inside an ARCL allow root.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_mount","{\"name\":\"arcl_mount\",\"description\":\"Mounts or ejects a floppy or internal CD from an ARCL allow root while paused.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"kind\":{\"type\":\"string\",\"enum\":[\"floppy\",\"cd\"]},\"action\":{\"type\":\"string\",\"enum\":[\"mount\",\"eject\"]},\"drive\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":3},\"path\":{\"type\":\"string\"}},\"required\":[\"kind\",\"action\"]}}"},
	{"arcl_registers","{\"name\":\"arcl_registers\",\"description\":\"Common (arcl) L2 Observation. Returns the current i486 CPU registers without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
	{"arcl_write_registers","{\"name\":\"arcl_write_registers\",\"description\":\"Common (arcl) L2 Action. Selectively writes general i486 registers; numbers may be integers or 0x-prefixed strings.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"eax\":{},\"ebx\":{},\"ecx\":{},\"edx\":{},\"esi\":{},\"edi\":{},\"ebp\":{},\"esp\":{},\"eip\":{},\"eflags\":{}}}}"},
	{"arcl_read_mem","{\"name\":\"arcl_read_mem\",\"description\":\"Common (arcl) L2 Observation. Reads a bounded physical-memory range without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"address\":{},\"length\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":4096}},\"required\":[\"address\",\"length\"]}}"},
	{"arcl_write_mem","{\"name\":\"arcl_write_mem\",\"description\":\"Common (arcl) L2 Action. Writes up to 4096 physical-memory bytes encoded as data_hex.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"address\":{},\"data_hex\":{\"type\":\"string\"}},\"required\":[\"address\",\"data_hex\"]}}"},
	{"arcl_disasm","{\"name\":\"arcl_disasm\",\"description\":\"Common (arcl) L2 Observation. Disassembles up to 64 instructions in the current CS; address is an EIP offset.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"address\":{},\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":64}}}}"},
	{"arcl_stack","{\"name\":\"arcl_stack\",\"description\":\"Common (arcl) L2 Observation. Returns a bounded stack-memory window without advancing virtual time.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"length\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":4096}}}}"},
	{"arcl_breakpoint","{\"name\":\"arcl_breakpoint\",\"description\":\"Common (arcl) L2 Action. Adds, removes, clears, or lists execution breakpoints and physical-memory read/write/access watchpoints.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"add\",\"remove\",\"clear\",\"list\"]},\"type\":{\"type\":\"string\",\"enum\":[\"exec\",\"read\",\"write\",\"access\"]},\"cs\":{},\"address\":{},\"length\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":4096}},\"required\":[\"action\"]}}"},
	{"arcl_step","{\"name\":\"arcl_step\",\"description\":\"Common (arcl) L2 Action. Executes exactly one i486 instruction, then returns paused.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"},
};

bool IsArclTool(const std::string &name)
{
	for(const auto &tool : TOOLS)
	{
		if(name==tool.name) return true;
	}
	return false;
}

std::string ToolListJson(const ArclMcpServer::ToolVisibility &toolVisibility)
{
	std::string json="{\"tools\":[";
	bool first=true;
	for(const auto &tool : TOOLS)
	{
		if(toolVisibility && !toolVisibility(tool.name)) continue;
		if(!first) json+=',';
		json+=tool.json;
		first=false;
	}
	return json+"]}";
}
}

ArclMcpServer::ArclMcpServer(ToolHandler toolHandler,ToolVisibility toolVisibility)
	: toolHandler(toolHandler),toolVisibility(toolVisibility)
{
}

std::string ArclMcpServer::EscapeJson(const std::string &text)
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

std::optional <std::string> ArclMcpServer::FindString(const std::string &json,const char key[])
{
	const std::string needle=std::string("\"")+key+"\"";
	auto pos=json.find(needle);
	if(std::string::npos==pos || std::string::npos==(pos=json.find(':',pos+needle.size())))
	{
		return {};
	}
	++pos;
	while(pos<json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
	{
		++pos;
	}
	if(pos==json.size() || '"'!=json[pos])
	{
		return {};
	}
	std::string value;
	for(++pos; pos<json.size(); ++pos)
	{
		if('\\'==json[pos] && pos+1<json.size())
		{
			const auto escaped=json[++pos];
			if('u'==escaped && pos+4<json.size())
			{
				// The Phase-2 type surface accepts ASCII only.  Preserve a
				// non-ASCII marker rather than silently turning "\\u3042" into
				// the six ASCII characters u3042.
				value+=static_cast<char>(0x80);
				pos+=4;
			}
			else
			{
				value+=escaped;
			}
		}
		else if('"'==json[pos])
		{
			return value;
		}
		else
		{
			value+=json[pos];
		}
	}
	return {};
}

std::string ArclMcpServer::FindId(const std::string &json)
{
	const auto key=json.find("\"id\"");
	if(std::string::npos==key)
	{
		return "null";
	}
	auto begin=json.find(':',key+4);
	if(std::string::npos==begin)
	{
		return "null";
	}
	++begin;
	while(begin<json.size() && std::isspace(static_cast<unsigned char>(json[begin])))
	{
		++begin;
	}
	if(begin==json.size())
	{
		return "null";
	}
	if('"'==json[begin])
	{
		auto end=begin+1;
		while(end<json.size() && '"'!=json[end])
		{
			end+=('\\'==json[end] ? 2 : 1);
		}
		return json.substr(begin,end-begin+1);
	}
	auto end=begin;
	while(end<json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || '-'==json[end]))
	{
		++end;
	}
	return (end==begin ? "null" : json.substr(begin,end-begin));
}

uint64_t ArclMcpServer::FindUnsigned(const std::string &json,const char key[],uint64_t defaultValue)
{
	const std::string needle=std::string("\"")+key+"\"";
	auto pos=json.find(needle);
	if(std::string::npos==pos || std::string::npos==(pos=json.find(':',pos+needle.size())))
	{
		return defaultValue;
	}
	++pos;
	while(pos<json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
	{
		++pos;
	}
	uint64_t value=0;
	const auto begin=pos;
	while(pos<json.size() && std::isdigit(static_cast<unsigned char>(json[pos])))
	{
		value=value*10+(json[pos++]-'0');
	}
	return (begin==pos ? defaultValue : value);
}

std::string ArclMcpServer::Error(const std::string &id,int code,const char message[])
{
	return "{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"error\":{\"code\":"+std::to_string(code)+",\"message\":\""+EscapeJson(message)+"\"}}";
}

std::string ArclMcpServer::Result(const std::string &id,const std::string &result)
{
	return "{\"jsonrpc\":\"2.0\",\"id\":"+id+",\"result\":"+result+"}";
}

std::optional <std::string> ArclMcpServer::HandleLine(const std::string &line) const
{
	const auto id=FindId(line);
	const auto method=FindString(line,"method");
	if(!method.has_value())
	{
		return Error(id,-32600,"Request has no string method.");
	}
	if("notifications/initialized"==*method)
	{
		return {};
	}
	if("initialize"==*method)
	{
		return Result(id,"{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"tsugaru-arcl\",\"version\":\"0.1.0\"}}");
	}
	if("tools/list"==*method)
	{
		return Result(id,ToolListJson(toolVisibility));
	}
	if("tools/call"==*method)
	{
		const auto name=FindString(line,"name");
		if(name.has_value() && IsArclTool(*name) && (!toolVisibility || toolVisibility(*name)))
		{
			if("arcl_run"==*name && std::string::npos!=line.find("\"max_frames\""))
			{
				return Result(id,"{\"content\":[{\"type\":\"text\",\"text\":\"{\\\"error\\\":\\\"unsupported_argument\\\",\\\"argument\\\":\\\"max_frames\\\",\\\"message\\\":\\\"Use frames.\\\"}\"}],\"isError\":true}");
			}
			const auto payload=(toolHandler ? toolHandler(*name,FindUnsigned(line,"frames",0),FindString(line,"key").value_or(""),FindString(line,"action").value_or(""),FindString(line,"text").value_or(""),FindString(line,"slot").value_or(""),line) : "{\"machine\":\"towns\",\"state\":\"bootstrap\",\"frame\":0}");
			return Result(id,"{\"content\":[{\"type\":\"text\",\"text\":\""+EscapeJson(payload)+"\"}]}" );
		}
		return Result(id,"{\"content\":[{\"type\":\"text\",\"text\":\"Unknown or unavailable ARCL tool.\"}],\"isError\":true}");
	}
	return Error(id,-32601,"Method not found.");
}

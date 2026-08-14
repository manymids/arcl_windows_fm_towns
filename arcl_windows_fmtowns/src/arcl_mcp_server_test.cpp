#include <cstdlib>
#include <iostream>

#include "arcl_mcp_server.h"

namespace
{
void Check(bool condition,const char message[])
{
	if(!condition)
	{
		std::cerr << "FAILED: " << message << std::endl;
		std::exit(1);
	}
}
}

int main(void)
{
	ArclMcpServer server;
	auto initialize=server.HandleLine("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}");
	Check(initialize.has_value() && std::string::npos!=initialize->find("protocolVersion"),"initialize must return MCP capabilities.");
	auto list=server.HandleLine("{\"jsonrpc\":\"2.0\",\"id\":\"list\",\"method\":\"tools/list\"}");
	Check(list.has_value() && std::string::npos!=list->find("arcl_status"),"tools/list must expose implemented tools.");
	Check(std::string::npos!=list->find("arcl_save_state") && std::string::npos!=list->find("arcl_reset"),"tools/list must expose all Control tools.");
	Check(std::string::npos!=list->find("arcl_input_macro"),"tools/list must expose input macros.");
	Check(std::string::npos!=list->find("arcl_joypad") && std::string::npos!=list->find("arcl_pause"),"tools/list must expose recent input and control tools.");
	Check(std::string::npos!=list->find("arcl_audio_record"),"tools/list must expose the L0 audio capture tool.");
	Check(std::string::npos!=list->find("arcl_snapshot"),"tools/list must expose the L4 snapshot tool when visible.");
	Check(std::string::npos!=list->find("arcl_rewind") && std::string::npos!=list->find("arcl_speed"),"tools/list must expose the L4 rewind and speed tools when visible.");
	Check(std::string::npos!=list->find("arcl_video"),"tools/list must expose the L3 video tool when visible.");
	Check(std::string::npos!=list->find("arcl_palette"),"tools/list must expose the L3 palette tool when visible.");
	Check(std::string::npos!=list->find("arcl_irq"),"tools/list must expose the L3 IRQ tool when visible.");
	Check(std::string::npos!=list->find("arcl_dma"),"tools/list must expose the L3 DMA tool when visible.");
	Check(std::string::npos!=list->find("arcl_sprites"),"tools/list must expose the L3 sprites tool when visible.");
	Check(std::string::npos!=list->find("arcl_vram"),"tools/list must expose the L3 VRAM tool when visible.");
	Check(std::string::npos!=list->find("towns_ym2612") && std::string::npos!=list->find("towns_rf5c68"),"tools/list must expose the TOWNS L3 audio tools when visible.");
	Check(std::string::npos!=list->find("arcl_console_read") && std::string::npos!=list->find("arcl_command") && std::string::npos!=list->find("arcl_host_dir") && std::string::npos!=list->find("arcl_mount"),"tools/list must expose L1 console and media tools when visible.");
	Check(std::string::npos!=list->find("arcl_registers") && std::string::npos!=list->find("arcl_read_mem") && std::string::npos!=list->find("arcl_breakpoint") && std::string::npos!=list->find("arcl_step"),"tools/list must expose implemented L2 tools when visible.");
	Check(std::string::npos==list->find('\n'),"stdio JSON-RPC responses must stay on one line.");
	ArclMcpServer controlOnly({},[](const std::string &name)
	{
		return "arcl_status"==name || "arcl_run"==name || "arcl_pause"==name || "arcl_resume"==name ||
			"arcl_reset"==name || "arcl_save_state"==name || "arcl_load_state"==name;
	});
	auto controlList=controlOnly.HandleLine("{\"jsonrpc\":\"2.0\",\"id\":\"control\",\"method\":\"tools/list\"}");
	Check(controlList.has_value() && std::string::npos!=controlList->find("arcl_status") && std::string::npos==controlList->find("arcl_screenshot") && std::string::npos==controlList->find("arcl_console_read") && std::string::npos==controlList->find("arcl_registers") && std::string::npos==controlList->find("arcl_snapshot"),"tool visibility must filter non-selected layers.");
	auto hiddenTool=controlOnly.HandleLine("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"arcl_screenshot\"}}");
	Check(hiddenTool.has_value() && std::string::npos!=hiddenTool->find("isError"),"hidden tools must reject calls.");
	std::string typed;
	ArclMcpServer textServer([&](const std::string &,uint64_t,const std::string &,const std::string &,const std::string &text,const std::string &,const std::string &)
	{
		typed=text;
		return "{}";
	});
	textServer.HandleLine("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"arcl_type\",\"arguments\":{\"text\":\"\\u3042\"}}}");
	Check(1==typed.size() && 0x80==static_cast<unsigned char>(typed[0]),"escaped non-ASCII text must stay distinguishable from ASCII.");
	auto status=server.HandleLine("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"arcl_status\"}}");
	Check(status.has_value() && std::string::npos!=status->find("\\\"machine\\\":\\\"towns"),"arcl_status must identify the machine.");
	Check(!server.HandleLine("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}").has_value(),"notifications must not receive responses.");
	return 0;
}

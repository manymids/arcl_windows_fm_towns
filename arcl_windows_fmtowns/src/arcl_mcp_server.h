#ifndef ARCL_MCP_SERVER_IS_INCLUDED
#define ARCL_MCP_SERVER_IS_INCLUDED

#include <cstdint>
#include <optional>
#include <functional>
#include <string>

/*! Dependency-free JSON-RPC subset used by the Phase-1 stdio MCP transport.

    It owns neither stdin/stdout nor the emulator.  The process host supplies
    a dispatcher in the next increment; keeping framing separate makes the
    transport testable without a ROM.
*/
class ArclMcpServer
{
public:
	using ToolHandler=std::function <std::string (const std::string &name,uint64_t frames,const std::string &key,const std::string &action,const std::string &text,const std::string &slot,const std::string &request)>;
	using ToolVisibility=std::function <bool (const std::string &name)>;

	explicit ArclMcpServer(ToolHandler toolHandler={},ToolVisibility toolVisibility={});
	std::optional <std::string> HandleLine(const std::string &line) const;

private:
	static std::string EscapeJson(const std::string &text);
	static std::optional <std::string> FindString(const std::string &json,const char key[]);
	static std::string FindId(const std::string &json);
	static std::string Error(const std::string &id,int code,const char message[]);
	static std::string Result(const std::string &id,const std::string &result);
	static uint64_t FindUnsigned(const std::string &json,const char key[],uint64_t defaultValue);
	ToolHandler toolHandler;
	ToolVisibility toolVisibility;
};

#endif

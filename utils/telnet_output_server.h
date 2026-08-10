#ifndef PDW_TELNET_OUTPUT_SERVER_H
#define PDW_TELNET_OUTPUT_SERVER_H

#include <cstddef>
#include <string>

namespace pdw
{
namespace outputs
{

// A deliberately small, read-only JSON stream. It does not implement a command
// channel, so a connected client cannot alter PDW or inject decoder data.
class TelnetOutputServer
{
public:
	struct State; // opaque implementation state

	TelnetOutputServer();
	~TelnetOutputServer();

	bool Start(const std::string& bindAddress, unsigned short port,
		bool allowRemote, std::string& error);
	void Stop();
	bool Broadcast(const std::string& jsonLine);
	bool IsRunning() const;
	unsigned short BoundPort() const;
	std::size_t ClientCount() const;
	std::string Status() const;

private:
	TelnetOutputServer(const TelnetOutputServer&);
	TelnetOutputServer& operator=(const TelnetOutputServer&);

	State* state_;
};

} // namespace outputs
} // namespace pdw

#endif

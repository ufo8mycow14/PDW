#ifndef PDW_LOCAL_DASHBOARD_SERVER_H
#define PDW_LOCAL_DASHBOARD_SERVER_H

#include <cstddef>
#include <string>

namespace pdw
{
namespace dashboard
{

bool ParseRequestTarget(const std::string& request, std::string& path,
	std::string& query);
bool HasAllowedHostHeader(const std::string& request, unsigned short boundPort);
std::string QueryValue(const std::string& query, const std::string& name);
std::string UrlDecode(const std::string& value);
std::string BuildDashboardHtml();

class LocalDashboardServer
{
public:
	struct State;

	LocalDashboardServer();
	~LocalDashboardServer();

	bool Start(unsigned short port, std::string& error);
	void RequestStop();
	void Stop();
	bool IsRunning() const;
	unsigned short BoundPort() const;
	std::string Status() const;

private:
	LocalDashboardServer(const LocalDashboardServer&);
	LocalDashboardServer& operator=(const LocalDashboardServer&);

	State* state_;
};

} // namespace dashboard
} // namespace pdw

#endif

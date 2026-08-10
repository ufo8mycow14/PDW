#include "local_dashboard_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdlib>
#include <iostream>
#include <string>

std::string MessageArchiveBuildMessagesJson(int, int, const std::string&,
	const std::string&, bool) { return "{\"events\":[]}"; }
std::string MessageArchiveBuildCapcodesJson(const std::string&) { return "{\"capcodes\":[]}"; }

namespace
{
	void Expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAILED: " << message << '\n';
			std::exit(1);
		}
	}
}

int main()
{
	std::string path, query;
	Expect(pdw::dashboard::ParseRequestTarget(
		"GET /api/v1/messages?q=Station+Alpha&limit=50 HTTP/1.1\r\nHost: localhost\r\n\r\n",
		path, query), "valid GET target parses");
	Expect(path == "/api/v1/messages", "request path parsed");
	Expect(pdw::dashboard::HasAllowedHostHeader(
		"GET / HTTP/1.1\r\nHost: localhost:8090\r\n\r\n", 8090),
		"localhost Host header accepted");
	Expect(pdw::dashboard::HasAllowedHostHeader(
		"GET / HTTP/1.1\r\nhOsT: 127.0.0.1\r\n\r\n", 8090),
		"loopback Host header accepted case-insensitively");
	Expect(!pdw::dashboard::HasAllowedHostHeader(
		"GET / HTTP/1.1\r\nHost: attacker.example\r\n\r\n", 8090),
		"non-local Host header rejected");
	Expect(!pdw::dashboard::HasAllowedHostHeader(
		"GET / HTTP/1.1\r\nHost: localhost:9000\r\n\r\n", 8090),
		"wrong local port rejected");
	Expect(!pdw::dashboard::HasAllowedHostHeader(
		"GET / HTTP/1.1\r\nHost: localhost\r\nHost: 127.0.0.1\r\n\r\n", 8090),
		"duplicate Host header rejected");
	Expect(pdw::dashboard::QueryValue(query, "q") == "Station Alpha", "query value decoded");
	Expect(!pdw::dashboard::ParseRequestTarget("POST / HTTP/1.1\r\n\r\n", path, query),
		"non-GET request rejected");
	Expect(!pdw::dashboard::ParseRequestTarget("GET /../secret HTTP/1.1\r\n\r\n", path, query),
		"path traversal rejected");
	const std::string html = pdw::dashboard::BuildDashboardHtml();
	Expect(html.find("/api/v1/messages") != std::string::npos, "dashboard uses versioned messages API");
	Expect(html.find("address_name") != std::string::npos, "dashboard renders directory names");

	pdw::dashboard::LocalDashboardServer server;
	std::string error;
	unsigned short port = 0;
	for (unsigned short candidate = 49190; candidate < 49210 && !port; ++candidate)
		if (server.Start(candidate, error)) port = candidate;
	Expect(port != 0 && server.IsRunning(), "loopback dashboard listener starts");
	WSADATA sockets = {};
	Expect(WSAStartup(MAKEWORD(2, 2), &sockets) == 0, "test client sockets start");
	SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in endpoint = {};
	endpoint.sin_family = AF_INET;
	endpoint.sin_port = htons(port);
	inet_pton(AF_INET, "127.0.0.1", &endpoint.sin_addr);
	Expect(client != INVALID_SOCKET && connect(client, reinterpret_cast<sockaddr*>(&endpoint),
		sizeof(endpoint)) == 0, "health client connects through loopback");
	const char request[] = "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
	Expect(send(client, request, sizeof(request) - 1, 0) == sizeof(request) - 1,
		"health request sends");
	std::string response;
	char buffer[2048];
	int received = 0;
	while ((received = recv(client, buffer, sizeof(buffer), 0)) > 0) response.append(buffer, received);
	closesocket(client);
	WSACleanup();
	Expect(response.find("HTTP/1.1 200 OK") == 0 &&
		response.find("\"local_only\":true") != std::string::npos,
		"health endpoint confirms loopback-only listener");
	client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	Expect(client != INVALID_SOCKET && connect(client, reinterpret_cast<sockaddr*>(&endpoint),
		sizeof(endpoint)) == 0, "rejected-host client connects through loopback");
	const char rejectedRequest[] = "GET /health HTTP/1.1\r\nHost: attacker.example\r\nConnection: close\r\n\r\n";
	Expect(send(client, rejectedRequest, sizeof(rejectedRequest) - 1, 0) == sizeof(rejectedRequest) - 1,
		"rejected Host request sends");
	response.clear();
	while ((received = recv(client, buffer, sizeof(buffer), 0)) > 0) response.append(buffer, received);
	closesocket(client);
	Expect(response.find("HTTP/1.1 403 Forbidden") == 0,
		"live dashboard rejects a DNS-rebinding Host header");
	server.Stop();
	Expect(!server.IsRunning(), "dashboard listener stops cleanly");
	std::cout << "Local dashboard core tests passed\n";
	return 0;
}

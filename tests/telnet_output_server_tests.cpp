#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "telnet_output_server.h"

#include <iostream>
#include <string>

int main()
{
	pdw::outputs::TelnetOutputServer server;
	std::string error;
	if (!server.Start("127.0.0.1", 0, false, error) || !server.BoundPort())
	{
		std::cerr << "Could not start loopback Telnet test server: " << error << '\n';
		return 1;
	}

	SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (client == INVALID_SOCKET)
	{
		std::cerr << "Could not create Telnet test client.\n";
		return 1;
	}
	sockaddr_in endpoint = {};
	endpoint.sin_family = AF_INET;
	endpoint.sin_port = htons(server.BoundPort());
	InetPtonA(AF_INET, "127.0.0.1", &endpoint.sin_addr);
	if (connect(client, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == SOCKET_ERROR)
	{
		std::cerr << "Could not connect to the loopback Telnet test server.\n";
		closesocket(client);
		return 1;
	}
	DWORD receiveTimeout = 3000;
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
		reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));
	char buffer[1024] = {};
	recv(client, buffer, sizeof(buffer) - 1, 0); // read the server greeting
	for (int attempt = 0; attempt < 20 && server.ClientCount() == 0; ++attempt) Sleep(25);
	const std::string payload = "{\"source\":\"PDW\",\"message\":\"loopback test\"}";
	if (!server.Broadcast(payload))
	{
		std::cerr << "Telnet server did not accept the test broadcast.\n";
		closesocket(client);
		return 1;
	}
	ZeroMemory(buffer, sizeof(buffer));
	const int received = recv(client, buffer, sizeof(buffer) - 1, 0);
	closesocket(client);
	server.Stop();
	if (received <= 0 || std::string(buffer, received) != payload + "\r\n")
	{
		std::cerr << "Telnet client received an unexpected JSON stream payload.\n";
		return 1;
	}
	std::cout << "Telnet loopback integration test passed.\n";
	return 0;
}

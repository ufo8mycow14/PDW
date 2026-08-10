#ifndef STRICT
#define STRICT 1
#endif

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>

#include "telnet_output_server.h"

#include <algorithm>
#include <vector>

#include "data_output_core.h"

namespace pdw
{
namespace outputs
{

struct TelnetOutputServer::State
{
	State()
		: initialized(false), winsock(false), listener(INVALID_SOCKET), stopEvent(NULL),
		  thread(NULL), running(0), boundPort(0), status("Telnet JSON output is stopped.") {}

	CRITICAL_SECTION lock;
	bool initialized;
	bool winsock;
	SOCKET listener;
	HANDLE stopEvent;
	HANDLE thread;
	volatile LONG running;
	unsigned short boundPort;
	std::vector<SOCKET> clients;
	std::string status;
};

namespace
{
	const std::size_t MAX_CLIENTS = 16;

	void SetStatus(TelnetOutputServer::State* state, const std::string& status)
	{
		EnterCriticalSection(&state->lock);
		state->status = status;
		LeaveCriticalSection(&state->lock);
	}

	void CloseSocket(SOCKET socketValue)
	{
		if (socketValue == INVALID_SOCKET) return;
		shutdown(socketValue, SD_BOTH);
		closesocket(socketValue);
	}

	unsigned int __stdcall AcceptWorker(void* argument)
	{
		TelnetOutputServer::State* state = static_cast<TelnetOutputServer::State*>(argument);
		while (WaitForSingleObject(state->stopEvent, 0) != WAIT_OBJECT_0)
		{
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(state->listener, &readSet);
			timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 250000;
			const int selected = select(0, &readSet, NULL, NULL, &timeout);
			if (selected == SOCKET_ERROR)
			{
				if (WaitForSingleObject(state->stopEvent, 0) == WAIT_OBJECT_0) break;
				SetStatus(state, "Telnet JSON listener stopped after a socket error.");
				break;
			}
			if (!selected) continue;
			SOCKET client = accept(state->listener, NULL, NULL);
			if (client == INVALID_SOCKET) continue;
			u_long nonBlocking = 1;
			ioctlsocket(client, FIONBIO, &nonBlocking);
			bool accepted = false;
			EnterCriticalSection(&state->lock);
			if (state->clients.size() < MAX_CLIENTS)
			{
				state->clients.push_back(client);
				accepted = true;
				state->status = "Telnet JSON client connected.";
			}
			LeaveCriticalSection(&state->lock);
			if (accepted)
			{
				static const char greeting[] = "PDW JSON message stream (read only)\r\n";
				send(client, greeting, static_cast<int>(sizeof(greeting) - 1), 0);
			}
			else CloseSocket(client);
		}
		InterlockedExchange(&state->running, 0);
		return 0;
	}
}

TelnetOutputServer::TelnetOutputServer() : state_(new State)
{
	InitializeCriticalSection(&state_->lock);
	state_->initialized = true;
}

TelnetOutputServer::~TelnetOutputServer()
{
	Stop();
	if (state_)
	{
		if (state_->initialized) DeleteCriticalSection(&state_->lock);
		delete state_;
		state_ = NULL;
	}
}

bool TelnetOutputServer::Start(const std::string& bindAddress, unsigned short port,
	bool allowRemote, std::string& error)
{
	error.clear();
	if (IsRunning())
	{
		error = "Telnet JSON output is already running.";
		return false;
	}
	if (!ValidateTelnetEndpoint(bindAddress, port ? port : 1, allowRemote, error) && port != 0)
		return false;
	if (port == 0 && !IsLoopbackBindAddress(bindAddress))
	{
		error = "Automatic test ports are restricted to loopback.";
		return false;
	}

	WSADATA winsockData;
	if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0)
	{
		error = "Windows Sockets could not start for Telnet JSON output.";
		return false;
	}
	state_->winsock = true;

	addrinfo hints = {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	char portText[8];
	_snprintf_s(portText, sizeof(portText), _TRUNCATE, "%u", static_cast<unsigned int>(port));
	addrinfo* addresses = NULL;
	const int lookup = getaddrinfo(bindAddress.c_str(), portText, &hints, &addresses);
	if (lookup != 0)
	{
		error = "Telnet bind address could not be resolved.";
		Stop();
		return false;
	}
	for (addrinfo* address = addresses; address; address = address->ai_next)
	{
		SOCKET listener = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		if (listener == INVALID_SOCKET) continue;
		BOOL exclusive = TRUE;
		setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
			reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
		if (bind(listener, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0 &&
			listen(listener, SOMAXCONN) == 0)
		{
			state_->listener = listener;
			break;
		}
		CloseSocket(listener);
	}
	freeaddrinfo(addresses);
	if (state_->listener == INVALID_SOCKET)
	{
		error = "Telnet JSON output could not bind or listen on the selected endpoint.";
		Stop();
		return false;
	}

	sockaddr_storage localAddress = {};
	int localLength = sizeof(localAddress);
	if (getsockname(state_->listener, reinterpret_cast<sockaddr*>(&localAddress), &localLength) == 0)
	{
		if (localAddress.ss_family == AF_INET)
			state_->boundPort = ntohs(reinterpret_cast<sockaddr_in*>(&localAddress)->sin_port);
		else if (localAddress.ss_family == AF_INET6)
			state_->boundPort = ntohs(reinterpret_cast<sockaddr_in6*>(&localAddress)->sin6_port);
	}
	state_->stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!state_->stopEvent)
	{
		error = "Telnet JSON output could not create its stop event.";
		Stop();
		return false;
	}
	InterlockedExchange(&state_->running, 1);
	const uintptr_t thread = _beginthreadex(NULL, 0, AcceptWorker, state_, 0, NULL);
	state_->thread = reinterpret_cast<HANDLE>(thread);
	if (!state_->thread)
	{
		InterlockedExchange(&state_->running, 0);
		error = "Telnet JSON output could not start its listener worker.";
		Stop();
		return false;
	}
	SetStatus(state_, "Telnet JSON output is listening.");
	return true;
}

void TelnetOutputServer::Stop()
{
	if (!state_) return;
	if (state_->stopEvent) SetEvent(state_->stopEvent);
	SOCKET listener = state_->listener;
	CloseSocket(listener);
	if (state_->thread)
	{
		WaitForSingleObject(state_->thread, 10000);
		CloseHandle(state_->thread);
		state_->thread = NULL;
	}
	state_->listener = INVALID_SOCKET;
	EnterCriticalSection(&state_->lock);
	for (std::vector<SOCKET>::iterator client = state_->clients.begin(); client != state_->clients.end(); ++client)
		CloseSocket(*client);
	state_->clients.clear();
	state_->status = "Telnet JSON output is stopped.";
	LeaveCriticalSection(&state_->lock);
	if (state_->stopEvent) CloseHandle(state_->stopEvent);
	state_->stopEvent = NULL;
	state_->boundPort = 0;
	InterlockedExchange(&state_->running, 0);
	if (state_->winsock) WSACleanup();
	state_->winsock = false;
}

bool TelnetOutputServer::Broadcast(const std::string& jsonLine)
{
	if (!IsRunning()) return false;
	std::string wire(jsonLine);
	while (!wire.empty() && (wire[wire.size() - 1] == '\r' || wire[wire.size() - 1] == '\n'))
		wire.erase(wire.size() - 1);
	wire += "\r\n";
	bool delivered = false;
	EnterCriticalSection(&state_->lock);
	for (std::vector<SOCKET>::iterator client = state_->clients.begin(); client != state_->clients.end();)
	{
		const int sent = send(*client, wire.data(), static_cast<int>(wire.size()), 0);
		if (sent == static_cast<int>(wire.size()))
		{
			delivered = true;
			++client;
		}
		else
		{
			CloseSocket(*client);
			client = state_->clients.erase(client);
		}
	}
	state_->status = delivered ? "Telnet JSON message delivered." :
		(state_->clients.empty() ? "Telnet JSON output is listening; no clients are connected." :
		 "Telnet JSON clients could not accept the message.");
	LeaveCriticalSection(&state_->lock);
	return delivered;
}

bool TelnetOutputServer::IsRunning() const
{
	return state_ && InterlockedCompareExchange(&state_->running, 0, 0) != 0;
}

unsigned short TelnetOutputServer::BoundPort() const
{
	return state_ ? state_->boundPort : 0;
}

std::size_t TelnetOutputServer::ClientCount() const
{
	if (!state_) return 0;
	EnterCriticalSection(&state_->lock);
	const std::size_t count = state_->clients.size();
	LeaveCriticalSection(&state_->lock);
	return count;
}

std::string TelnetOutputServer::Status() const
{
	if (!state_) return "Telnet JSON output is unavailable.";
	EnterCriticalSection(&state_->lock);
	const std::string status(state_->status);
	LeaveCriticalSection(&state_->lock);
	return status;
}

} // namespace outputs
} // namespace pdw

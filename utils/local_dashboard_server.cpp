#include "local_dashboard_server.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include "headers\message_archive_manager.h"

namespace pdw
{
namespace dashboard
{
namespace
{
	bool SendAll(SOCKET client, const char* data, std::size_t size)
	{
		std::size_t sent = 0;
		while (sent < size)
		{
			const int result = send(client, data + sent,
				static_cast<int>((std::min)(size - sent, static_cast<std::size_t>(16384))), 0);
			if (result <= 0) return false;
			sent += static_cast<std::size_t>(result);
		}
		return true;
	}

	void SendResponse(SOCKET client, int status, const char* statusText,
		const char* contentType, const std::string& body)
	{
		std::ostringstream header;
		header << "HTTP/1.1 " << status << ' ' << statusText << "\r\n"
			<< "Content-Type: " << contentType << "\r\n"
			<< "Content-Length: " << body.size() << "\r\n"
			<< "Cache-Control: no-store\r\n"
			<< "X-Content-Type-Options: nosniff\r\n"
			<< "X-Frame-Options: DENY\r\n"
			<< "Cross-Origin-Resource-Policy: same-origin\r\n"
			<< "Referrer-Policy: no-referrer\r\n"
			<< "Content-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'\r\n"
			<< "Connection: close\r\n\r\n";
		const std::string head = header.str();
		SendAll(client, head.data(), head.size());
		SendAll(client, body.data(), body.size());
	}

	int IntegerQuery(const std::string& query, const char* name, int defaultValue,
		int minimum, int maximum)
	{
		const std::string text = QueryValue(query, name);
		if (text.empty()) return defaultValue;
		char* end = NULL;
		const long value = std::strtol(text.c_str(), &end, 10);
		if (!end || *end || value < minimum || value > maximum) return defaultValue;
		return static_cast<int>(value);
	}
}

struct LocalDashboardServer::State
{
	CRITICAL_SECTION lock;
	SOCKET listener;
	SOCKET activeClient;
	HANDLE stopEvent;
	HANDLE thread;
	volatile LONG running;
	unsigned short boundPort;
	bool socketsStarted;
	std::string status;

	State() : listener(INVALID_SOCKET), activeClient(INVALID_SOCKET), stopEvent(NULL), thread(NULL), running(0),
		boundPort(0), socketsStarted(false), status("Local dashboard is stopped.")
	{
		InitializeCriticalSection(&lock);
	}

	~State()
	{
		DeleteCriticalSection(&lock);
	}
};

bool ParseRequestTarget(const std::string& request, std::string& path,
	std::string& query)
{
	path.clear();
	query.clear();
	const std::size_t lineEnd = request.find("\r\n");
	const std::string firstLine = request.substr(0, lineEnd);
	if (firstLine.compare(0, 4, "GET ") != 0) return false;
	const std::size_t targetEnd = firstLine.find(' ', 4);
	if (targetEnd == std::string::npos) return false;
	const std::string target = firstLine.substr(4, targetEnd - 4);
	if (target.empty() || target[0] != '/' || target.size() > 2048) return false;
	const std::size_t question = target.find('?');
	path = target.substr(0, question);
	if (question != std::string::npos) query = target.substr(question + 1);
	return path.find("..") == std::string::npos;
}

bool HasAllowedHostHeader(const std::string& request, unsigned short boundPort)
{
	const std::size_t firstLineEnd = request.find("\r\n");
	if (firstLineEnd == std::string::npos) return false;
	std::size_t lineStart = firstLineEnd + 2;
	bool foundHost = false;
	while (lineStart < request.size())
	{
		const std::size_t lineEnd = request.find("\r\n", lineStart);
		if (lineEnd == std::string::npos) return false;
		if (lineEnd == lineStart) break;
		const std::string line = request.substr(lineStart, lineEnd - lineStart);
		const std::size_t colon = line.find(':');
		if (colon == std::string::npos) return false;
		std::string name = line.substr(0, colon);
		for (std::size_t index = 0; index < name.size(); ++index)
			name[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[index])));
		if (name == "host")
		{
			if (foundHost) return false;
			foundHost = true;
			std::size_t valueStart = colon + 1;
			while (valueStart < line.size() && (line[valueStart] == ' ' || line[valueStart] == '\t')) ++valueStart;
			std::size_t valueEnd = line.size();
			while (valueEnd > valueStart && (line[valueEnd - 1] == ' ' || line[valueEnd - 1] == '\t')) --valueEnd;
			std::string value = line.substr(valueStart, valueEnd - valueStart);
			for (std::size_t index = 0; index < value.size(); ++index)
				value[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[index])));

			std::string host = value;
			std::string portText;
			const std::size_t portSeparator = value.rfind(':');
			if (portSeparator != std::string::npos)
			{
				host = value.substr(0, portSeparator);
				portText = value.substr(portSeparator + 1);
			}
			if (host != "localhost" && host != "127.0.0.1") return false;
			if (!portText.empty())
			{
				char* end = NULL;
				const unsigned long port = std::strtoul(portText.c_str(), &end, 10);
				if (!end || *end || port != boundPort) return false;
			}
			else if (portSeparator != std::string::npos) return false;
		}
		lineStart = lineEnd + 2;
	}
	return foundHost;
}

std::string UrlDecode(const std::string& value)
{
	std::string result;
	result.reserve(value.size());
	for (std::size_t index = 0; index < value.size(); ++index)
	{
		if (value[index] == '+') result += ' ';
		else if (value[index] == '%' && index + 2 < value.size() &&
			std::isxdigit(static_cast<unsigned char>(value[index + 1])) &&
			std::isxdigit(static_cast<unsigned char>(value[index + 2])))
		{
			const std::string hex = value.substr(index + 1, 2);
			result += static_cast<char>(std::strtoul(hex.c_str(), NULL, 16));
			index += 2;
		}
		else result += value[index];
	}
	return result;
}

std::string QueryValue(const std::string& query, const std::string& name)
{
	std::size_t start = 0;
	while (start <= query.size())
	{
		const std::size_t end = query.find('&', start);
		const std::string part = query.substr(start,
			end == std::string::npos ? std::string::npos : end - start);
		const std::size_t equals = part.find('=');
		if (UrlDecode(part.substr(0, equals)) == name)
			return equals == std::string::npos ? std::string() : UrlDecode(part.substr(equals + 1));
		if (end == std::string::npos) break;
		start = end + 1;
	}
	return std::string();
}

std::string BuildDashboardHtml()
{
	return
		"<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>PDW Live Dashboard</title><style>"
		":root{color-scheme:light dark;font-family:Segoe UI,system-ui,sans-serif}body{margin:0;background:#0b1220;color:#e5eefc}"
		"header{position:sticky;top:0;background:#111b2e;padding:16px 20px;border-bottom:1px solid #2a3954;z-index:2}"
		"h1{font-size:20px;margin:0 0 10px}.controls{display:flex;gap:8px;flex-wrap:wrap}input,select,button{font:inherit;padding:8px 10px;border-radius:6px;border:1px solid #415273;background:#15223a;color:inherit}"
		"input{min-width:240px}button{cursor:pointer;background:#1f6feb;border-color:#1f6feb}main{padding:14px 20px}.status{margin:0 0 10px;color:#a9bad7}"
		"table{width:100%;border-collapse:collapse;background:#111b2e}th,td{padding:9px;border-bottom:1px solid #263754;text-align:left;vertical-align:top}th{position:sticky;top:103px;background:#16233b}"
		".cap{font-variant-numeric:tabular-nums;white-space:nowrap}.name{font-weight:600}.muted{color:#9aacc8}.filtered{border-left:3px solid #22c55e}.message{white-space:pre-wrap;word-break:break-word}"
		"@media(max-width:760px){.hide-small{display:none}header{position:static}th{position:static}main{padding:8px}input{min-width:150px}}"
		"</style></head><body><header><h1>PDW Live Dashboard</h1><div class=\"controls\">"
		"<input id=\"q\" placeholder=\"Search capcode, name, agency or message\"><select id=\"protocol\"><option value=\"\">All protocols</option><option>POCSAG</option><option>FLEX</option><option>ERMES</option><option>ACARS</option><option>MOBITEX</option></select>"
		"<label><input id=\"filtered\" type=\"checkbox\"> Filtered only</label><button id=\"refresh\">Refresh</button></div></header>"
		"<main><p class=\"status\" id=\"status\">Connecting to PDW...</p><table><thead><tr><th>Received</th><th>Protocol</th><th>Capcode</th><th>Name / agency</th><th class=\"hide-small\">Type</th><th>Message</th></tr></thead><tbody id=\"rows\"></tbody></table></main>"
		"<script>const e=s=>document.getElementById(s);const esc=v=>String(v??'').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));"
		"async function load(){const p=new URLSearchParams({limit:'250',q:e('q').value,protocol:e('protocol').value,filtered:e('filtered').checked?'1':'0'});try{const r=await fetch('/api/v1/messages?'+p);if(!r.ok)throw Error(r.status);const d=await r.json();e('status').textContent=d.total+' matching messages - local read-only feed - refreshes every 3 seconds';e('rows').innerHTML=d.events.map(x=>'<tr class=\"'+(x.filtered?'filtered':'')+'\"><td>'+esc(x.timestamp)+'</td><td>'+esc(x.mode)+'</td><td class=\"cap\">'+esc(x.address)+'</td><td><span class=\"name\">'+esc(x.address_name||'')+'</span><br><span class=\"muted\">'+esc(x.agency||'')+'</span></td><td class=\"hide-small\">'+esc(x.message_type)+'</td><td class=\"message\">'+esc(x.message)+'</td></tr>').join('');}catch(err){e('status').textContent='PDW dashboard unavailable: '+err;}}"
		"let timer;function soon(){clearTimeout(timer);timer=setTimeout(load,250)}e('q').addEventListener('input',soon);e('protocol').addEventListener('change',load);e('filtered').addEventListener('change',load);e('refresh').addEventListener('click',load);load();setInterval(load,3000);</script></body></html>";
}

namespace
{
	DWORD WINAPI ServerThread(LPVOID argument)
	{
		LocalDashboardServer::State* state = static_cast<LocalDashboardServer::State*>(argument);
		while (WaitForSingleObject(state->stopEvent, 0) != WAIT_OBJECT_0)
		{
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(state->listener, &readSet);
			timeval timeout = {0, 250000};
			const int ready = select(0, &readSet, NULL, NULL, &timeout);
			if (ready <= 0) continue;
			SOCKET client = accept(state->listener, NULL, NULL);
			if (client == INVALID_SOCKET) continue;
			EnterCriticalSection(&state->lock);
			state->activeClient = client;
			LeaveCriticalSection(&state->lock);
			DWORD receiveTimeout = 3000;
			DWORD sendTimeout = 3000;
			setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
				reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));
			setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
				reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout));
			std::string request;
			char buffer[2048];
			while (request.size() < 8192 && request.find("\r\n\r\n") == std::string::npos)
			{
				const int count = recv(client, buffer, sizeof(buffer), 0);
				if (count <= 0) break;
				request.append(buffer, count);
			}
			std::string path, query;
			if (!ParseRequestTarget(request, path, query))
				SendResponse(client, 400, "Bad Request", "text/plain; charset=utf-8", "Invalid read-only dashboard request.\n");
			else if (!HasAllowedHostHeader(request, state->boundPort))
				SendResponse(client, 403, "Forbidden", "text/plain; charset=utf-8", "The dashboard only accepts a local Host header.\n");
			else if (path == "/" || path == "/index.html")
				SendResponse(client, 200, "OK", "text/html; charset=utf-8", BuildDashboardHtml());
			else if (path == "/api/v1/messages")
			{
				const int limit = IntegerQuery(query, "limit", 200, 1, 500);
				const int offset = IntegerQuery(query, "offset", 0, 0, 100000000);
				const bool filtered = QueryValue(query, "filtered") == "1";
				SendResponse(client, 200, "OK", "application/json; charset=utf-8",
					MessageArchiveBuildMessagesJson(limit, offset, QueryValue(query, "q"),
						QueryValue(query, "protocol"), filtered));
			}
			else if (path == "/api/v1/capcodes")
				SendResponse(client, 200, "OK", "application/json; charset=utf-8",
					MessageArchiveBuildCapcodesJson(QueryValue(query, "q")));
			else if (path == "/health")
				SendResponse(client, 200, "OK", "application/json; charset=utf-8", "{\"status\":\"ok\",\"local_only\":true}\n");
			else SendResponse(client, 404, "Not Found", "text/plain; charset=utf-8", "Not found.\n");
			EnterCriticalSection(&state->lock);
			if (state->activeClient == client) state->activeClient = INVALID_SOCKET;
			LeaveCriticalSection(&state->lock);
			shutdown(client, SD_BOTH);
			closesocket(client);
		}
		InterlockedExchange(&state->running, 0);
		return 0;
	}
}

LocalDashboardServer::LocalDashboardServer() : state_(new State) {}

LocalDashboardServer::~LocalDashboardServer()
{
	Stop();
	delete state_;
}

bool LocalDashboardServer::Start(unsigned short port, std::string& error)
{
	error.clear();
	if (!state_) { error = "Local dashboard state is unavailable."; return false; }
	if (IsRunning())
	{
		if (BoundPort() == port) return true;
		Stop();
	}
	if (port == 0) { error = "Local dashboard port must be between 1 and 65535."; return false; }
	WSADATA sockets = {};
	if (WSAStartup(MAKEWORD(2, 2), &sockets) != 0)
	{
		error = "Windows Sockets could not start the local dashboard.";
		return false;
	}
	state_->socketsStarted = true;
	state_->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (state_->listener == INVALID_SOCKET)
	{
		error = "The local dashboard could not create its listener.";
		WSACleanup();
		state_->socketsStarted = false;
		return false;
	}
	BOOL exclusive = TRUE;
	setsockopt(state_->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
	sockaddr_in endpoint = {};
	endpoint.sin_family = AF_INET;
	endpoint.sin_port = htons(port);
	inet_pton(AF_INET, "127.0.0.1", &endpoint.sin_addr);
	if (bind(state_->listener, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == SOCKET_ERROR ||
		listen(state_->listener, SOMAXCONN) == SOCKET_ERROR)
	{
		error = "The local dashboard could not bind 127.0.0.1 on the selected port.";
		closesocket(state_->listener);
		state_->listener = INVALID_SOCKET;
		WSACleanup();
		state_->socketsStarted = false;
		return false;
	}
	state_->stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!state_->stopEvent)
	{
		error = "The local dashboard could not create its stop event.";
		closesocket(state_->listener);
		state_->listener = INVALID_SOCKET;
		WSACleanup();
		state_->socketsStarted = false;
		return false;
	}
	state_->boundPort = port;
	InterlockedExchange(&state_->running, 1);
	state_->thread = CreateThread(NULL, 0, ServerThread, state_, 0, NULL);
	if (!state_->thread)
	{
		error = "The local dashboard could not start its listener worker.";
		InterlockedExchange(&state_->running, 0);
		CloseHandle(state_->stopEvent);
		state_->stopEvent = NULL;
		closesocket(state_->listener);
		state_->listener = INVALID_SOCKET;
		WSACleanup();
		state_->socketsStarted = false;
		return false;
	}
	EnterCriticalSection(&state_->lock);
	state_->status = "Local dashboard is listening on 127.0.0.1.";
	LeaveCriticalSection(&state_->lock);
	return true;
}

void LocalDashboardServer::RequestStop()
{
	if (!state_) return;
	if (state_->stopEvent) SetEvent(state_->stopEvent);
	if (state_->listener != INVALID_SOCKET) shutdown(state_->listener, SD_BOTH);
	EnterCriticalSection(&state_->lock);
	if (state_->activeClient != INVALID_SOCKET) shutdown(state_->activeClient, SD_BOTH);
	LeaveCriticalSection(&state_->lock);
}

void LocalDashboardServer::Stop()
{
	if (!state_) return;
	RequestStop();
	if (state_->thread)
	{
		// Receive/send timeouts and SQLite's busy timeout bound all work in the
		// server thread. Join before releasing State so shutdown cannot race a
		// request that is still using its sockets or critical section.
		WaitForSingleObject(state_->thread, INFINITE);
		CloseHandle(state_->thread);
	}
	state_->thread = NULL;
	if (state_->listener != INVALID_SOCKET) closesocket(state_->listener);
	state_->listener = INVALID_SOCKET;
	state_->activeClient = INVALID_SOCKET;
	if (state_->stopEvent) CloseHandle(state_->stopEvent);
	state_->stopEvent = NULL;
	state_->boundPort = 0;
	InterlockedExchange(&state_->running, 0);
	EnterCriticalSection(&state_->lock);
	state_->status = "Local dashboard is stopped.";
	LeaveCriticalSection(&state_->lock);
	if (state_->socketsStarted)
	{
		WSACleanup();
		state_->socketsStarted = false;
	}
}

bool LocalDashboardServer::IsRunning() const
{
	return state_ && InterlockedCompareExchange(&state_->running, 0, 0) != 0;
}

unsigned short LocalDashboardServer::BoundPort() const
{
	return state_ ? state_->boundPort : 0;
}

std::string LocalDashboardServer::Status() const
{
	if (!state_) return "Local dashboard is unavailable.";
	EnterCriticalSection(&state_->lock);
	const std::string status = state_->status;
	LeaveCriticalSection(&state_->lock);
	return status;
}

} // namespace dashboard
} // namespace pdw

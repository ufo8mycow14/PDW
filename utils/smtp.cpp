// 
/*
**	SMTP routines for mailsend - a simple mail sender via SMTP
**
*/

#include <windows.h>
#include <stdio.h>
#include <atomic>
#include <string>
#include "..\headers\pdw.h"
#include "smtp_int.h"
#include "smtp.h"
#include "smtp_message_core.h"
#include "..\utils\debug.h"

#include "openssl\ssl.h"
#include "openssl\err.h"
#include "openssl\x509.h"
#include <wincrypt.h>

#define MY_BUFF_SIZE 4096

static SOCKET smtp_socket = INVALID_SOCKET;
static std::atomic<SOCKET> activeSocket(INVALID_SOCKET);
static char buf[MY_BUFF_SIZE];

static HANDLE MailThread = NULL;
static HANDLE MailWakeEvent = NULL;
static THEMAIL mail ;
static int nMaxLen ;
static std::atomic<bool> keepbusy(true);
static BOOL bWsaStartup ;
static std::atomic<HWND> responseWindow(NULL);

#define MAX_MAIL		100
#define MAX_MAIL_LEN	(MAX_STR_LEN + 1024)

static char szMailBuffer[MAX_MAIL][MAX_MAIL_LEN] ;
static int  nBufferdMailStart ;
static int  nBufferdMailEnd ;
static int  nBufferedMailCount ;
static unsigned int nDroppedMailCount ;
static SRWLOCK MailQueueLock = SRWLOCK_INIT;

static byte dtable[256];

extern int nSMTPerrors;
extern int iSMTPlastError;

//SSL
SSL_CTX*      m_ctx = NULL;
SSL*          m_ssl = NULL;

// The hostname is retained per connection so OpenSSL can send SNI and verify
// the certificate against the same name the user configured.
static char g_szTlsHostname[256] = "";


char *szSmtpCharSets[] = {
	"us-ascii     (Standard)",
	"iso-8859-1   (West European)",
	"iso-8859-2   (East European)",
	"iso-8859-3   (South European)", 
	"iso-8859-4   (North European)",
	"iso-8859-5   (Cyrillic)",
	"iso-8859-6   (Arabic)", 
	"iso-8859-7   (Greek)", 
	"iso-8859-8   (Hebrew)",
	"iso-8859-9   (Turkish)",
	"iso-8859-10  (Nordic)",
	"iso-2022-kr  (Korean)",
	"KOI8-R       (Russian)",
	"EUC-KR       (Korean)",
	"Shift_JIS    (Japanese)",
	"ISO-2022-JP  (Japanese)",
	"EUC-JP       (Japanese)",
	"GB2312       (Chinese)",
	"Big5         (Traditional Chinese)",
	"windows-1250 (Central Europ Windows)",
	"windows-1251 (Cyrillic Windows)",
	"windows-1252 (Western Europ Windows)",
	"windows-1253 (Greek Windows)",
	"windows-1254 (Turkish (Windows)",
	"windows-1255 (Hebrew Windows)",
	"windows-1256 (Arabic Windows)",
	"windows-1257 (Baltic Windows)",
	"windows-1258 (Vietnamese Windows)"
} ;

enum SSLError
{
	CSMTP_NO_ERROR = 0,
	WSA_STARTUP = 100, // WSAGetLastError()
	WSA_VER,
	WSA_SEND,
	WSA_RECV,
	WSA_CONNECT,
	WSA_GETHOSTBY_NAME_ADDR,
	WSA_INVALID_SOCKET,
	WSA_HOSTNAME,
	WSA_IOCTLSOCKET,
	WSA_SELECT,
	BAD_IPV4_ADDR,
	UNDEF_MSG_HEADER = 200,
	UNDEF_MAIL_FROM,
	UNDEF_SUBJECT,
	UNDEF_RECIPIENTS,
	UNDEF_LOGIN,
	UNDEF_PASSWORD,
	BAD_LOGIN_PASSWORD,
	BAD_DIGEST_RESPONSE,
	BAD_SERVER_NAME,
	UNDEF_RECIPIENT_MAIL,
	COMMAND_MAIL_FROM = 300,
	COMMAND_EHLO,
	COMMAND_AUTH_PLAIN,
	COMMAND_AUTH_LOGIN,
	COMMAND_AUTH_CRAMMD5,
	COMMAND_AUTH_DIGESTMD5,
	COMMAND_DIGESTMD5,
	COMMAND_DATA,
	COMMAND_QUIT,
	COMMAND_RCPT_TO,
	MSG_BODY_ERROR,
	CONNECTION_CLOSED = 400, // by server
	SERVER_NOT_READY, // remote server
	SERVER_NOT_RESPONDING,
	SELECT_TIMEOUT,
	FILE_NOT_EXIST,
	MSG_TOO_BIG,
	BAD_LOGIN_PASS,
	UNDEF_XYZ_RESPONSE,
	LACK_OF_MEMORY,
	TIME_ERROR,
	RECVBUF_IS_EMPTY,
	SENDBUF_IS_EMPTY,
	OUT_OF_MSG_RANGE,
	COMMAND_EHLO_STARTTLS,
	SSL_PROBLEM,
	COMMAND_DATABLOCK,
	STARTTLS_NOT_SUPPORTED,
	LOGIN_NOT_SUPPORTED
};

int initOpenSSL()
{
	SSL_library_init();
	SSL_load_error_strings();
	m_ctx = SSL_CTX_new (TLS_client_method());
	if(m_ctx == NULL)
		return SSL_PROBLEM;

	if (SSL_CTX_set_min_proto_version(m_ctx, TLS1_2_VERSION) != 1)
	{
		SSL_CTX_free(m_ctx);
		m_ctx = NULL;
		return SSL_PROBLEM;
	}

	SSL_CTX_set_verify(m_ctx, SSL_VERIFY_PEER, NULL);
	SSL_CTX_set_default_verify_paths(m_ctx);

	// The bundled OpenSSL build does not automatically use Windows' trusted
	// root store. Import it so TLS keeps the same trust decisions as Windows.
	HCERTSTORE hStore = CertOpenSystemStoreA(0, "ROOT");
	if (hStore)
	{
		X509_STORE *x509Store = SSL_CTX_get_cert_store(m_ctx);
		PCCERT_CONTEXT certificate = NULL;
		while ((certificate = CertEnumCertificatesInStore(hStore, certificate)) != NULL)
		{
			const unsigned char *encoded = certificate->pbCertEncoded;
			X509 *x509 = d2i_X509(NULL, &encoded, (long)certificate->cbCertEncoded);
			if (x509)
			{
				if (X509_STORE_add_cert(x509Store, x509) != 1)
					ERR_clear_error(); // duplicate Windows roots are harmless
				X509_free(x509);
			}
		}
		CertCloseStore(hStore, 0);
	}

	return CSMTP_NO_ERROR;
}


#define TIME_IN_SEC		30	// bounded TLS handshake wait

int openSSLConnect()
{
	if(m_ctx == NULL)
		return SSL_PROBLEM;

	m_ssl = SSL_new (m_ctx);   
	if(m_ssl == NULL)
		return SSL_PROBLEM;

	SSL_set_fd (m_ssl, (int)smtp_socket);
	SSL_set_mode(m_ssl, SSL_MODE_AUTO_RETRY);

	if (g_szTlsHostname[0])
	{
		const unsigned long ipv4 = inet_addr(g_szTlsHostname);
		if (ipv4 != INADDR_NONE)
		{
			X509_VERIFY_PARAM *verifyParameters = SSL_get0_param(m_ssl);
			if (!verifyParameters ||
				X509_VERIFY_PARAM_set1_ip_asc(verifyParameters, g_szTlsHostname) != 1)
			{
				SSL_free(m_ssl);
				m_ssl = NULL;
				return SSL_PROBLEM;
			}
		}
		else
		{
			if (SSL_set_tlsext_host_name(m_ssl, g_szTlsHostname) != 1 ||
				SSL_set1_host(m_ssl, g_szTlsHostname) != 1)
			{
				SSL_free(m_ssl);
				m_ssl = NULL;
				return SSL_PROBLEM;
			}
		}
	}

	int res = 0;
	fd_set fdwrite;
	fd_set fdread;
	int write_blocked = 0;
	int read_blocked = 0;

	while(1)
	{
		FD_ZERO(&fdwrite);
		FD_ZERO(&fdread);

		if(write_blocked)
			FD_SET(smtp_socket, &fdwrite);
		if(read_blocked)
			FD_SET(smtp_socket, &fdread);

		if(write_blocked || read_blocked)
		{
			timeval time;
			time.tv_sec = TIME_IN_SEC;
			time.tv_usec = 0;
			write_blocked = 0;
			read_blocked = 0;
			if((res = select(smtp_socket+1,&fdread,&fdwrite,NULL,&time)) == SOCKET_ERROR)
			{
				FD_ZERO(&fdwrite);
				FD_ZERO(&fdread);
				return WSA_SELECT;
			}
			if(!res)
			{
				//timeout
				FD_ZERO(&fdwrite);
				FD_ZERO(&fdread);
				return SERVER_NOT_RESPONDING;
			}
		}
		res = SSL_connect(m_ssl);
		switch(SSL_get_error(m_ssl, res))
		{
		case SSL_ERROR_NONE:
			FD_ZERO(&fdwrite);
			FD_ZERO(&fdread);
			if (SSL_get_verify_result(m_ssl) != X509_V_OK)
				return SSL_PROBLEM;
			return CSMTP_NO_ERROR;
			break;

		case SSL_ERROR_WANT_WRITE:
			write_blocked = 1;
			break;

		case SSL_ERROR_WANT_READ:
			read_blocked = 1;
			break;

		default:	      
			FD_ZERO(&fdwrite);
			FD_ZERO(&fdread);
			return SSL_PROBLEM;
		}
	}

	return CSMTP_NO_ERROR;
}


void cleanupOpenSSL()
{
	if(m_ssl != NULL)
	{
		SSL_shutdown (m_ssl);  /* send SSL/TLS close_notify */
		SSL_free (m_ssl);
		m_ssl = NULL;
	}
	if(m_ctx != NULL)
	{
		SSL_CTX_free (m_ctx);	
		m_ctx = NULL;
	}
}


#define SEND_RECIEVE_TO 30

int receiveData_SSL(SSL* ssl, char* buf)
{
	int res = 0;
	int offset = 0;
	fd_set fdread;
	fd_set fdwrite;
	int read_blocked_on_write = 0;

	if(buf == NULL)
		return RECVBUF_IS_EMPTY;

	bool bFinish = false;

	while(!bFinish)
	{
		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);

		FD_SET(smtp_socket,&fdread);

		if(read_blocked_on_write)
		{
			FD_SET(smtp_socket, &fdwrite);
		}

		timeval time;
		time.tv_sec = SEND_RECIEVE_TO;
		time.tv_usec = 0;
		if((res = select(smtp_socket+1, &fdread, &fdwrite, NULL, &time)) == SOCKET_ERROR)
		{
			FD_ZERO(&fdread);
			FD_ZERO(&fdwrite);
			return WSA_SELECT;
		}

		if(!res)
		{
			//timeout
			FD_ZERO(&fdread);
			FD_ZERO(&fdwrite);
			return SERVER_NOT_RESPONDING;
		}

		if(FD_ISSET(smtp_socket,&fdread) || (read_blocked_on_write && FD_ISSET(smtp_socket,&fdwrite)) )
		{
			while(1)
			{
				read_blocked_on_write=0;

				const int buff_len = 1024;
				char buff[buff_len];

				res = SSL_read(ssl, buff, buff_len);

				int ssl_err = SSL_get_error(ssl, res);
				if(ssl_err == SSL_ERROR_NONE)
				{
					if(offset + res > MY_BUFF_SIZE - 1)
					{
						FD_ZERO(&fdread);
						FD_ZERO(&fdwrite);
						return LACK_OF_MEMORY;
					}
					memcpy(buf + offset, buff, res);
					offset += res;
					if(SSL_pending(ssl))
					{
						continue;
					}
					else
					{
						bFinish = true;
						break;
					}
				}
				else if(ssl_err == SSL_ERROR_ZERO_RETURN)
				{
					bFinish = true;
					break;
				}
				else if(ssl_err == SSL_ERROR_WANT_READ)
				{
					break;
				}
				else if(ssl_err == SSL_ERROR_WANT_WRITE)
				{
					/* We get a WANT_WRITE if we're
					trying to rehandshake and we block on
					a write during that rehandshake.

					We need to wait on the socket to be 
					writeable but reinitiate the read
					when it is */
					read_blocked_on_write=1;
					break;
				}
				else
				{
					FD_ZERO(&fdread);
					FD_ZERO(&fdwrite);
					return SSL_PROBLEM;
				}
			}
		}
	}

	FD_ZERO(&fdread);
	FD_ZERO(&fdwrite);
	buf[offset] = 0;
	if(offset == 0)
	{
		return CONNECTION_CLOSED;
	}

	return CSMTP_NO_ERROR;
}

int sendData_SSL(SSL* ssl, char *buf)
{
	if(buf == NULL)
		return SENDBUF_IS_EMPTY;

	int offset = 0,res,nLeft = (int)strlen(buf);
	fd_set fdwrite;
	fd_set fdread;

	int write_blocked_on_read = 0;

	while(nLeft > 0)
	{
		FD_ZERO(&fdwrite);
		FD_ZERO(&fdread);

		FD_SET(smtp_socket,&fdwrite);

		if(write_blocked_on_read)
		{
			FD_SET(smtp_socket, &fdread);
		}

		timeval time;
		time.tv_sec = SEND_RECIEVE_TO;
		time.tv_usec = 0;
		if((res = select(smtp_socket+1,&fdread,&fdwrite,NULL,&time)) == SOCKET_ERROR)
		{
			FD_ZERO(&fdwrite);
			FD_ZERO(&fdread);
			return WSA_SELECT;
		}

		if(!res)
		{
			//timeout
			FD_ZERO(&fdwrite);
			FD_ZERO(&fdread);
			return SERVER_NOT_RESPONDING;
		}

		if(FD_ISSET(smtp_socket,&fdwrite) || (write_blocked_on_read && FD_ISSET(smtp_socket, &fdread)) )
		{
			write_blocked_on_read=0;

			/* Try to write */
			res = SSL_write(ssl, buf+offset, nLeft);
	          
			switch(SSL_get_error(ssl,res))
			{
			  /* We wrote something*/
			  case SSL_ERROR_NONE:
				nLeft -= res;
				offset += res;
				break;
	              
				/* We would have blocked */
			  case SSL_ERROR_WANT_WRITE:
				break;

				/* We get a WANT_READ if we're
				   trying to rehandshake and we block on
				   write during the current connection.
	               
				   We need to wait on the socket to be readable
				   but reinitiate our write when it is */
			  case SSL_ERROR_WANT_READ:
				write_blocked_on_read=1;
				break;
	              
				  /* Some other error */
			  default:	      
				FD_ZERO(&fdread);
				FD_ZERO(&fdwrite);
				return SSL_PROBLEM;
			}

		}
	}

	FD_ZERO(&fdwrite);
	FD_ZERO(&fdread);

	return CSMTP_NO_ERROR;
}
	
char *EncodeBase64(char *szIn, char *szOut)
{
	char *pIn = szIn, *pOut = szOut ;
	int i,hiteof= FALSE;

	for(i= 0;i<9;i++){
		dtable[i]= 'A'+i;
		dtable[i+9]= 'J'+i;
		dtable[26+i]= 'a'+i;
		dtable[26+i+9]= 'j'+i;
	}
	for(i= 0;i<8;i++){
		dtable[i+18]= 'S'+i;
		dtable[26+i+18]= 's'+i;
	}
	for(i= 0;i<10;i++){
		dtable[52+i]= '0'+i;
	}
	dtable[62]= '+';
	dtable[63]= '/';


	while(!hiteof){
		byte igroup[3],ogroup[4];
		int c,n;
	
		igroup[0]= igroup[1]= igroup[2]= 0;
		for(n= 0;n<3;n++){
			c = *pIn++;
			if(!c){
				hiteof= TRUE;
				break;
			}
			igroup[n]= (byte)c;
		}
		if(n> 0){
			ogroup[0]= dtable[igroup[0]>>2];
			ogroup[1]= dtable[((igroup[0]&3)<<4)|(igroup[1]>>4)];
			ogroup[2]= dtable[((igroup[1]&0xF)<<2)|(igroup[2]>>6)];
			ogroup[3]= dtable[igroup[2]&0x3F];

			if(n<3){
				ogroup[3]= '=';
				if(n<2){
					ogroup[2]= '=';
				}
			}
			for(i= 0;i<4;i++){
				*pOut++ = ogroup[i];
			}
		}
	}
	*pOut = '\0' ;

	// Authentication values must never be echoed to the debugger or response UI.
	OUTPUTDEBUGMSG((("EncodeBase64(): encoded credential\n")));
	return(szOut) ;
}

char *DecodeBase64(char *szIn, char *szOut)
{
	int i, j;
	char *pIn = szIn, *pOut = szOut ;

	for(i= 0;i<256;i++){
		dtable[i]= 0x80;
	}
	for(i= 'A';i<='I';i++){
		dtable[i]= 0+(i-'A');
	}
	for(i= 'J';i<='R';i++){
		dtable[i]= 9+(i-'J');
	}
	for(i= 'S';i<='Z';i++){
		dtable[i]= 18+(i-'S');
	}
	for(i= 'a';i<='i';i++){
		dtable[i]= 26+(i-'a');
	}
	for(i= 'j';i<='r';i++){
		dtable[i]= 35+(i-'j');
	}
	for(i= 's';i<='z';i++){
		dtable[i]= 44+(i-'s');
	}
	for(i= '0';i<='9';i++){
		dtable[i]= 52+(i-'0');
	}
	dtable['+']= 62;
	dtable['/']= 63;
	dtable['=']= 0;

	while(TRUE){
		byte a[4],b[4],o[3];
		
		for(i = 0; i < 4; i++){
			int c = *pIn++;		
			if(!c){
				if(i> 0){
					OUTPUTDEBUGMSG((("DecodeBase64(): Input line incomplete.\n")));
				}
				*pOut = '\0'  ;
				OUTPUTDEBUGMSG((("DecodeBase64(): In >%s< out >%s< \n"),szIn, szOut));
				return(szOut);
			}
			if(c < 0 || dtable[(unsigned char)c]&0x80){
				OUTPUTDEBUGMSG((("DecodeBase64(): Illegal character '%c' in input line.\n"),c));
				i--;
				continue;
			}
			a[i]= (byte)c;
			b[i]= (byte)dtable[(unsigned char)c];
		}
		o[0]= (b[0]<<2)|(b[1]>>4);
		o[1]= (b[1]<<4)|(b[2]>>2);
		o[2]= (b[2]<<6)|b[3];
		i = a[2]=='='?1:(a[3]=='='?2:3);

		for(j = 0; j < i; j++) {
			*pOut++ = o[j] ;
		}
		if(i < 3){
			*pOut = '\0'  ;
			OUTPUTDEBUGMSG((("DecodeBase64(): In >%s< out >%s< \n"),szIn, szOut));
			return(szOut);
		}
	}	
}

void AddResponse(char *buf) 
{
	if (!buf)
		return;

	HWND hResponse = responseWindow.load();
	if(hResponse && IsWindow(hResponse))
	{
		// The mail worker must not synchronously deadlock the UI during shutdown.
		// SendMessageTimeout retains the legacy live response list while bounding it.
		const int estimatedWidth = (int)strlen(buf) * 8;
		DWORD_PTR ignored = 0;
		if(estimatedWidth > nMaxLen)
		{
			nMaxLen = estimatedWidth;
			SendMessageTimeoutA(hResponse, LB_SETHORIZONTALEXTENT,
				estimatedWidth, 0, SMTO_ABORTIFHUNG, 250, &ignored);
		}
		SendMessageTimeoutA(hResponse, LB_ADDSTRING, 0, (LPARAM)buf,
			SMTO_ABORTIFHUNG, 250, &ignored);
		OUTPUTDEBUGMSG((("AddResponse() : >>> %s"),buf));
	}
}


struct in_addr *atoAddr(char *address)
{
	static struct in_addr saddr;
	struct hostent *host;
	
	saddr.s_addr=inet_addr(address);
	if(saddr.s_addr != -1) return (&saddr);
	host = gethostbyname(address);
	if(host != (struct hostent *) NULL) {
		return((struct in_addr *) *host->h_addr_list);
	}
	return((struct in_addr *) NULL);
}

int initWinSock(void)
{
	WORD	version_requested;
	WSADATA wsa_data;
	int		err;
	
	if(!bWsaStartup) {
		version_requested=MAKEWORD(2,0);
		err = WSAStartup(version_requested,&wsa_data);
		if(err != 0) {
			OUTPUTDEBUGMSG(((" Unable to initialize winsock (%d)\n"),err));
			AddResponse("Unable to initialize winsock\n");
			bWsaStartup = FALSE ;
			return(-1);
		}
		bWsaStartup = TRUE ;
	}
	return(0);
}

// returns SOCKET on success INVALID_SOCKET on failure
SOCKET clientSocket(char *address,int port)
{
	SOCKET				s;
	struct sockaddr_in	sa;
	struct in_addr		*addr;
	int 				rc;
	
	rc = initWinSock();
	if(rc != 0) {
		OUTPUTDEBUGMSG((("clientSocket() : Error in initWinSock() = 0x%04x\n"), rc));
		AddResponse("clientSocket() : Error in initWinSock()\n");
		return(INVALID_SOCKET);
	}	
	addr = atoAddr(address);
	if(addr == NULL) {
		OUTPUTDEBUGMSG((("clientSocket() : Invalid address: %s\n"),address));
		AddResponse("clientSocket() : Invalid address\n");
		return(INVALID_SOCKET);
	}
	
	memset((char *) &sa,0,sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short) port);
	sa.sin_addr.s_addr = addr->s_addr;
	
	// open the socket
	s = socket(AF_INET,SOCK_STREAM,PF_UNSPEC);
	if(s == INVALID_SOCKET) {
		OUTPUTDEBUGMSG((("clientSocket() : Could not create socket\n")));
		AddResponse("clientSocket() : Could not create socket\n");
		return(INVALID_SOCKET);
	}
	// Bound connection setup so an unreachable server cannot stall shutdown or
	// every later queued page behind the Windows TCP default timeout.
	u_long nonBlocking = 1;
	if (ioctlsocket(s, FIONBIO, &nonBlocking) == SOCKET_ERROR)
	{
		closesocket(s);
		return(INVALID_SOCKET);
	}

	int connectResult = connect(s,(struct sockaddr *) &sa,sizeof(sa));
	if (connectResult == SOCKET_ERROR)
	{
		const int connectError = WSAGetLastError();
		if (connectError != WSAEWOULDBLOCK && connectError != WSAEINPROGRESS)
		{
			OUTPUTDEBUGMSG((("clientSocket() : connect() failed WSAError=%d\n"), connectError));
			AddResponse("clientSocket() : connect() failed\n");
			closesocket(s);
			return(INVALID_SOCKET);
		}

		fd_set writable;
		fd_set exceptional;
		FD_ZERO(&writable);
		FD_ZERO(&exceptional);
		FD_SET(s, &writable);
		FD_SET(s, &exceptional);
		timeval timeout;
		timeout.tv_sec = 10;
		timeout.tv_usec = 0;
		const int selected = select((int)s + 1, NULL, &writable, &exceptional, &timeout);
		int socketError = 0;
		int socketErrorLength = sizeof(socketError);
		if (selected <= 0 || FD_ISSET(s, &exceptional) ||
			getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&socketError, &socketErrorLength) == SOCKET_ERROR ||
			socketError != 0)
		{
			OUTPUTDEBUGMSG((("clientSocket() : connect() timed out or was refused\n")));
			AddResponse("clientSocket() : connect() failed or timed out\n");
			closesocket(s);
			return(INVALID_SOCKET);
		}
	}

	nonBlocking = 0;
	if (ioctlsocket(s, FIONBIO, &nonBlocking) == SOCKET_ERROR)
	{
		closesocket(s);
		return(INVALID_SOCKET);
	}

	DWORD ioTimeout = 30000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ioTimeout, sizeof(ioTimeout));
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ioTimeout, sizeof(ioTimeout));
	return(s);
}




// this function writes a character string out to a socket.
// it returns -1 if the connection is closed while it is trying to write
static int sockWrite(SOCKET sock,char *str,size_t count)
{
	size_t	bytesSent=0 ; 
	int		thisWrite ;
	
	while (bytesSent < count)  {
		thisWrite=send(sock,str,count-bytesSent,0) ;
		if(thisWrite <= 0) {
			return (thisWrite) ;
		}
		bytesSent += thisWrite ; 
		str += thisWrite ;
	}
	return(count) ;
}

int sockPuts(SOCKET sock,char *str)
{
// 	OUTPUTDEBUGMSG((("sockPuts() : %s\n"), str));
	AddResponse(str) ;

	if (m_ssl != NULL)
		return sendData_SSL(m_ssl,str);

	return(sockWrite(sock,str,strlen(str)));
}

static int sockPutsSecret(SOCKET sock, char *str)
{
	if (!str)
		return SENDBUF_IS_EMPTY;
	if (m_ssl != NULL)
		return sendData_SSL(m_ssl, str);
	return sockWrite(sock, str, strlen(str));
}

int sockGets(SOCKET sockfd,char *str,size_t count)
{
	int bytesRead;
	size_t totalCount = 0;
	char buf[1], *currentPosition;
	char lastRead = 0;

	currentPosition = str;
	while(lastRead != 10) {
		bytesRead=recv(sockfd,buf,1,0);
		OUTPUTDEBUGMSG((("%s,%d\n"), __FILE__, bytesRead));
		if(bytesRead <= 0) {
			// the other side may have closed unexpectedly
			OUTPUTDEBUGMSG((("ERRNO:%d\n"), WSAGetLastError()));
			return (-1);
		}
		lastRead=buf[0];
		if((totalCount < count) && (lastRead != 10) && (lastRead != 13)) {
			*currentPosition=lastRead;
			currentPosition++;
			totalCount++;
		}
	}
	if (count > 0) {
		*currentPosition=0;
	}
	return (totalCount);
}


// disconnect to SMTP server and returns the socket fd
static void smtpDisconnect(SOCKET sfd)
{
	cleanupOpenSSL();
	if (sfd != INVALID_SOCKET)
		closesocket(sfd) ;
	activeSocket.store(INVALID_SOCKET);
	smtp_socket = INVALID_SOCKET;
}

static int smtpResponse(int sfd);

enum SmtpTlsMode
{
	SMTP_TLS_NONE,
	SMTP_TLS_IMPLICIT,
	SMTP_TLS_STARTTLS
};

static SmtpTlsMode smtpTlsMode = SMTP_TLS_NONE;

// connect to SMTP server and returns the socket fd
static SOCKET smtpConnect(char *smtp_server,int port)
{
	SOCKET sfd;
	int res = CSMTP_NO_ERROR;
	if (!smtp_server || !smtp_server[0])
		return INVALID_SOCKET;
	
	sfd = clientSocket(smtp_server,port);
	if(sfd == INVALID_SOCKET) {
		OUTPUTDEBUGMSG((("smtpConnect() : Could not connect to SMTP server \"%s\" at port %d\n"), smtp_server,port));
		AddResponse("smtpConnect() : Could not connect to SMTP server\n");

		nSMTPerrors++;	// PH: Counts # Errors

		return (INVALID_SOCKET);
	}
	// save it. we'll need it to clean up
	smtp_socket = sfd;
	activeSocket.store(sfd);
	_snprintf_s(g_szTlsHostname, sizeof(g_szTlsHostname), _TRUNCATE, "%s", smtp_server);

	// Preserve the historical implicit-TLS behaviour on custom ports. Standard
	// submission ports use their conventional mode without adding another UI
	// choice: 465 is implicit TLS; 25/587 upgrade with STARTTLS.
	smtpTlsMode = SMTP_TLS_NONE;
	if (mail.options & MAIL_OPTION_SSL)
	{
		smtpTlsMode = (port == 25 || port == 587) ? SMTP_TLS_STARTTLS : SMTP_TLS_IMPLICIT;
		if (smtpTlsMode == SMTP_TLS_IMPLICIT)
		{
			res = initOpenSSL();
			if (res == CSMTP_NO_ERROR)
				res = openSSLConnect();
			OUTPUTDEBUGMSG(("SSL Connect res = %d\n",res));
			if (res != CSMTP_NO_ERROR)
			{
				AddResponse("smtpConnect() : TLS handshake or certificate verification failed\n");
				smtpDisconnect(sfd);
				nSMTPerrors++;
				return INVALID_SOCKET;
			}
		}
	}

	// Consume and validate the server's 220 greeting before sending EHLO. The
	// legacy implementation read it as the HELO response and shifted every
	// later reply by one command.
	if (smtpResponse((int)sfd) != 0)
	{
		smtpDisconnect(sfd);
		return INVALID_SOCKET;
	}

	return(sfd);
}

// read SMTP response. returns 0 on success, -1 on failure 
static int smtpResponse(int sfd)
{
	int n = 0, err = 0 ;
	char buf[MY_BUFF_SIZE], tmp[MY_BUFF_SIZE] ;

	memset(buf,0,sizeof(buf));

	if (m_ssl != NULL)
	{
		const int receiveResult = receiveData_SSL(m_ssl,buf);
		if (receiveResult != CSMTP_NO_ERROR)
		{
			nSMTPerrors++;
			return -1;
		}
	}
	else
	{
		// Drain a multiline reply through its final "ddd " line so the next
		// command cannot consume a stale EHLO capability line.
		do
		{
			memset(buf,0,sizeof(buf));
			n = sockGets((SOCKET)sfd, buf, sizeof(buf)-1);
			if (n <= 0)
			{
				nSMTPerrors++;
				return -1;
			}
			AddResponse(buf);
		} while (strlen(buf) >= 4 && buf[3] == '-');
	}
//	OUTPUTDEBUGMSG((("smtpResponse() : %s\n"),buf));
	if (m_ssl != NULL)
		AddResponse(buf) ;
	char *responseLine = buf;
	for (char *cursor = buf; *cursor; ++cursor)
	{
		if (*cursor == '\n' && cursor[1] != '\0')
			responseLine = cursor + 1;
	}
	if (responseLine[0] == '\r' && responseLine[1] == '\0')
		responseLine = buf;
	err = atoi(responseLine) ;
	OUTPUTDEBUGMSG((("smtpResponse(): Err: %d!\n"), err));
	if(err == 334) {
		DecodeBase64(responseLine+4, tmp) ;
	}

	if((responseLine[0] == '1' || responseLine[0] == '2' || responseLine[0] == '3') &&
		responseLine[3] == A_SPACE) {
		return (0);
	}
	OUTPUTDEBUGMSG((("smtpResponse(): ERROR!\n")));
	nSMTPerrors++;			// PH: Counts # Errors
	iSMTPlastError = err;	// PH: Last Error

	return (-1);
}

static void smtpBuildHeloArg(char *out, size_t outLength)
{
	if (!out || outLength == 0)
		return;

	if (mail.helo_domain && mail.helo_domain[0] &&
		strcmp(mail.helo_domain, "127.0.0.1") != 0)
	{
		_snprintf_s(out, outLength, _TRUNCATE, "%s", mail.helo_domain);
		return;
	}

	struct sockaddr_in localAddress = {};
	int localAddressLength = sizeof(localAddress);
	if (smtp_socket != INVALID_SOCKET &&
		getsockname(smtp_socket, (struct sockaddr*)&localAddress, &localAddressLength) == 0 &&
		localAddress.sin_family == AF_INET)
	{
		const unsigned char *ip = (const unsigned char*)&localAddress.sin_addr;
		_snprintf_s(out, outLength, _TRUNCATE, "[%u.%u.%u.%u]", ip[0], ip[1], ip[2], ip[3]);
		return;
	}

	_snprintf_s(out, outLength, _TRUNCATE, "[127.0.0.1]");
}

// SMTP: EHLO/HELO and optional STARTTLS upgrade.
static int smtpHelo(int sfd)
{
	char heloArgument[300] = {};
	smtpBuildHeloArg(heloArgument, sizeof(heloArgument));
	_snprintf_s(buf, sizeof(buf), _TRUNCATE, "EHLO %s\r\n", heloArgument);
	sockPuts(sfd,buf);
	if (smtpResponse(sfd) != 0)
	{
		// Keep compatibility with older plaintext servers that only understand
		// HELO. TLS/authenticated sessions require ESMTP and therefore fail cleanly.
		if (smtpTlsMode != SMTP_TLS_NONE || (mail.options & MAIL_OPTION_AUTH))
			return -1;
		_snprintf_s(buf, sizeof(buf), _TRUNCATE, "HELO %s\r\n", heloArgument);
		sockPuts(sfd,buf);
		return smtpResponse(sfd);
	}

	if (smtpTlsMode == SMTP_TLS_STARTTLS && m_ssl == NULL)
	{
		sockPuts(sfd, "STARTTLS\r\n");
		if (smtpResponse(sfd) != 0)
			return -1;
		if (initOpenSSL() != CSMTP_NO_ERROR || openSSLConnect() != CSMTP_NO_ERROR)
		{
			AddResponse("smtpHelo() : STARTTLS handshake or certificate verification failed\n");
			return -1;
		}

		_snprintf_s(buf, sizeof(buf), _TRUNCATE, "EHLO %s\r\n", heloArgument);
		sockPuts(sfd,buf);
		if (smtpResponse(sfd) != 0)
			return -1;
	}

	return 0;
}


// SMTP: Authentication
static int smtpLogin(int sfd)
{
	char szTmp[200] ;


	if(mail.options & MAIL_OPTION_AUTH) {
		_snprintf(buf,sizeof(buf)-1,"AUTH LOGIN\r\n");
		sockPuts(sfd,buf);
		if(smtpResponse(sfd)) return(TRUE) ;

		if (!mail.user || !mail.password)
			return TRUE;

		_snprintf_s(buf,sizeof(buf), _TRUNCATE, "%s\r\n", EncodeBase64(mail.user, szTmp));
		AddResponse("[SMTP authentication username hidden]\n");
		sockPutsSecret(sfd,buf);
		if(smtpResponse(sfd)) return(TRUE) ;

		_snprintf_s(buf,sizeof(buf), _TRUNCATE, "%s\r\n", EncodeBase64(mail.password, szTmp));
		AddResponse("[SMTP authentication password hidden]\n");
		sockPutsSecret(sfd,buf);
		return(smtpResponse(sfd));
	}
	return(FALSE) ;
}


// SMTP: MAIL FROM 
static bool smtpSafeMailbox(const char *mailbox)
{
	return mailbox && mailbox[0] && !strchr(mailbox, '\r') && !strchr(mailbox, '\n');
}

static int smtpMailFrom(int sfd)
{
	if (!smtpSafeMailbox(mail.from))
		return -1;
	_snprintf_s(buf,sizeof(buf), _TRUNCATE, "MAIL FROM: <%s>\r\n",mail.from);
//	OUTPUTDEBUGMSG((("smtpMailFrom() : >>> %s"),buf));
	sockPuts(sfd,buf);
	return (smtpResponse(sfd));
}

// SMTP: quit
static int smtpQuit(int sfd)
{
	sockPuts(sfd,"QUIT\r\n");
	return (smtpResponse(sfd));
}

// SMTP: RSET
// aborts current mail transaction and cause both ends to reset
static int smtpRset(int sfd)
{
	sockPuts(sfd,"RSET\r\n");
	return (smtpResponse(sfd));
}


char *StripSpecial(char *szStr)
{
	int len = strlen(szStr) ;

	while(len--) {
		switch(szStr[len]) {
			case ',' :
			case ';' :
			case ' ' :
				szStr[len] = '\0' ;
				break ;
			default:
				return(szStr) ;
		}
	}
	return(szStr) ;
}

// SMTP: RCPT TO
static int smtpRcptTo(int sfd)
{
	char szTemp[(MAX_MAIL * 5) + 1] = {};
	if (!mail.to)
		return -1;
	_snprintf_s(szTemp, sizeof(szTemp), _TRUNCATE, "%s", mail.to);

	char *context = NULL;
	char *recipient = strtok_s(szTemp, ";,", &context);
	bool sentRecipient = false;
	while(recipient)
	{
		while (*recipient == ' ' || *recipient == '\t')
			++recipient;
		StripSpecial(recipient);
		if (recipient[0])
		{
			if (!smtpSafeMailbox(recipient))
				return -1;
			_snprintf_s(buf, sizeof(buf), _TRUNCATE, "RCPT TO: <%s>\r\n", recipient);
			sockPuts(sfd,buf);
			if (smtpResponse(sfd) != 0)
			{
				smtpRset(sfd);
				return (-1);
			}
			sentRecipient = true;
		}
		recipient = strtok_s(NULL, ";,", &context);
	}
	if (!sentRecipient)
		return -1;
	return (0);
	
}

// SMTP: DATA
static int smtpData(int sfd)
{
	sockPuts(sfd,"DATA\r\n");
	return(smtpResponse(sfd));
}

// SMTP: EOM
static int smtpEom(int sfd)
{
	sockPuts(sfd,"\r\n.\r\n");
	return (smtpResponse(sfd));
}

// SMTP: mail
static int smtpMail(int sfd, char *data)
{
	char szBuffer[128], *pTmp ;
	const pdw::SmtpMessageParts parts =
		pdw::SmtpSplitLegacyMessage(data, 0xBB, MAX_MAIL_LEN - 1);
	const std::string safeSubject = pdw::SmtpSanitizeHeader(parts.subject);

	if (mail.options & MAIL_OPTION_SUBJECT)
	{
		if (!safeSubject.empty())
		{
			memset(buf,0,sizeof(buf));
			(void) _snprintf_s(buf,sizeof(buf), _TRUNCATE, "Subject: %s\r\n", safeSubject.c_str());
			sockPuts(sfd,buf);
		}
	}

	int charsetIndex = ((mail.options & 0x1F0000) >> 16) - 1;
	if (charsetIndex < 0 || charsetIndex >= MAX_SMTP_CHARSETS)
		charsetIndex = 0;
	_snprintf_s(szBuffer, sizeof(szBuffer), _TRUNCATE, "%s", szSmtpCharSets[charsetIndex]);
	pTmp = strchr(szBuffer, ' ') ;
	if(pTmp != NULL)
		*pTmp = '\0' ;
	_snprintf_s(buf,sizeof(buf), _TRUNCATE,
		"Content-type: text/plain; charset=\"%s\"\r\n", szBuffer);
	sockPuts(sfd,buf);
	
	// headers
	if(mail.from)
	{
		const std::string safeFrom = pdw::SmtpSanitizeHeader(mail.from);
		memset(buf,0,sizeof(buf));
		(void) _snprintf_s(buf,sizeof(buf), _TRUNCATE, "From: %s\r\n",safeFrom.c_str());
		sockPuts(sfd,buf);
	}
	if(mail.to)
	{
		const std::string safeTo = pdw::SmtpSanitizeHeader(mail.to);
		memset(buf,0,sizeof(buf));
		(void) _snprintf_s(buf,sizeof(buf), _TRUNCATE, "To: %s\r\n",safeTo.c_str());
		sockPuts(sfd,buf);	
	}
	
	if(mail.cc)
	{
		const std::string safeCc = pdw::SmtpSanitizeHeader(mail.cc);
		memset(buf,0,sizeof(buf));
		(void) _snprintf_s(buf,sizeof(buf), _TRUNCATE, "Cc: %s\r\n",safeCc.c_str());
		sockPuts(sfd,buf);
	}
	memset(buf,0,sizeof(buf));
	_snprintf_s(buf,sizeof(buf), _TRUNCATE, "X-Mailer: %s\r\n",MAILSEND_VERSION);
	sockPuts(sfd,buf);
	
	
	sockPuts(sfd,"\r\n");
	
	if ((mail.options & MAIL_OPTION_MSG) && !parts.body.empty())
	{
		std::string escapedBody = pdw::SmtpDotStuff(parts.body);
		sockPuts(sfd, &escapedBody[0]);
		sockPuts(sfd,"\r\n");
	}
	return (0);
}


static bool MailQueuePeek(char *message, size_t messageLength)
{
	if (!message || messageLength == 0)
		return false;

	AcquireSRWLockShared(&MailQueueLock);
	const bool hasMessage = nBufferedMailCount > 0;
	if (hasMessage)
		_snprintf_s(message, messageLength, _TRUNCATE, "%s", szMailBuffer[nBufferdMailEnd]);
	ReleaseSRWLockShared(&MailQueueLock);
	return hasMessage;
}

static void MailQueueCommit()
{
	AcquireSRWLockExclusive(&MailQueueLock);
	if (nBufferedMailCount > 0)
	{
		szMailBuffer[nBufferdMailEnd][0] = '\0';
		nBufferdMailEnd = (nBufferdMailEnd + 1) % MAX_MAIL;
		--nBufferedMailCount;
	}
	ReleaseSRWLockExclusive(&MailQueueLock);
}

static bool MailQueueAppend(const char *message)
{
	if (!message || !message[0])
		return false;

	AcquireSRWLockExclusive(&MailQueueLock);
	if (nBufferedMailCount >= MAX_MAIL)
	{
		++nDroppedMailCount;
		ReleaseSRWLockExclusive(&MailQueueLock);
		return false;
	}
	_snprintf_s(szMailBuffer[nBufferdMailStart], MAX_MAIL_LEN, _TRUNCATE, "%s", message);
	nBufferdMailStart = (nBufferdMailStart + 1) % MAX_MAIL;
	++nBufferedMailCount;
	ReleaseSRWLockExclusive(&MailQueueLock);
	if (MailWakeEvent)
		SetEvent(MailWakeEvent);
	return true;
}

// Returns 1 after one queued message is accepted by the server, or 0 when
// there is nothing to do / the head message must be retried later.
int xSendMail(THEMAIL *pMail)
{
	SOCKET	sfd;
	int 	rc = (-1);
	char queuedMessage[MAX_MAIL_LEN] = {};
	extern int nSMTPsessions;
	extern int nSMTPemails;
	
	if(!MailQueuePeek(queuedMessage, sizeof(queuedMessage)))
		return(0) ;

	if (!pMail || !smtpSafeMailbox(pMail->from)) {
		OUTPUTDEBUGMSG((("No From address specified")));
		AddResponse("xSendMail(): No From address specified\n");
		MailQueueCommit();
		return (0);
	}
	if (!pMail->smtp_server || !pMail->smtp_server[0]) {
		AddResponse("xSendMail(): No SMTP server specified\n");
		MailQueueCommit();
		return (0);
	}
	const int smtpPort = pMail->smtp_port > 0 ? pMail->smtp_port : MAILSEND_SMTP_PORT;

	// open the network connection
	sfd = smtpConnect(pMail->smtp_server, smtpPort);
	if(sfd == INVALID_SOCKET)
	{
		return(0) ;
	}
	else nSMTPsessions++;		// PH: Counts # of sessions

	if(!(rc = smtpHelo(sfd))) {
		if(!(rc = smtpLogin(sfd))) {
			if(!(rc = smtpMailFrom(sfd))) {
				if(!(rc = smtpRcptTo(sfd))) {
					if(!(rc = smtpData(sfd))) {
						if(!(rc = smtpMail(sfd, queuedMessage))) {
							if(!(rc = smtpEom(sfd))) {
								nSMTPemails++;
								MailQueueCommit();
								smtpQuit(sfd); // best effort after the server accepted DATA
							}
						}
					}
				}
			}
		}
	}

	// close the network connection
	smtpDisconnect(sfd);
	return(rc == 0 ? 1 : 0);
}


DWORD WINAPI MailThreadFunc(LPVOID lpData)
{
	OUTPUTDEBUGMSG((("MailThreadFunc()")));

	while(keepbusy.load())
	{
		if(xSendMail((THEMAIL *) lpData))
			continue;
		WaitForSingleObject(MailWakeEvent, 1000);
	}
	OUTPUTDEBUGMSG((("MailThreadFunc() exiting\n")));
	return 0;
}

static bool StopMailWorker()
{
	if(!MailThread)
		return true;

	keepbusy.store(false);
	SOCKET socketToInterrupt = activeSocket.load();
	if(socketToInterrupt != INVALID_SOCKET)
		shutdown(socketToInterrupt, 2); // SD_BOTH in Winsock2; legacy headers omit the name
	if(MailWakeEvent)
		SetEvent(MailWakeEvent);

	const DWORD waitResult = WaitForSingleObject(MailThread, 5000);
	if(waitResult != WAIT_OBJECT_0)
	{
		OUTPUTDEBUGMSG((("StopMailWorker(): worker did not stop within five seconds\n")));
		return false;
	}

	CloseHandle(MailThread);
	MailThread = NULL;
	if(MailWakeEvent)
	{
		CloseHandle(MailWakeEvent);
		MailWakeEvent = NULL;
	}
	return true;
}

static bool StartMail(int nOptions)
{
	if(!(nOptions & MAIL_OPTION_ENABLE))
		return StopMailWorker();
	if(MailThread)
		return true;

	MailWakeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!MailWakeEvent)
		return false;

	keepbusy.store(true);
	DWORD threadId = 0;
	MailThread = CreateThread(NULL, 0, MailThreadFunc, (LPVOID)&mail, 0, &threadId);
	if(!MailThread)
	{
		keepbusy.store(false);
		CloseHandle(MailWakeEvent);
		MailWakeEvent = NULL;
		return false;
	}
	if(nBufferedMailCount > 0)
		SetEvent(MailWakeEvent);
	return true;
}


int SendMail(HWND hResponse, bool bMatch, bool bMonitor_only, int iSeparateSMTP, char *sz1, char *sz2, char *sz3, char *sz4, char *sz5, char *sz6, char *sz7, char *szLabel)
{
//	OUTPUTDEBUGMSG((("SendMail()")));
	if(hResponse) 
	{
		responseWindow.store(hResponse);
		mail.hResponse = hResponse;
		nMaxLen = 0;
		SendMessage(hResponse, LB_RESETCONTENT, 0, 0L) ;
	}
	if(mail.options & MAIL_OPTION_ENABLE) 
	{
		switch(mail.options & MAIL_OPTION_MODES) 
		{
			case MAIL_OPTION_MODE_ALL :
				OUTPUTDEBUGMSG((("SendMail() Send : Mode All")));
			break ;

			case MAIL_OPTION_MODE_FILTER :	
			if(!bMatch || bMonitor_only)
			{
				OUTPUTDEBUGMSG((("SendMail() Not Send : !bMatch || bMonitor_only")));
				return(0) ;
			}
			OUTPUTDEBUGMSG((("SendMail() Send : bMatch(%d) || bMonitor_only(%d)"), bMatch, bMonitor_only));
			break ;

			case MAIL_OPTION_MODE_MONITOR :
			if(!bMatch)
			{
				OUTPUTDEBUGMSG((("SendMail() Not Send: !bMatch")));
				return(0) ;
			}
			OUTPUTDEBUGMSG((("SendMail() Send : bMatch(%d) || bMonitor_only(%d)"), bMatch, bMonitor_only));

			break ;
			case MAIL_OPTION_MODE_SELECTABLE :
			if(!bMatch || !iSeparateSMTP)
			{
				OUTPUTDEBUGMSG((("SendMail() Not Send: !bMatch || !iSeparateSMTP")));
				return(0) ;
			}
			OUTPUTDEBUGMSG((("SendMail() Send : iSeparateSMTP(%d)"), iSeparateSMTP));
			break ;
		}
	}
	else 
	{
		OUTPUTDEBUGMSG((("SendMail() Mail Disabled")));
		return(0) ;
	}

	if(!mail.smtp_port)
	{
		OUTPUTDEBUGMSG((("SendMail() Error: MailInit NOT called!")));
		nSMTPerrors++;		// PH: Counts # of Errors

		return(-1) ;
	}

	std::string message;
	message.reserve(512);
	const size_t maxMessageLength = MAX_MAIL_LEN - 1;
	if(mail.options & MAIL_OPTION_ADDRESS)
		pdw::SmtpAppendField(message, sz1, maxMessageLength);
	if(mail.options & MAIL_OPTION_TIME)
		pdw::SmtpAppendField(message, sz2, maxMessageLength);
	if(mail.options & MAIL_OPTION_DATE)
		pdw::SmtpAppendField(message, sz3, maxMessageLength);
	if(mail.options & MAIL_OPTION_MODE)
		pdw::SmtpAppendField(message, sz4, maxMessageLength);
	if(mail.options & MAIL_OPTION_TYPE)
		pdw::SmtpAppendField(message, sz5, maxMessageLength);
	if(mail.options & MAIL_OPTION_BITRATE)
		pdw::SmtpAppendField(message, sz6, maxMessageLength);
	if(mail.options & MAIL_OPTION_MESSAGE)
		pdw::SmtpAppendField(message, sz7, maxMessageLength);
	if(mail.options & MAIL_OPTION_LABEL)
		pdw::SmtpAppendField(message, szLabel, maxMessageLength, "- ");

	if(!message.empty())
	{
		OUTPUTDEBUGMSG((("SendMail() queued %u bytes\n"), (unsigned)message.size()));
		if(!MailQueueAppend(message.c_str()))
		{
			nSMTPerrors++;
			AddResponse("SendMail(): SMTP queue is full; message was not queued\n");
			return -1;
		}
	}
//	OUTPUTDEBUGMSG((("SendMail() nBufferdMailStart %d nBufferdMailEnd %d\n"), nBufferdMailStart, nBufferdMailEnd));
	return(0) ;
}	


int MailInit(char *szMailHost, char *szMailHeloDomain, char *szMailFrom, char *szMailTo, char *szMailUser, char *szMailPassword, int iMailPort, int nOptions)
{
	// Configuration pointers belong to Profile. Join the worker before changing
	// them so a concurrent connect/authentication cannot dereference half-updated
	// or null values during Apply/exit.
	if(!StopMailWorker())
		return -1;

	memset(&mail, 0, sizeof(mail)) ;
	mail.from = szMailFrom ;
	mail.to = szMailTo ;
	mail.cc = NULL ;
	mail.bcc = NULL ;
	mail.smtp_server = szMailHost ;
	mail.helo_domain = szMailHeloDomain ;
	mail.user = szMailUser ;
	mail.password = szMailPassword ;
	mail.smtp_port = iMailPort ;
	mail.options = nOptions ;
	responseWindow.store(NULL);
	if(!StartMail(nOptions))
		return -1;
	return(0) ;
}

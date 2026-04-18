#include "pch.h"
#include "Core.h"

void NetCore::LoadAcceptEx()
{
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD bytes = 0;

	if (WSAIoctl(listenSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx, sizeof(guidAcceptEx),
		&lpAcceptEx, sizeof(lpAcceptEx), &bytes, NULL, NULL)
		== SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;

		GUID guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;

	if (WSAIoctl(listenSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidGetAcceptExSockaddrs, sizeof(guidGetAcceptExSockaddrs),
		&lpGetAcceptExSockaddrs, sizeof(lpGetAcceptExSockaddrs),
		&bytes, NULL, NULL) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;
}

void NetCore::PostAccept() const
{
	AcceptContext* ctx = new AcceptContext();
	ZeroMemory(ctx, sizeof(AcceptContext));

	if ((ctx->sock = WSASocket(AF_INET, SOCK_STREAM,
		0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
	{
		PRINT_LAST_WSA_EXCAPTION;
		delete ctx;
		return;
	}

	ctx->overlapped.ioType = IOType::ACCEPT;

	DWORD bytes = 0;

	BOOL ret = lpAcceptEx(listenSock, ctx->sock, ctx->buf,
		0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		&bytes, &ctx->overlapped.overlapped);

	if (!ret) PRINT_LAST_WSA_EXCAPTION;
}

void NetCore::OnAccept(AcceptContext* ctx)
{
	int sockLen = sizeof(listenSock);
	setsockopt(ctx->sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
		(char*)&listenSock, sizeof(listenSock));

	SOCKADDR_IN* localAdr = nullptr;
	SOCKADDR_IN* remoteAdr = nullptr;
	int localAdrLen = 0;
	int remoteAdrLen = 0;

	lpGetAcceptExSockaddrs(
		ctx->buf,
		0,
		sizeof(SOCKADDR_IN) + 16,
		sizeof(SOCKADDR_IN) + 16,
		(SOCKADDR**)&localAdr,
		&localAdrLen,
		(SOCKADDR**)&remoteAdr,
		&remoteAdrLen
	);

	char ipStr[INET_ADDRSTRLEN] = { 0 };

	inet_ntop(AF_INET, &remoteAdr->sin_addr, ipStr, sizeof(ipStr));

	Session* session = new Session();
	session->sock = ctx->sock;

	session->sessionID = sm.IssueSessionId();
	sm.Emplace(session);

	auto str = "Client " + to_string(ctx->sock) + " is connected from " + ipStr + " | Session ID : " + to_string(session->sessionID);
	LOG_INFO(str);

	if (CreateIoCompletionPort((HANDLE)session->sock, hComPort,
		(ULONG_PTR)session->sessionID, 0) == NULL) PRINT_LAST_WSA_EXCAPTION;

	PostRecv(session);

	PostAccept();

	delete ctx;
}

void NetCore::StartUp(string name, USHORT port, int count)
{
	Debug::PrintLogo(name);

	isRunning = true;
	InitializeCriticalSection(&cs);

	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	size_t threadSize = static_cast<size_t>(sysInfo.dwNumberOfProcessors) * 2 + 1;
	pool.resize(threadSize);

	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) PRINT_LAST_WSA_EXCAPTION;
	if ((hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) == NULL) PRINT_LAST_WSA_EXCAPTION;
	if ((listenSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET) PRINT_LAST_WSA_EXCAPTION;
	CreateIoCompletionPort((HANDLE)listenSock, hComPort, (ULONG_PTR)0, 0);

	memset(&listenAdr, 0, sizeof(listenAdr));

	listenAdr.sin_family = AF_INET;
	listenAdr.sin_addr.s_addr = htonl(INADDR_ANY);
	listenAdr.sin_port = htons(port);

	if (::bind(listenSock, (SOCKADDR*)&listenAdr, sizeof(listenAdr)) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;

	acceptCount = count;

	if (listen(listenSock, acceptCount) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;

	RegistAccept();

	for (int i = 0; i < threadSize; i++)
	{ pool[i] = (HANDLE)_beginthreadex(NULL, 0, ThreadEntry, new ThreadParam{ this, hComPort }, 0, NULL); }

}

void NetCore::PostRecv(Session* session)
{
	RecvContext* ctx = new RecvContext();
	ZeroMemory(ctx, sizeof(RecvContext));
	
	ctx->overlapped.ioType = IOType::READING;

	ctx->buf.buf = ctx->overlapped.buf;
	ctx->buf.len = sizeof(ctx->overlapped.buf);

	if (WSARecv(session->sock, &ctx->buf, 1, NULL,
		&session->ioFlag, &ctx->overlapped.overlapped, NULL) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;
}

void NetCore::Parser(Session* session)
{
	while (true)
	{
		if (session->recvBuf.Size() < sizeof(PacketHeader)) return;

		PacketHeader header;
		ZeroMemory(&header, sizeof(header));

		if (!session->recvBuf.TryPop((char*)&header, sizeof(header))) return;

		if (session->recvBuf.Size() < header.size - 6) return;

		vector<char> data(header.size);

		if (!session->recvBuf.TryPop(data.data(), header.size)) return;

		session->jobQueue.Push(Job(header.header, move(data)));
	}
}

unsigned __stdcall NetCore::ThreadEntry(void* param)
{
	ThreadParam* p = static_cast<ThreadParam*>(param);

	NetCore* self = p->self;
	HANDLE hComPort = p->hComPort;

	delete p;

	return self->WorkerThread(hComPort);
}

unsigned __stdcall NetCore::WorkerThread(HANDLE hComPort)
{
	DWORD byteTrans = 0;
	ULONG_PTR completionKey = 0;
	OVERLAPPED* overlapped = nullptr;
	OVERLAPPED_EX* overlappedEx = nullptr;
	DWORD64 sessionID = 0;
	Session* session = nullptr;
	BOOL res = false;

	while (true)
	{
		byteTrans = 0;
		completionKey = 0;
		overlapped = nullptr;
		overlappedEx = nullptr;
		session = nullptr;

		res = GetQueuedCompletionStatus(hComPort, &byteTrans, &completionKey, &overlapped, INFINITE);
		if (!overlapped)
		{
			//shutdown
			if (!completionKey)
			{
				PRINT_LAST_WSA_EXCAPTION;
			}
			else
			{
				//logging
			}
			continue;
		}

		if (overlapped == (LPOVERLAPPED)0xffffffff)
		{
			//disconnect
			continue;
		}

		overlappedEx = (OVERLAPPED_EX*)overlapped;

		sessionID = completionKey;
		session = sm.GetSession(sessionID);

		if (res)
		{
			switch (overlappedEx->ioType)
			{
			case IOType::ACCEPT:
			{
				AcceptContext* ctx = CONTAINING_RECORD(overlappedEx, AcceptContext, overlapped);
				OnAccept(ctx);
				break;
			}
			case IOType::READING:
			{
				if (!session) continue;
				RecvContext* ctx = CONTAINING_RECORD(overlappedEx, RecvContext, overlapped);
				if (!ctx)
				{
					PRINT_LAST_WSA_EXCAPTION;
					break;
				}
				if (!byteTrans)
				{
					//disconnect
					break;
				}

				session->recvBuf.TryPush(ctx->overlapped.buf, byteTrans);
				Parser(session);

				Processing(&session->jobQueue);

				PostRecv(session);

				delete ctx;
				break;
			}
			case IOType::WRITING:
			{
				break;
			}
			default: break;
			}
		}
		if (session && !InterlockedDecrement(&session->ioCount))
		{
			//shutdown session
		}
	}
	return 0;
}
void NetCore::RegistAccept()
{
	LoadAcceptEx();
	for (int i = 0; i < acceptCount; i++) PostAccept();
}
void NetCore::ProcessAccept()
{
}
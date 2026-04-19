#include "pch.h"
#include "Core.h"

void NetCore::LoadAcceptEx()
{
	DWORD bytes = 0;

	GUID guidAcceptEx = WSAID_ACCEPTEX;

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
	IOContext* ctx = new IOContext();
	ZeroMemory(ctx, sizeof(IOContext));

	if ((ctx->sock = WSASocket(AF_INET, SOCK_STREAM,
		0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
	{
		PRINT_LAST_WSA_EXCAPTION;
		delete ctx;
		return;
	}

	ctx->overlappedEx.ioType = IOType::ACCEPT;

	DWORD bytes = 0;

	BOOL ret = lpAcceptEx(listenSock, ctx->owner->sock, ctx->overlappedEx.buf,
		0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		&bytes, &ctx->overlappedEx.overlapped);

	if (!ret) PRINT_LAST_WSA_EXCAPTION;
}

void NetCore::OnAccept(IOContext* ctx)
{
	int sockLen = sizeof(listenSock);
	setsockopt(ctx->sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
		(char*)&listenSock, sizeof(listenSock));

	SOCKADDR_IN* localAdr = nullptr;
	SOCKADDR_IN* remoteAdr = nullptr;
	int localAdrLen = 0;
	int remoteAdrLen = 0;

	lpGetAcceptExSockaddrs(
		ctx->overlappedEx.buf,
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

	Session* session = CreateSession(ctx->sock);
	sm.Emplace(session);

	auto str = "Client " + to_string(session->sock) + " is connected from " + ipStr + " | Session ID : " + to_string(session->sessionID);
	LOG_INFO(str);

	if (!CreateIoCompletionPort((HANDLE)session->sock, hComPort,
		(ULONG_PTR)session->sessionID, 0)) PRINT_LAST_WSA_EXCAPTION;

	PostRecv(session);

	PostAccept();

	delete ctx;
}

void NetCore::OnRecv(IOContext* ctx, int byteTrans)
{
	if (!ctx)
	{
		PRINT_LAST_WSA_EXCAPTION;
		return;
	}
	if (!byteTrans)
	{
		//disconnect
		return;
	}

	ctx->owner->recvBuf.TryPush(ctx->overlappedEx.buf, byteTrans);
	Parser(ctx->owner);

	Processing(&ctx->owner->jobQueue);

	PostRecv(ctx->owner);
}

void NetCore::OnSend(IOContext* ctx)
{
	SRWExclusiveLock lock(ctx->owner->sl);

	if (ctx->owner->sending) return;

	//need excaption

	if (ctx->owner->sendBuf.Size() < sizeof(PacketHeader)) return;

	PacketHeader header;
	ZeroMemory(&header, sizeof(header));

	if (!ctx->owner->sendBuf.Peek((char*)&header, sizeof(header))) return;

	if (ctx->owner->sendBuf.Size() < header.size) return;

	if (!ctx->owner->sendBuf.TryPop(ctx->owner->sendCtx.overlappedEx.buf, header.size)) return;

	ZeroMemory(&ctx->owner->sendCtx.overlappedEx.overlapped, sizeof(OVERLAPPED));

	ctx->owner->sendCtx.overlappedEx.wsaBuf.buf =
		ctx->owner->sendCtx.overlappedEx.buf;
	ctx->owner->sendCtx.overlappedEx.wsaBuf.len = header.size;

	ctx->owner->sending = true;

	WSASend(ctx->owner->sock, &ctx->owner->sendCtx.overlappedEx.wsaBuf, 1, NULL, 0,
		&ctx->owner->sendCtx.overlappedEx.overlapped, NULL);
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

	LoadAcceptEx();
	for (int i = 0; i < acceptCount; i++) PostAccept();

	for (int i = 0; i < threadSize; i++)
	{ pool[i] = (HANDLE)_beginthreadex(NULL, 0, ThreadEntry, new ThreadParam{ this, hComPort }, 0, NULL); }

}

void NetCore::PostRecv(Session* session) const
{	
	if (session->recving) return;

	session->recvCtx.sock = session->sock;
	session->recvCtx.overlappedEx.ioType = IOType::READING;
	ZeroMemory(&session->recvCtx.overlappedEx.overlapped, sizeof(OVERLAPPED));

	session->recvCtx.owner = session;

	session->recvCtx.overlappedEx.wsaBuf.buf = session->recvCtx.overlappedEx.buf;
	session->recvCtx.overlappedEx.wsaBuf.len = sizeof(session->recvCtx.overlappedEx.buf);

	session->recving = true;

	if (WSARecv(session->recvCtx.sock, &session->recvCtx.overlappedEx.wsaBuf, 1, NULL,
		&session->ioFlag, &session->recvCtx.overlappedEx.overlapped, NULL) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;
}

void NetCore::PostSend(Session* session) const
{
	SRWExclusiveLock lock(session->sl);

	if (session->sending) return;

	session->sendCtx.sock = session->sock;
	session->sendCtx.overlappedEx.ioType = IOType::WRITING;
	ZeroMemory(&session->sendCtx.overlappedEx.overlapped, sizeof(OVERLAPPED));

	session->sendCtx.owner = session;

	session->sending = true;

	PostQueuedCompletionStatus(hComPort, 0, (ULONG_PTR)session,
		&session->sendCtx.overlappedEx.overlapped);
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
	OVERLAPPED* overlappedPtr = nullptr;
	OVERLAPPED_EX* overlappedExPtr = nullptr;
	DWORD64 sessionID = 0;
	Session* session = nullptr;
	BOOL res = false;

	while (true)
	{
		byteTrans = 0;
		completionKey = 0;
		overlappedPtr = nullptr;
		overlappedExPtr = nullptr;
		session = nullptr;

		res = GetQueuedCompletionStatus(hComPort, &byteTrans, &completionKey, &overlappedPtr, INFINITE);
		if (!overlappedPtr)
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

		if (overlappedPtr == (LPOVERLAPPED)0xffffffff)
		{
			//disconnect
			continue;
		}

		overlappedExPtr = (OVERLAPPED_EX*)overlappedPtr;

		sessionID = completionKey;
		session = sm.GetSession(sessionID);

		if (res)
		{
			IOContext* ctx = CONTAINING_RECORD(overlappedExPtr, IOContext, overlappedEx);
			switch (overlappedExPtr->ioType)
			{
			case IOType::ACCEPT: OnAccept(ctx); break;
			case IOType::READING:
			{
				ctx->owner->recving = false;
				OnRecv(ctx, byteTrans);
				break;
			}
			case IOType::WRITING:
			{
				ctx->owner->sending = false;
				OnSend(ctx);
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

Session* NetCore::CreateSession(SOCKET sock)
{
	Session* session = new Session();
	session->sock = sock;
	session->sessionID = sm.IssueSessionId();
	session->alive = true;
	return session;
}

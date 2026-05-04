#include "pch.h"
#include "Core.h"

void NetCore::LoadAcceptEx()
{
	DWORD bytes = 0;

	auto loadExtension = [&](GUID guid, void** fnPtr)
		{ if (WSAIoctl(listenSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
			&guid, sizeof(guid), fnPtr, sizeof(*fnPtr),
			&bytes, NULL, NULL) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION; };

	loadExtension(WSAID_ACCEPTEX, reinterpret_cast<void**>(&lpAcceptEx));
	loadExtension(WSAID_GETACCEPTEXSOCKADDRS, reinterpret_cast<void**>(&lpGetAcceptExSockaddrs));
}


void NetCore::PostAccept() const
{
	PIOCONTEXT ctx = new IOCONTEXT();

	if ((ctx->sock = WSASocket(AF_INET, SOCK_STREAM,
		0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
	{
		PRINT_LAST_WSA_EXCAPTION;
		delete ctx;
		return;
	}

	ctx->overlappedEx.ioType = IO_ACCEPT;

	DWORD bytes = 0;
	DWORD dwLen = sizeof(SOCKADDR_IN) + 16;

	BOOL ret = lpAcceptEx(listenSock, ctx->sock, ctx->overlappedEx.buf,
		0, dwLen, dwLen, &bytes, &ctx->overlappedEx.overlapped);

	if (!ret) PRINT_LAST_WSA_EXCAPTION;
}

void NetCore::OnAccept(PIOCONTEXT ctx)
{
	int sockLen = sizeof(listenSock);

	setsockopt(ctx->sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
		(char*)&listenSock, sizeof(listenSock));

	PSOCKADDR_IN localAdr = nullptr;
	PSOCKADDR_IN remoteAdr = nullptr;
	int localAdrLen = 0;
	int remoteAdrLen = 0;

	DWORD dwLen = sizeof(SOCKADDR_IN) + 16;

	lpGetAcceptExSockaddrs(ctx->overlappedEx.buf, 0, dwLen, dwLen,
		(PSOCKADDR*)&localAdr, &localAdrLen, (PSOCKADDR*)&remoteAdr, &remoteAdrLen);

	char ipStr[INET_ADDRSTRLEN] = { 0 };

	inet_ntop(AF_INET, &remoteAdr->sin_addr, ipStr, sizeof(ipStr));

	PSESSION session = CreateSession(ctx->sock);
	sm.Emplace(session);

	auto str = "Client " + to_string(session->sock) + " is connected from " + ipStr +
		" | Session ID : " + to_string(session->sessionID);
	LOG_INFO(str);

	if (!CreateIoCompletionPort((HANDLE)session->sock, hComPort,
		(ULONG_PTR)session->sessionID, 0)) PRINT_LAST_WSA_EXCAPTION;

	PostRecv(session);

	PostAccept();

	delete ctx;
}

void NetCore::OnRecv(PIOCONTEXT ctx, int byteTrans)
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

	PSESSION session = ctx->owner;

	PIOCONTEXT cx = session->contextPool.Acquire();
	cx->Init(session);

	cx->owner->recvBuf.TryPush(ctx->overlappedEx.buf, byteTrans);
	cx->owner->contextPool.Release(ctx);

	Parser(cx->owner);

	Processing(cx->owner);

	PostRecv(cx->owner);
}

void NetCore::OnSend(PIOCONTEXT ctx)
{
	SRWEL lock(ctx->owner->sl);

	PIOCONTEXT cx = ctx->owner->contextPool.Acquire();
	cx->Init(ctx->owner,IO_WRITING);
	cx->owner->contextPool.Release(ctx);

	if (cx->owner->sendBuf.Size() < sizeof(HEADER)) return;

	HEADER header;
	ZeroMemory(&header, sizeof(header));

	if (!cx->owner->sendBuf.Peek((char*)&header, sizeof(header))) return;

	if (cx->owner->sendBuf.Size() < header.size) return;

	if (!cx->owner->sendBuf.TryPop(cx->overlappedEx.buf, header.size)) return;

	cx->overlappedEx.wsaBuf.len = header.size;

	if (WSASend(cx->sock, &cx->overlappedEx.wsaBuf, 1, NULL, 0,
		&cx->overlappedEx.overlapped, NULL) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;
}

void NetCore::StartUp(string name, USHORT port, int count)
{
	Debug::PrintLogo(name);

	isRunning = true;
	acceptCount = count;
	InitializeCriticalSection(&cs);

	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	size_t threadSize = static_cast<size_t>(sysInfo.dwNumberOfProcessors) * 2 + 1;
	threadPool.resize(threadSize);

	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) PRINT_LAST_WSA_EXCAPTION;
	if ((hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) == NULL) PRINT_LAST_WSA_EXCAPTION;
	if ((listenSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET) PRINT_LAST_WSA_EXCAPTION;
	CreateIoCompletionPort((HANDLE)listenSock, hComPort, (ULONG_PTR)0, 0);

	memset(&listenAdr, 0, sizeof(listenAdr));

	listenAdr.sin_family = AF_INET;
	listenAdr.sin_addr.s_addr = htonl(INADDR_ANY);
	listenAdr.sin_port = htons(port);

	if (::bind(listenSock, (PSOCKADDR)&listenAdr, sizeof(listenAdr)) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;

	if (listen(listenSock, acceptCount) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;

	LoadAcceptEx();
	for (int i = 0; i < acceptCount; i++) PostAccept();

	for (int i = 0; i < threadSize; i++)
		threadPool[i] = (HANDLE)_beginthreadex(NULL, 0, ThreadEntry, new TPRAM{ this, hComPort }, 0, NULL);

}

void NetCore::PostRecv(PSESSION session) const
{
	PIOCONTEXT cx = session->contextPool.Acquire();
	cx->Init(session);

	if (WSARecv(cx->sock, &cx->overlappedEx.wsaBuf, 1, NULL,
		&cx->owner->ioFlag, &cx->overlappedEx.overlapped, NULL) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;
}

void NetCore::PostSend(PSESSION session) const
{
	SRWEL lock(session->sl);

	PIOCONTEXT cx = session->contextPool.Acquire();
	cx->Init(session, IO_WRITING);

	if (PostQueuedCompletionStatus(hComPort, 0, (ULONG_PTR)cx->owner,
		&cx->overlappedEx.overlapped) == SOCKET_ERROR) PRINT_LAST_EXCAPTION;
}

void NetCore::Parser(PSESSION session)
{
	while (true)
	{
		PJOB job = session->jobPool.Acquire();

		if (session->recvBuf.Size() < sizeof(HEADER)) return;

		HEADER header;
		ZeroMemory(&header, sizeof(header));

		if (!session->recvBuf.TryPop((char*)&header, sizeof(header))) return;

		if (session->recvBuf.Size() < header.size) return;

		vector<char> data(header.size);

		if (!session->recvBuf.TryPop(data.data(), header.size)) return;

		job->header = header.header;
		job->data = move(data);

		session->jobQueue.push(job);
	}
}

unsigned __stdcall NetCore::ThreadEntry(void* param)
{
	PTPRAM p = static_cast<PTPRAM>(param);

	NetCore* self = p->self;
	HANDLE hComPort = p->hComPort;

	delete p;

	return self->WorkerThread(hComPort);
}

unsigned __stdcall NetCore::WorkerThread(HANDLE hComPort)
{
	DWORD byteTrans = 0;
	ULONG_PTR completionKey = 0;
	LPOVERLAPPED overlappedPtr = nullptr;
	POVERLAPPED_EX overlappedExPtr = nullptr;
	DWORD64 sessionID = 0;
	PSESSION session = nullptr;
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

		overlappedExPtr = (POVERLAPPED_EX)overlappedPtr;

		sessionID = completionKey;
		session = sm.GetSession(sessionID);

		if (res)
		{
			PIOCONTEXT ctx = CONTAINING_RECORD(overlappedExPtr, IOCONTEXT, overlappedEx);
			switch (overlappedExPtr->ioType)
			{
			case IO_ACCEPT: OnAccept(ctx); break;
			case IO_READING: OnRecv(ctx, byteTrans); break;
			case IO_WRITING: OnSend(ctx); break;
			default: break;
			}
		}
		//if (session && !InterlockedDecrement(&session->ioCount))
		//{
		//	//shutdown session
		//}
	}
	return 0;
}

PSESSION NetCore::CreateSession(SOCKET sock)
{ return new SESSION(sock, sm.GenerateSessionId()); }

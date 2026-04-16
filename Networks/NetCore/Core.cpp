#include "pch.h"
#include "Core.h"

void NetCore::LoadAcceptEx()
{
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD bytes = 0;

	if (WSAIoctl(listenSock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx, sizeof(guidAcceptEx),
		&lpAcceptEx, sizeof(lpAcceptEx), &bytes, NULL, NULL)
		== SOCKET_ERROR)
	{
		//logging
	}
}

void NetCore::PostAccept() const
{
	AcceptContext* ctx = new AcceptContext();

	ctx->sock = WSASocket(AF_INET, SOCK_STREAM,
		0, NULL, 0, WSA_FLAG_OVERLAPPED);

	ctx->overlapped.ioType = IOType::ACCEPT;

	DWORD bytes = 0;

	BOOL ret = lpAcceptEx(listenSock, ctx->sock, ctx->buf,
		0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		&bytes, &ctx->overlapped.overlapped);

	if (!ret)
	{
		//error logging
	}
}

void NetCore::OnAccept(AcceptContext* ctx)
{
	int sockLen = sizeof(listenSock);
	getsockopt(ctx->sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
		(char*)&listenSock, &sockLen);

	Session* session = new Session();
	session->sock = ctx->sock;

	session->sessionID = sm.IssueSessionId();
	sm.Emplace(session);

	if (CreateIoCompletionPort((HANDLE)session->sock, hComPort, 
		(ULONG_PTR)session, 0) == NULL)
	{
		//logging
	}

	PostRecv(session);

	PostAccept();

	delete ctx;
}

void NetCore::StartUp(USHORT port, int acceptCount)
{
	isRunning = true;
	InitializeCriticalSection(&cs);

	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	size_t threadSize = static_cast<size_t>(sysInfo.dwNumberOfProcessors) * 2 + 1;
	pool.resize(threadSize);

	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		//logging
	}
	if ((hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) == NULL)
	{
		//logging
	}
	if ((listenSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
	{
		//logging
	}
	CreateIoCompletionPort((HANDLE)listenSock, hComPort, (ULONG_PTR)0, 0);

	memset(&listenAdr, 0, sizeof(listenAdr));

	listenAdr.sin_family = AF_INET;
	listenAdr.sin_addr.s_addr = htonl(INADDR_ANY);
	listenAdr.sin_port = port;

	if (::bind(listenSock, (SOCKADDR*)&listenAdr, sizeof(listenAdr)) == SOCKET_ERROR)
	{
		//logging
	}

	if (listen(listenSock, 100) == SOCKET_ERROR)
	{
		//logging
	}

	LoadAcceptEx();
	for (int i = 0; i < acceptCount; i++) PostAccept();

	for (int i = 0; i < threadSize; i++)
	{
		pool[i] = (HANDLE)_beginthreadex(NULL, 0, ThreadEntry, new ThreadParam{ this,hComPort }, 0, NULL);
	}

}

void NetCore::PostRecv(Session* session)
{
	WSABUF buf{};
	buf.buf = session->overlappedEx.buf;
	buf.len = sizeof(session->overlappedEx.buf);

	session->overlappedEx.ioType = IOType::READING;

	if (WSARecv(session->sock, &buf, 1, NULL,
		&session->ioFlag, &session->overlappedEx.overlapped, NULL) == SOCKET_ERROR)
	{
		//logging
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
				//error logging
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

		if (res && byteTrans)
		{
			switch (overlappedEx->ioType)
			{
			case IOType::ACCEPT:
			{
				//test
				cout << "connected";
				break;
			}
			case IOType::READING:
			{

				//test
				vector<char> a;
				Job job{ PKT_UNKNOWN,move(a) };
				Processing(session, job);
				break;
			}
			case IOType::WRITING:
			{
				break;
			}
			default: break;
			}
		}
		if (!InterlockedDecrement(&session->ioCount))
		{
			//shutdown session
		}
	}
	return 0;
}

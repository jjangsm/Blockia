#pragma once
#include "common.h"
#include "Debug.h"
#include <Common.pb.h>

constexpr auto MAX_BUF =  4096;
constexpr auto MAX_CONTEXT_POOL_SIZE = 8;
constexpr auto MAX_JOB_POOL_SIZE = 16;

using namespace Protocol;

enum class IOType : uint8_t
{
	ACCEPT,
	READING,
	WRITING
};

#pragma pack(push, 1)
struct PacketHeader
{
	uint16_t size = 0;
	uint32_t header = 0;
};
#pragma pack(pop)

class Job
{
public:
	vector<char> data{};
	uint32_t header = 0;
public:
	Job() = default;

	size_t Size() const noexcept { return data.size(); }
};

typedef struct OverlappedEx
{
	OVERLAPPED overlapped{};
	char buf[MAX_BUF]{};
	WSABUF wsaBuf{};
	IOType ioType = IOType::READING;
} OVERLAPPED_EX, * LPOVERLAPPED_EX;


class Session;

typedef struct IOContext
{
	OVERLAPPED_EX overlappedEx{};
	SOCKET sock = INVALID_SOCKET;
	Session* owner = nullptr;
} IOCONTEXT, *LPIOCONTEXT;

typedef class IOContextPool
{
private:
	LockFreePool<IOCONTEXT> pool{};
public:
	IOContextPool(size_t size)
	{ for (int i = 0; i < size; i++) pool.Push(new IOCONTEXT); }

	LPIOCONTEXT Acquire()
	{
		if (LPIOCONTEXT obj = pool.Pop()) return obj;
		else return new IOCONTEXT();
	}

	void Release(LPIOCONTEXT ctx)
	{
		ZeroMemory(&ctx, sizeof(IOCONTEXT));
		pool.Push(ctx);
	}
} IOCONTEXT_POOL;

class alignas(64) Session
{
public:
	DWORD64				sessionID		= 0;
	SOCKET				sock			= INVALID_SOCKET;
	SOCKADDR_IN			sockAdr			= { };
	DWORD				sockAdrIP		= 0;
	USHORT				sockAdrPort		= 0;

	DWORD				timeoutTime		= 0;

	RingBuffer			recvBuf{ MAX_BUF * 2 };
	RingBuffer			sendBuf{ MAX_BUF * 2 };

	IOCONTEXT_POOL		contextPool{ MAX_CONTEXT_POOL_SIZE };

	LockFreePool<Job>	jobPool;
	queue<Job*>			jobQueue;

	SRWLOCK				sl;

	atomic<bool>		alive			= false;

	alignas(64) DWORD	ioCount			= 0x80000000;
	alignas(64) DWORD	ioFlag			= 0;

	Session()
	{ 
		InitializeSRWLock(&sl);
		for (int i = 0; i < MAX_JOB_POOL_SIZE; i++) jobPool.Push(new Job());
	}
	virtual ~Session() {};
	void Push(uint32_t header, vector<char>&& data)
	{
		PacketHeader h;
		h.size = sizeof(PacketHeader) + data.size();
		h.header = header;

		sendBuf.TryPush(reinterpret_cast<const char*>(&h), sizeof(h));
		sendBuf.TryPush(data.data(), data.size());
	}
};

class SessionManager
{
private:
	static constexpr DWORD64 sessionIdBase = 1000000000000001ULL;
	atomic<DWORD64> nextSessionId;

	unordered_map<DWORD64, Session*> sessions;
	mutex mtx;
public:
	SessionManager() : nextSessionId(sessionIdBase) {};

	DWORD64 GenerateSessionId()
	{ return nextSessionId.fetch_add(1, memory_order_relaxed); }

	void Emplace(Session* session)
	{ sessions.emplace(session->sessionID, session); }

	void RemoveSession(DWORD64 id)
	{
		lock_guard<mutex> lock(mtx);

		auto it = sessions.find(id);
		if (it != sessions.end())
		{
			delete it->second;
			sessions.erase(it);
		}
	}
	Session* GetSession(DWORD64 id)
	{
		lock_guard<mutex> lock(mtx);

		auto it = sessions.find(id);
		if (it != sessions.end()) return it->second;
	}
	void CloseSession(DWORD64 id)
	{
		//close
	}
};

class NetCore
{
public:
	atomic<bool> isRunning = false;
	SessionManager sm {};

	SOCKET listenSock = INVALID_SOCKET;
	SOCKADDR_IN listenAdr{};

	HANDLE hComPort = 0;
	LPFN_ACCEPTEX lpAcceptEx{};
	LPFN_GETACCEPTEXSOCKADDRS lpGetAcceptExSockaddrs{};

	CRITICAL_SECTION cs{};
	SRWLOCK sl{};

	vector<HANDLE> pool{};

	int acceptCount = 0;
public:
	NetCore()
	{
		InitializeSRWLock(&sl);
		InitializeCriticalSection(&cs);
	}

	void StartUp(string name, USHORT port, int count = 100);
	void PostRecv(Session* session) const;
	void PostSend(Session* session) const;
	void Parser(Session* session);

	struct ThreadParam
	{
		NetCore* self;
		HANDLE hComPort;
	};
	static unsigned __stdcall ThreadEntry(void* param);

	unsigned __stdcall WorkerThread(HANDLE hComPort);

	virtual ~NetCore() {}
private:
	void LoadAcceptEx();
	void PostAccept() const;
	void OnAccept(LPIOCONTEXT ctx);
protected:
	virtual void OnRecv(LPIOCONTEXT ctx, int byteTrans);
	void OnSend(LPIOCONTEXT ctx);

	virtual void Processing(Session* session) = 0;
	virtual Session* CreateSession(SOCKET sock);
};
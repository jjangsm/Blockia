#pragma once
#include "common.h"
#include "Debug.h"
#include <Common.pb.h>

constexpr auto MAX_BUF = 4096;
constexpr auto MAX_CONTEXT_POOL_SIZE = 8;
constexpr auto MAX_JOB_POOL_SIZE = 16;

using namespace Protocol;

#define IO_ACCEPT IOType::ACCEPT
#define IO_READING IOType::READING
#define IO_WRITING IOType::WRITING

typedef class Job					JOB,				* PJOB;
typedef class Session				SESSION,			* PSESSION;
typedef class OverlappedEx			OVERLAPPED_EX,		* POVERLAPPED_EX;
typedef class IOContext				IOCONTEXT,			* PIOCONTEXT;

template<typename T> class WrappedLockFreePool
{
	static_assert(is_base_of<Initializable, T>::value,
		"T must derive from Initializable");
private:
	LFPOOL<T> pool{};
public:
	WrappedLockFreePool(size_t size)
	{
		for (int i = 0; i < size; i++) pool.Push(new T);
	}

	T* Acquire()
	{
		if (T* obj = pool.Pop()) return obj;
		else return new T();
	}

	void Release(T* obj)
	{
		obj->Init();
		pool.Push(obj);
	}
};
template<typename T> using WLFPOOL = WrappedLockFreePool<T>;

enum class IOType : uint8_t
{
	ACCEPT,
	READING,
	WRITING
};

#pragma pack(push, 1)
typedef struct Header
{
	uint16_t size = 0;
	uint32_t header = 0;
} HEADER;
#pragma pack(pop)

class Job : public Initializable
{
public:
	vector<char> data{};
	uint32_t header = 0;
public:
	Job() { Init(); }
	void Init() override
	{
		ZeroMemory(&data, sizeof(vector<char>));
		header = 0;
	}

	size_t Size() const noexcept { return data.size(); }
};

struct OverlappedEx
{
	OVERLAPPED overlapped{};
	char buf[MAX_BUF]{};
	WSABUF wsaBuf{};
	IOType ioType = IO_READING;
};

class IOContext : public Initializable
{
public:
	OVERLAPPED_EX overlappedEx{};
	SOCKET sock = INVALID_SOCKET;
	PSESSION owner = nullptr;
public:
	IOContext() { Init(); }
	void Init() override
	{
		ZeroMemory(&overlappedEx.overlapped, sizeof(OVERLAPPED));
		ZeroMemory(&overlappedEx.buf, MAX_BUF);

		overlappedEx.wsaBuf.len = 0;
		overlappedEx.wsaBuf.buf = nullptr;

		sock = INVALID_SOCKET;

		owner = nullptr;
	}
	void Init(PSESSION owner, IOType ioType = IO_READING);
};

class alignas(64) Session
{
public:
	DWORD64				sessionID = 0;
	SOCKET				sock = INVALID_SOCKET;
	SOCKADDR_IN			sockAdr = { };
	DWORD				sockAdrIP = 0;
	USHORT				sockAdrPort = 0;

	DWORD				timeoutTime = 0;

	RBUF				recvBuf{ MAX_BUF << 1 };
	RBUF				sendBuf{ MAX_BUF << 1 };

	WLFPOOL<IOCONTEXT>	contextPool{ MAX_CONTEXT_POOL_SIZE };

	WLFPOOL<JOB>		jobPool{ MAX_JOB_POOL_SIZE };
	queue<PJOB>			jobQueue{};

	SRWLOCK				sl{};

	atomic<bool>		alive = false;

	alignas(64) DWORD	ioCount = 0x80000000;
	alignas(64) DWORD	ioFlag = 0;
public:
	Session() = default;
	Session(SOCKET sock, DWORD64 id)
	{
		InitializeSRWLock(&sl);
		this->sock = sock;
		this->sessionID = id;
		this->alive = true;
	}
	virtual ~Session() {};
	void Push(uint32_t header, vector<char>&& data)
	{
		HEADER h;
		h.size = sizeof(HEADER) + data.size();
		h.header = header;

		sendBuf.TryPush(reinterpret_cast<const char*>(&h), sizeof(h));
		sendBuf.TryPush(data.data(), data.size());
	}
};

inline void IOContext::Init(PSESSION owner, IOType ioType)
{
	ZeroMemory(&overlappedEx.overlapped, sizeof(OVERLAPPED));
	ZeroMemory(&overlappedEx.buf, MAX_BUF);

	this->sock = owner->sock;
	this->owner = owner;

	overlappedEx.ioType = ioType;

	overlappedEx.wsaBuf.buf = overlappedEx.buf;
	overlappedEx.wsaBuf.len = sizeof(overlappedEx.buf);
}

class SessionManager
{
private:
	static constexpr DWORD64 sessionIdBase = 1000000000000001ULL;
	atomic<DWORD64> nextSessionId;

	unordered_map<DWORD64, PSESSION> sessions;
	mutex mtx;
public:
	SessionManager() : nextSessionId(sessionIdBase) {};

	DWORD64 GenerateSessionId()
	{ return nextSessionId.fetch_add(1, memory_order_relaxed); }

	void Emplace(PSESSION session)
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
	PSESSION GetSession(DWORD64 id)
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

	vector<HANDLE> threadPool{};

	int acceptCount = 0;
public:
	NetCore()
	{
		InitializeSRWLock(&sl);
		InitializeCriticalSection(&cs);
	}

	void StartUp(string name, USHORT port, int count = 100);
	void PostRecv(PSESSION session) const;
	void PostSend(PSESSION session) const;
	void Parser(PSESSION session);

	typedef struct ThreadParam
	{
		NetCore* self;
		HANDLE hComPort;
	} TPRAM, * PTPRAM;
	static unsigned __stdcall ThreadEntry(void* param);

	unsigned __stdcall WorkerThread(HANDLE hComPort);

	virtual ~NetCore() {}
private:
	void LoadAcceptEx();
	void PostAccept() const;
	void OnAccept(PIOCONTEXT ctx);
protected:
	virtual void OnRecv(PIOCONTEXT ctx, int byteTrans);
	virtual void OnSend(PIOCONTEXT ctx);

	virtual void Processing(PSESSION session) = 0;
	virtual PSESSION CreateSession(SOCKET sock);
};
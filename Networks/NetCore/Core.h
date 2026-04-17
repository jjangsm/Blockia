#pragma once
#include "common.h"
#include "Debug.h"
#include <Common.pb.h>

constexpr auto MAX_BUF =  8192;

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
	uint16_t size;
	uint32_t header;
};
#pragma pack(pop)

class Job
{
private:
	vector<char> data{};
	uint32_t header = 0;
public:
	Job() = default;
	explicit Job(uint32_t h, vector<char>&& d)
		: header(h), data(move(d)) { }

	PACKET_HEADER GetHeader() const noexcept
	{ return static_cast<PACKET_HEADER>(header); }

	const char* GetData() const noexcept { return data.data(); }
	size_t GetSize() const noexcept { return data.size(); }
};

class JobQueue
{
private:
	queue<Job> q;
	mutex mtx;
public:
	void Push(Job&& job)
	{
		lock_guard<mutex> lock(mtx);
		q.push(move(job));
	}

	bool Pop(Job& out)
	{
		lock_guard<mutex> lock(mtx);
		if (q.empty()) return false;

		out = move(q.front());
		q.pop();
		return true;
	}
};

typedef struct OverlappedEx
{
	OVERLAPPED overlapped{};
	char buf[MAX_BUF]{};
	IOType ioType = IOType::READING;
} OVERLAPPED_EX;

struct AcceptContext
{
	OVERLAPPED_EX overlapped;
	SOCKET sock;
	char buf[1024];
};

struct RecvContext
{
	OVERLAPPED_EX overlapped;
	WSABUF buf;
};

struct alignas(64) Session
{
	DWORD64				sessionID		= 0;
	SOCKET				sock			= INVALID_SOCKET;
	SOCKADDR_IN			sockAdr			= { };
	DWORD				sockAdrIP		= 0;
	USHORT				sockAdrPort		= 0;

	DWORD				timeoutTime		= 0;

	RingBuffer			recvBuf{ MAX_BUF };
	RingBuffer			sendBuf{ MAX_BUF };

	JobQueue			jobQueue;

	SRWLOCK				sl;

	atomic<bool>		alive			= false;

	alignas(64) DWORD	ioCount			= 0x80000000;
	alignas(64) DWORD	ioFlag			= 0;

	Session() { InitializeSRWLock(&sl); }
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

	DWORD64 IssueSessionId()
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
};

class NetCore
{
	atomic<bool> isRunning = false;
	SessionManager sm;

	SOCKET listenSock;
	SOCKADDR_IN listenAdr;

	HANDLE hComPort;
	LPFN_ACCEPTEX lpAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS lpGetAcceptExSockaddrs;

	CRITICAL_SECTION cs;
	SRWLOCK sl;

	vector<HANDLE> pool;
private:
	void LoadAcceptEx();
	void PostAccept() const;
	void OnAccept(AcceptContext* ctx);
public:
	void StartUp(string name, USHORT port, int acceptCount);
	void PostRecv(Session* session);
	void Parser(Session* session);

	struct ThreadParam
	{
		NetCore* self;
		HANDLE hComPort;
	};
	static unsigned __stdcall ThreadEntry(void* param);

	unsigned __stdcall WorkerThread(HANDLE hComPort);
protected:
	virtual void Processing(JobQueue* jobQueue);
};
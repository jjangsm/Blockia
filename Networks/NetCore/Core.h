#pragma once
#include "common.h"
#include "Debug.h"
#include <Common.pb.h>

constexpr auto MAX_BUF =  4096;

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

	PacketHeader() {}
	PacketHeader(uint16_t s, uint32_t h) : size(s), header(h) {}
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
	WSABUF wsaBuf;
	IOType ioType = IOType::READING;
} OVERLAPPED_EX;

class Session;
struct IOContext
{
	OVERLAPPED_EX overlappedEx;
	SOCKET sock;
	Session* owner = nullptr;
};

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

	atomic<bool>		sending = false;
	atomic<bool>		recving = false;

	IOContext			recvCtx{};
	IOContext			sendCtx{};

	JobQueue			jobQueue;

	SRWLOCK				sl;

	atomic<bool>		alive			= false;

	alignas(64) DWORD	ioCount			= 0x80000000;
	alignas(64) DWORD	ioFlag			= 0;

	Session() { InitializeSRWLock(&sl); }
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
	NetCore() {}
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
	void OnAccept(IOContext* ctx);
protected:
	virtual void OnRecv(IOContext* ctx, int byteTrans);
	virtual void OnSend(IOContext* ctx);

	virtual void Processing(JobQueue* jobQueue) = 0;
	virtual Session* CreateSession(SOCKET sock);
};
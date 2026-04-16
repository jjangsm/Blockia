#pragma once
#include "common.h"
#include <Common.pb.h>

using namespace Protocol;

enum class IOType : uint8_t
{
	ACCEPT,
	READING,
	WRITING
};

class Job
{
private:
	vector<char> data;
	uint16_t header = 0;
public:
	explicit Job(uint16_t h, vector<char>&& d)
		: header(h), data(move(d)) {}

	explicit Job(PACKET_HEADER h, vector<char>&& d)
		: Job(static_cast<uint16_t>(h), move(d)) {}

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

struct OverlappedEx
{
	OVERLAPPED overlapped;
	char buf[4096];
	IOType ioType;
};
using OVERLAPPED_EX = OverlappedEx;

struct alignas(64) Session
{
	DWORD64				sessionID		= 0;
	SOCKET				sock			= INVALID_SOCKET;
	SOCKADDR_IN			sockAdr			= { 0 };
	DWORD				sockAdrIP		= 0;
	USHORT				sockAdrPort		= 0;

	DWORD				timeoutTime		= 0;

	OVERLAPPED_EX		overlappedEx	= { 0 };

	RingBuffer			recvBuf;
	RingBuffer			sendBuf;

	JobQueue			jobQueue;

	SRWLOCK				sl;

	atomic<bool>		alive			= false;

	alignas(64) DWORD	ioCount			= 0x80000000;
	alignas(64) DWORD	ioFlag			= 0;

	Session() :sessionID(0), sock(INVALID_SOCKET), sockAdr{ 0 },
		sockAdrIP(0), sockAdrPort(0), timeoutTime(0), recvBuf{ 0 },
		sendBuf{ 0 },jobQueue{}, alive(false)
	{ InitializeSRWLock(&sl); }
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

struct AcceptContext
{
	OVERLAPPED_EX overlapped;
	SOCKET sock;
	char buf[64];
};

inline SessionManager sm{};

class NetCore
{
	atomic<bool> isRunning = false;

	SOCKET listenSock;
	SOCKADDR_IN listenAdr;

	HANDLE hComPort;
	LPFN_ACCEPTEX lpAcceptEx;

	CRITICAL_SECTION cs;
	SRWLOCK sl;

	vector<HANDLE> pool;
private:
	void LoadAcceptEx();
	void PostAccept() const;
	void OnAccept(AcceptContext* ctx);
public:
	void StartUp(USHORT port, int acceptCount);
	void PostRecv(Session* session);
	virtual void Processing(Session* session, Job job);

	struct ThreadParam
	{
		NetCore* self;
		HANDLE hComPort;
	};
	static unsigned __stdcall ThreadEntry(void* param);

	unsigned __stdcall WorkerThread(HANDLE hComPort);
};
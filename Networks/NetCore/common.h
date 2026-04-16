#pragma once
#include "pch.h"

using namespace std;


class ScopeLock
{
private:
	CRITICAL_SECTION& m_cs;
public:
	ScopeLock(CRITICAL_SECTION& cs) : m_cs(cs)
	{ EnterCriticalSection(&m_cs); }
	~ScopeLock() { LeaveCriticalSection(&m_cs); }
};

class SRWSharedLock
{
private:
	SRWLOCK& m_sl;
public:
	SRWSharedLock(SRWLOCK& sl) :m_sl(sl)
	{ AcquireSRWLockShared(&m_sl); }
	~SRWSharedLock() { ReleaseSRWLockShared(&m_sl); }
};

class SRWExclusiveLock
{
private:
	SRWLOCK& m_sl;
public:
	SRWExclusiveLock(SRWLOCK& sl) :m_sl(sl)
	{ AcquireSRWLockExclusive(&m_sl); }
	~SRWExclusiveLock() { ReleaseSRWLockExclusive(&m_sl); }
};

class RingBuffer
{
private:
	vector<char> buf;

	struct alignas(64) CacheLineAtomic
	{
		atomic<size_t> value;
		char padding[64 - sizeof(atomic<size_t>)];
	};

	mutable CacheLineAtomic head;
	mutable CacheLineAtomic tail;

	size_t capacity;
	size_t mask;
public:
	explicit RingBuffer(size_t size)
	{
		size_t cap = 1;
		while (cap < size) cap <<= 1;
		
		mask = (capacity = cap) - 1;
		buf.resize(capacity);
	}
public:
	bool TryPush(const char* __restrict data, size_t len)
	{
		if (!len) return true;

		size_t h = head.value.load(memory_order_acquire);
		size_t t = tail.value.load(memory_order_relaxed);

		if (capacity - (t - h) - 1 < len) return false;

		size_t idx = t & mask;

		if (len >= 64)
		{
			_mm_prefetch((char*)&buf[idx], _MM_HINT_T0);
			_mm_prefetch((char*)&buf[0], _MM_HINT_T0);
		}


		size_t resume = capacity - idx;
		size_t first = len < resume ? len : resume;

		memcpy(&buf[idx], data, first);
		memcpy(&buf[0], data + first, len - first);

		tail.value.store(t + len, memory_order_release);
		return true;
	}
	bool TryPop(char* __restrict dest, size_t len)
	{
		if (!len) return true;

		size_t h = head.value.load(memory_order_relaxed);
		size_t t = tail.value.load(memory_order_acquire);

		if (t - h < len) return false;

		size_t idx = h & mask;

		if (len >= 64)
		{
			_mm_prefetch((char*)&buf[idx], _MM_HINT_T0);
			_mm_prefetch((char*)&buf[0], _MM_HINT_T0);
		}

		size_t resume = capacity - idx;
		size_t first = len < resume ? len : resume;

		memcpy(dest, &buf[idx], first);
		memcpy(dest + first, &buf[0], len - first);

		head.value.store(h + len, memory_order_release);
		return true;

	}

	bool Peek(char* __restrict dest, size_t len) const
	{
		if (!len) return true;

		size_t h = head.value.load(memory_order_relaxed);
		size_t t = tail.value.load(memory_order_acquire);

		if ((t - h) < len) return false;

		size_t idx = h & mask;

		if (len >= 64)
		{
			_mm_prefetch((char*)&buf[idx], _MM_HINT_T0);
			_mm_prefetch((char*)&buf[0], _MM_HINT_T0);
		}

		size_t resume = capacity - idx;
		size_t first = len < resume ? len : resume;

		memcpy(dest, &buf[idx], first);
		memcpy(dest + first, &buf[0], len - first);

		return true;
	}
};
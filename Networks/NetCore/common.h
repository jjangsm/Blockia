#pragma once
#include "pch.h"

using namespace std;
using namespace chrono;

typedef class CSScopeLock			CSSPL;
typedef class SRWSharedLock			SRWSL;
typedef class SRWExclusiveLock		SRWEL;

typedef class RingBuffer			RBUF;

class Initializable
{
	virtual void Init() = 0;
};
class CSScopeLock
{
private:
	CRITICAL_SECTION& m_cs;
public:
	CSScopeLock(CRITICAL_SECTION& cs) : m_cs(cs) { EnterCriticalSection(&m_cs); }
	~CSScopeLock() { LeaveCriticalSection(&m_cs); }
};

class SRWSharedLock
{
private:
	SRWLOCK& m_sl;
public:
	SRWSharedLock(SRWLOCK& sl) :m_sl(sl) { AcquireSRWLockShared(&m_sl); }
	~SRWSharedLock() { ReleaseSRWLockShared(&m_sl); }
};

class SRWExclusiveLock
{
private:
	SRWLOCK& m_sl;
public:
	SRWExclusiveLock(SRWLOCK& sl) :m_sl(sl) { AcquireSRWLockExclusive(&m_sl); }
	~SRWExclusiveLock() { ReleaseSRWLockExclusive(&m_sl); }
};

class RingBuffer
{
private:
	vector<char> buf;

	struct alignas(64) CacheLineAtomic
	{
		atomic<size_t> value = 0;
		char padding[64 - sizeof(atomic<size_t>)]{};
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
	size_t Size() const
	{
		size_t h = head.value.load(memory_order_relaxed);
		size_t t = tail.value.load(memory_order_acquire);
		return t - h;
	}
};

template<typename T>
class LockFreePool
{
	struct Node
	{
		T* data;
		Node* next;
	};

	struct TaggedPtr
	{
		Node* ptr;
		uint64_t tag;
	};

	atomic<TaggedPtr> head;

public:
	LockFreePool()
	{ head.store({ nullptr, 0 }, std::memory_order_relaxed); }

	void Push(T* obj)
	{
		Node* node = new Node{ obj, nullptr };

		TaggedPtr oldHead = head.load(std::memory_order_acquire);

		while (true)
		{
			node->next = oldHead.ptr;

			TaggedPtr newHead;
			newHead.ptr = node;
			newHead.tag = oldHead.tag + 1;

			if (head.compare_exchange_weak(
				oldHead,
				newHead,
				std::memory_order_release,
				std::memory_order_acquire)) return;
		}
	}

	T* Pop()
	{
		TaggedPtr oldHead = head.load(std::memory_order_acquire);

		while (oldHead.ptr)
		{
			Node* next = oldHead.ptr->next;

			TaggedPtr newHead;
			newHead.ptr = next;
			newHead.tag = oldHead.tag + 1;

			if (head.compare_exchange_weak(
				oldHead,
				newHead,
				std::memory_order_acq_rel,
				std::memory_order_acquire))
			{
				T* result = oldHead.ptr->data;

				delete oldHead.ptr;

				return result;
			}
		}

		return nullptr;
	}
};
template<typename T> using LFPOOL = LockFreePool<T>;
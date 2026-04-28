#pragma once
#include <iostream>
#include <Common.pb.h>
#include <Core.h>

#include <mysql.h>
#include <curl/curl.h>

#include "TLS.h"

using namespace std;
using namespace Protocol;

class GatewayServer : public NetCore
{
public:
	class TLSSession : public Session
	{
	public:
		SSL* ssl = {};
		BIO* rbio = {};
		BIO* wbio = {};
		bool handshakeDone = false;
	};

	void Processing(Session* session) override
	{
		while (!session->jobQueue.empty())
		{
			Job* job = session->jobQueue.front();
			session->jobQueue.pop();

			switch (job->header)
			{
			case PKT_REQ_LOGIN:
			{
				LoginData login;
				login.ParseFromString(job->data.data());

				LOG_INFO(login.id());
				LOG_INFO(login.mail());
				LOG_INFO(login.password());
				break;
			}
			}
			session->jobPool.Push(job);
		}
	}
	void OnRecv(IOContext* ctx, int byteTrans)
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
		
		TLSSession* session = static_cast<TLSSession*>(ctx->owner);

		BIO_write(session->rbio, ctx->overlappedEx.buf, byteTrans);

		if (!session->handshakeDone)
		{
			int ret = SSL_accept(session->ssl);

			vector<char> buf(MAX_BUF);

			while (true)
			{
				int outLen = BIO_read(session->wbio, buf.data(), MAX_BUF);
				if (outLen <= 0) break;

				LPIOCONTEXT cx = session->contextPool.Acquire();
				cx->sock = session->sock;
				cx->owner = session;

				memcpy(cx->overlappedEx.buf, buf.data(), outLen);

				ZeroMemory(&cx->overlappedEx.overlapped, sizeof(OVERLAPPED));
				cx->overlappedEx.ioType = IOType::WRITING;

				cx->overlappedEx.wsaBuf.buf = cx->overlappedEx.buf;
				cx->overlappedEx.wsaBuf.len = outLen;

				if (WSASend(cx->sock, &cx->overlappedEx.wsaBuf, 1, NULL, 0,
					&cx->overlappedEx.overlapped, NULL) == SOCKET_ERROR) PRINT_LAST_WSA_EXCAPTION;
			}

			if (ret == 1) session->handshakeDone = true;
			else
			{
				int err = SSL_get_error(session->ssl, ret);

				if (err == SSL_ERROR_WANT_READ) PostRecv(session);
				else if (err != SSL_ERROR_WANT_WRITE)
				{
					auto alive = session->alive.load(memory_order_relaxed);
					if (!alive) return;

					session->alive.store(false, memory_order_release);

					if (session->ssl)
					{
						auto str = TLS::GetOpenSSLError();
						if (str != "") LOG_EXCAPTION(str);

						SSL_shutdown(session->ssl);
						SSL_free(session->ssl);
						session->ssl = nullptr;
					}

					//disconnect
				}
			}
			if (session->handshakeDone) PostRecv(session);
		}
		else
		{
			ctx->owner->recvBuf.TryPush(ctx->overlappedEx.buf, byteTrans);
			Parser(ctx->owner);
			Processing(ctx->owner);
			PostRecv(ctx->owner);
		}

	}
	void OnSend(LPIOCONTEXT ctx) override
	{
		TLSSession* session = static_cast<TLSSession*>(ctx->owner);

		if (session->handshakeDone) NetCore::OnSend(ctx);
		else
		{
			SRWExclusiveLock lock(session->sl);
			session->contextPool.Release(ctx);
		}
	}
	Session* CreateSession(SOCKET sock) override
	{
		TLSSession* session = new TLSSession();

		session->sock = sock;
		session->sessionID = sm.GenerateSessionId();
		session->alive = true;

		session->ssl = SSL_new(g_ctx);

		session->rbio = BIO_new(BIO_s_mem());
		session->wbio = BIO_new(BIO_s_mem());

		SSL_set_bio(session->ssl, session->rbio, session->wbio);
		SSL_set_accept_state(session->ssl);

		return session;
	}
};
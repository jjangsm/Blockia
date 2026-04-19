#pragma once
#include <iostream>
#include <Common.pb.h>
#include <Core.h>

#include <mysql.h>

#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include <curl/curl.h>

using namespace std;
using namespace Protocol;

class GatewayServer : public NetCore
{
public:
	void Processing(JobQueue* jobQueue) override
	{
		Job job;
		PACKET_HEADER header;
		const char* data;
		while (jobQueue->Pop(job))
		{
			header = job.GetHeader();
			data = job.GetData();

			switch (job.GetHeader())
			{
			case PKT_REQ_LOGIN:
			{
				LoginData login;
				login.ParseFromString(data);

				LOG_INFO(login.id());
				LOG_INFO(login.mail());
				LOG_INFO(login.password());
				break;
			}
			}
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
			int err = SSL_get_error(session->ssl, ret);

			vector<char> buf(MAX_BUF);

			while (true)
			{
				int outLen = BIO_read(session->wbio, buf.data(), buf.size());
				if (outLen < 0) break;

				session->Push(static_cast<uint32_t>(PKT_HANDSHAKE), move(buf));

				PostSend(session);
			}

			if (ret == 1) session->handshakeDone = true;
			else
			{
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

	}
	void OnSend(IOContext* ctx) override
	{
		SRWExclusiveLock lock(ctx->owner->sl);

		if (ctx->owner->sending) return;

		//need excaption

		if (ctx->owner->sendBuf.Size() < sizeof(PacketHeader)) return;

		PacketHeader header;
		ZeroMemory(&header, sizeof(header));

		if (!ctx->owner->sendBuf.TryPop((char*)&header, sizeof(header))) return;

		if (ctx->owner->sendBuf.Size() < header.size - 6) return;

		if (!ctx->owner->sendBuf.TryPop(ctx->owner->sendCtx.overlappedEx.buf, header.size)) return;

		ZeroMemory(&ctx->owner->sendCtx.overlappedEx.overlapped, sizeof(OVERLAPPED));

		ctx->owner->sendCtx.overlappedEx.wsaBuf.buf =
			ctx->owner->sendCtx.overlappedEx.buf;
		ctx->owner->sendCtx.overlappedEx.wsaBuf.len = header.size;

		ctx->owner->sending = true;

		WSASend(ctx->owner->sock, &ctx->owner->sendCtx.overlappedEx.wsaBuf, 1, NULL, 0,
			&ctx->owner->sendCtx.overlappedEx.overlapped, NULL);
	}
	Session* CreateSession(SOCKET sock) override
	{
		TLSSession* session = new TLSSession();
		session->ssl = SSL_new(g_ctx);

		session->rbio = BIO_new(BIO_s_mem());
		session->wbio = BIO_new(BIO_s_mem());

		SSL_set_bio(session->ssl, session->rbio, session->wbio);
		SSL_set_accept_state(session->ssl);
		return session;
	}
};

class TLSSession : public Session
{
public:
	SSL* ssl = {};
	BIO* rbio = {};
	BIO* wbio = {};
	bool handshakeDone = false;
};
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
			case PKT_UNKNOWN:
			{
				string str(data, job.GetSize());

				LOG_INFO(str);
				break;
			}
			}
		}
	}
};
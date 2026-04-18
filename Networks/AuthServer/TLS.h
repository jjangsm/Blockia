#pragma once
#include "Network.h"

inline SSL_CTX* g_ctx;

class TLS
{
public:
	static void InitTLS();
	static SSL_CTX* CreateContext();
	static string GetOpenSSLError();
};
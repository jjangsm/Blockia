#pragma 
#include <iostream>
#include <string>

#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

using namespace std;

inline SSL_CTX* g_ctx;

class TLS
{
public:
	static void InitTLS();
	static SSL_CTX* CreateContext();
	static string GetOpenSSLError();
};
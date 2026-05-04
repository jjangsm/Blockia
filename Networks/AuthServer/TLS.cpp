#include "TLS.h"

void TLS::InitTLS()
{
	SSL_load_error_strings();
	OpenSSL_add_ssl_algorithms();

	g_ctx = TLS::CreateContext();

	SSL_CTX_set_options(g_ctx, SSL_OP_NO_TLSv1_3);
	SSL_CTX_set_min_proto_version(g_ctx, TLS1_2_VERSION);
	SSL_CTX_set_max_proto_version(g_ctx, TLS1_2_VERSION);

	if (SSL_CTX_use_certificate_file(g_ctx, "server.crt", SSL_FILETYPE_PEM) <= 0)
	{
		GetOpenSSLError();
		exit(1);
	}
	if (SSL_CTX_use_PrivateKey_file(g_ctx, "server.key", SSL_FILETYPE_PEM) <= 0)
	{
		GetOpenSSLError();
		exit(1);
	}
	if (SSL_CTX_load_verify_locations(g_ctx, "ca.crt", nullptr) <= 0)
	{
		GetOpenSSLError();
		exit(1);
	}
}

SSL_CTX* TLS::CreateContext()
{
	SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
	if (!ctx)
	{
		GetOpenSSLError();
		exit(1);
	}
	return ctx;
}

string TLS::GetOpenSSLError()
{
	BIO* bio = BIO_new(BIO_s_mem());

	ERR_print_errors(bio);

	char* data;
	long len = BIO_get_mem_data(bio, &data);

	string result(data, len);

	if (!result.empty() && result.back() == '\n') result.pop_back();
	BIO_free(bio);

	return result;
}

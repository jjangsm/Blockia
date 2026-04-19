#include "Network.h"

int main()
{
	NetCore* core = new GatewayServer();

	TLS::InitTLS();

	core->StartUp("Gateway Server", 8080, 100);

	while (true)
	{
		Sleep(1000);
	}
}
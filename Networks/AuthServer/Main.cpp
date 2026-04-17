#include "Network.h"

int main()
{
	NetCore core;

	core.StartUp("Gateway Server", 8080, 100);

	while (true)
	{
		Sleep(1000);
	}
}
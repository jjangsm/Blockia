#pragma once
#include <iostream>
#include <Common.pb.h>
#include <Core.h>

using namespace std;
using namespace Protocol;

class GatewayServer : public NetCore
{
	void Processing(Session* session, Job job) override
	{

	}
};
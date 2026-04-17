#ifndef PCH_H
#define PCH_H

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <queue>
#include <memory>
#include <chrono>
#include <atomic>
#include <condition_variable>

#include <WinSock2.h>
#include <Windows.h>
#include <MSWSock.h>
#include <ws2tcpip.h>

#include <json.hpp>

#pragma comment(lib, "ws2_32.lib")

#endif
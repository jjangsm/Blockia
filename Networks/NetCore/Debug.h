#pragma once
#include "common.h"

#define LOG_UNKNOWN(msg) Debug::PrintLog(DebugLvl::UNKNOWN, msg);
#define LOG_INFO(msg) Debug::PrintLog(DebugLvl::INFO, msg);
#define LOG_DETAIL(msg) Debug::PrintLog(DebugLvl::DETAIL, msg);
#define LOG_WARNING(msg) Debug::PrintLog(DebugLvl::DETAIL, msg);
#define LOG_EXCAPTION(msg) Debug::PrintLog(DebugLvl::EXCAPTION, msg);

#define PRINT_LAST_WSA_EXCAPTION Debug::PrintWSAGetLastError();
#define PRINT_LAST_EXCAPTION Debug::PrintGetLastErrorStr();

enum class DebugLvl : uint8_t
{ UNKNOWN, INFO, DETAIL, WARNING, EXCAPTION };

inline mutex d_mtx;

class Debug
{
private:
	static const char* get_time();
	static const char* lvlString(DebugLvl lvl);
public:
	static void PrintLogo(string name);
	static void PrintLog(DebugLvl lvl, string msg);
	static void PrintWSAGetLastError();
	static void PrintGetLastErrorStr();
};
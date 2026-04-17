#include "pch.h"
#include "Debug.h"


void Debug::PrintLogo(string name)
{
	//test
	fputs(name.data(), stdout);
	fputc('\n', stdout);
}

void Debug::PrintLog(DebugLvl lvl, string msg)
{
	lock_guard<mutex> lock(d_mtx);
	const char* lvlStr = lvlString(lvl);

	fputc('[', stdout);
	fputs(get_time(), stdout);
	fputs(" LOG ", stdout);

	fputs(lvlStr, stdout);
	fputs("] ", stdout);

	fputs(msg.data(), stdout);
	fputc('\n', stdout);
}
void Debug::PrintWSAGetLastError()
{
	int err = WSAGetLastError();
	if (err == WSA_IO_PENDING) return;

	char* s = nullptr;
	FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, err, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPSTR)&s, 0, nullptr);

	string message = s;
	LocalFree(s);
	LOG_EXCAPTION(message);
}

void Debug::PrintGetLastErrorStr()
{
	DWORD errorMessageID = GetLastError();
	if (errorMessageID == 0) return;

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		(LPSTR)&messageBuffer, 0, NULL);

	string message(messageBuffer, size);

	LocalFree(messageBuffer);

	LOG_EXCAPTION(message);
}

const char* Debug::get_time()
{
	static string str;


	auto now = system_clock::now();

	auto seconds = system_clock::to_time_t(now);
	tm* local = localtime(&seconds);

	auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

	ostringstream oss;
	oss << put_time(local, "%H:%M:%S")
		<< '.' << setw(3) << setfill('0') << ms.count();

	str = oss.str();
	return str.c_str();
}

const char* Debug::lvlString(DebugLvl lvl)
{
	switch (lvl)
	{
	default: case DebugLvl::UNKNOWN: return "UNKNOWN";
	case DebugLvl::INFO: return "INFO";
	case DebugLvl::DETAIL: return "DETAIL";
	case DebugLvl::WARNING: return "WARNING";
	case DebugLvl::EXCAPTION: return "EXCAPTION";
	}
}

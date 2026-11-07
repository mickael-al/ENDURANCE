#include "ProcessGuard.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#endif

#include <atomic>
#include <cwchar>
#include <mutex>

namespace
{
#if defined(_WIN32)
	constexpr DWORD MonitorInitialDelayMs = 1500;
	constexpr DWORD MonitorPeriodMs = 4000;
	HANDLE g_monitorTimer = nullptr;
	std::atomic_flag g_monitorCallbackActive = ATOMIC_FLAG_INIT;

	using NtQueryInformationProcessFunction = LONG (NTAPI*)(
		HANDLE,
		ULONG,
		PVOID,
		ULONG,
		PULONG);

	bool startsWithInsensitive(
		const wchar_t* value,
		const wchar_t* prefix)
	{
		if (value == nullptr || prefix == nullptr)
		{
			return false;
		}
		const std::size_t prefixLength = std::wcslen(prefix);
		return std::wcslen(value) >= prefixLength &&
			_wcsnicmp(value, prefix, prefixLength) == 0;
	}

	bool isForbiddenExecutable(const wchar_t* executableName)
	{
		// Intentionally narrow: these are dedicated memory editors rather than
		// generic developer tools, overlays, debuggers or monitoring software.
		return startsWithInsensitive(executableName, L"cheatengine") ||
			startsWithInsensitive(executableName, L"cheat engine") ||
			startsWithInsensitive(executableName, L"ceserver") ||
			startsWithInsensitive(executableName, L"artmoney") ||
			startsWithInsensitive(executableName, L"squalr") ||
			startsWithInsensitive(executableName, L"scanmem") ||
			startsWithInsensitive(executableName, L"gameconqueror");
	}

	bool hasNativeDebuggerIndicators()
	{
		const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
		if (ntdll == nullptr)
		{
			return false;
		}
		const auto queryProcess =
			reinterpret_cast<NtQueryInformationProcessFunction>(
				::GetProcAddress(ntdll, "NtQueryInformationProcess"));
		if (queryProcess == nullptr)
		{
			return false;
		}

		constexpr ULONG ProcessDebugPort = 7;
		constexpr ULONG ProcessDebugObjectHandle = 30;
		constexpr ULONG ProcessDebugFlags = 31;
		HANDLE process = ::GetCurrentProcess();

		ULONG_PTR debugPort = 0;
		if (queryProcess(
			process,
			ProcessDebugPort,
			&debugPort,
			static_cast<ULONG>(sizeof(debugPort)),
			nullptr) >= 0 && debugPort != 0)
		{
			return true;
		}

		HANDLE debugObject = nullptr;
		if (queryProcess(
			process,
			ProcessDebugObjectHandle,
			&debugObject,
			static_cast<ULONG>(sizeof(debugObject)),
			nullptr) >= 0 && debugObject != nullptr)
		{
			return true;
		}

		ULONG debugFlags = 1;
		return queryProcess(
			process,
			ProcessDebugFlags,
			&debugFlags,
			static_cast<ULONG>(sizeof(debugFlags)),
			nullptr) >= 0 && debugFlags == 0;
	}

	VOID CALLBACK monitorCallback(PVOID, BOOLEAN)
	{
		if (g_monitorCallbackActive.test_and_set(std::memory_order_acquire))
		{
			return;
		}
		ProcessGuard::enforceNoDebugger();
		g_monitorCallbackActive.clear(std::memory_order_release);
	}
#endif
}

namespace ProcessGuard
{
	bool isProtectionEnabled()
	{
#if defined(_WIN32) && defined(EDITOR_RUN_MODE) && defined(ANTAGONIZ_RELEASE)
		return true;
#else
		return false;
#endif
	}

	bool isDebuggerAttached()
	{
#if defined(_WIN32)
		if (::IsDebuggerPresent() != FALSE)
		{
			return true;
		}

		BOOL remoteDebugger = FALSE;
		if (::CheckRemoteDebuggerPresent(
			::GetCurrentProcess(),
			&remoteDebugger) != FALSE && remoteDebugger != FALSE)
		{
			return true;
		}
		return hasNativeDebuggerIndicators();
#else
		return false;
#endif
	}

	bool isForbiddenToolRunning()
	{
#if defined(_WIN32)
		const HANDLE snapshot = ::CreateToolhelp32Snapshot(
			TH32CS_SNAPPROCESS,
			0);
		if (snapshot == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		bool found = false;
		if (::Process32FirstW(snapshot, &entry) != FALSE)
		{
			do
			{
				if (entry.th32ProcessID != ::GetCurrentProcessId() &&
					isForbiddenExecutable(entry.szExeFile))
				{
					found = true;
					break;
				}
			}
			while (::Process32NextW(snapshot, &entry) != FALSE);
		}
		::CloseHandle(snapshot);
		return found;
#else
		return false;
#endif
	}

	void enforceNoDebugger()
	{
#if defined(_WIN32) && defined(EDITOR_RUN_MODE) && defined(ANTAGONIZ_RELEASE)
		if (isDebuggerAttached() || isForbiddenToolRunning())
		{
			::ExitProcess(ERROR_ACCESS_DENIED);
		}
#endif
	}

	void startMonitoring()
	{
#if defined(_WIN32) && defined(EDITOR_RUN_MODE) && defined(ANTAGONIZ_RELEASE)
		enforceNoDebugger();
		static std::once_flag startFlag;
		std::call_once(
			startFlag,
			[]()
			{
				::CreateTimerQueueTimer(
					&g_monitorTimer,
					nullptr,
					monitorCallback,
					nullptr,
					MonitorInitialDelayMs,
					MonitorPeriodMs,
					WT_EXECUTEDEFAULT);
			});
#endif
	}
}

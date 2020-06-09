#include "stdafx.h"
#include "Service.h"

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);

wchar_t g_serviceName[] = L"EvtPost";
wchar_t g_serviceDisplayName[] = L"EvtPost service";

DWORD Install();
DWORD Uninstall();
extern void ReadLogs();
extern void UploadLogs();

int _tmain(int argc, _TCHAR* argv[])
{
	if (argc > 1) {
		constexpr auto switches = _T("-/"), installFlags = _T("iI"), uninstallFlags = _T("uU"), testFlags = _T("tT");

		if (argv) {
			if (const auto& param = argv[1]) {
				if (wcschr(switches, param[0])) {
					if (wcschr(installFlags, param[1]))
						return Install();

					if (wcschr(uninstallFlags, param[1]))
						return Uninstall();

					if (wcschr(testFlags, param[1])) {
						ReadLogs();
						UploadLogs();
						return 0;
					}
				}
			}
		}

		wprintf(L"-i to install, -u to uninstall, -t to do a single run (read logs, upload)\nOnce the service is installed, you can start it using service.msc, or by running net start %s\n", g_serviceName);
		return ERROR_INVALID_PARAMETER;
	}

	const SERVICE_TABLE_ENTRY ServiceTable[] = { { &g_serviceName[0], (LPSERVICE_MAIN_FUNCTION)ServiceMain }, { NULL, NULL } };

	if (StartServiceCtrlDispatcher(ServiceTable) == FALSE) {
		return GetLastError();
	}

	return 0;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
	try {
		NTService service(g_serviceName);
		NTService::ServiceMainImpl(service);
	}
	catch (...) {
	}
}


DWORD Install()
{
	wchar_t wszPath[MAX_PATH]{};

	if (GetModuleFileName(nullptr, wszPath, _countof(wszPath)) == 0) {
		const auto err = GetLastError();
		wprintf(L"GetModuleFileName failed w/err 0x%08lx\n", err);
		return err;
	}

	using SvcHandlePtr = std::unique_ptr<std::remove_pointer<SC_HANDLE>::type, decltype(&CloseServiceHandle)>;

	SvcHandlePtr pManager(OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE), CloseServiceHandle);
	if (!pManager) {
		const auto err = GetLastError();
		wprintf(L"OpenSCManager failed w/err 0x%08lx\n", err);
		return err;
	}

	SvcHandlePtr pService(CreateServiceW(pManager.get(), g_serviceName, g_serviceDisplayName, SERVICE_QUERY_STATUS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, wszPath, nullptr, nullptr, nullptr, nullptr, nullptr),
						  CloseServiceHandle);
	if (pService == nullptr) {
		const auto err = GetLastError();
		wprintf(L"CreateService failed w/err 0x%08lx\n", err);
		return err;
	}

	wprintf(L"%s is installed.\n", g_serviceDisplayName);
	return 0;
}

DWORD Uninstall()
{
	using SvcHandlePtr = std::unique_ptr<std::remove_pointer<SC_HANDLE>::type, decltype(&CloseServiceHandle)>;

	SvcHandlePtr pManager(OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE), CloseServiceHandle);
	if (!pManager) {
		const auto err = GetLastError();
		wprintf(L"OpenSCManager failed w/err 0x%08lx\n", err);
		return err;
	}

	SvcHandlePtr pService(OpenServiceW(pManager.get(), g_serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE), CloseServiceHandle);
	if (pService == nullptr) {
		const auto err = GetLastError();
		wprintf(L"CreateService failed w/err 0x%08lx\n", err);
		return err;
	}

	SERVICE_STATUS status{};
	if (ControlService(pService.get(), SERVICE_CONTROL_STOP, &status)) {
		wprintf(L"Stopping %s.", g_serviceName);
		Sleep(1000);

		while (QueryServiceStatus(pService.get(), &status)) {
			if (status.dwCurrentState == SERVICE_STOP_PENDING) {
				wprintf(L".");
				Sleep(1000);
			}
			else
				break;
		}

		if (status.dwCurrentState == SERVICE_STOPPED) {
			wprintf(L"\n%s is stopped.\n", g_serviceName);
		}
		else {
			wprintf(L"\n%s failed to stop.\n", g_serviceName);
		}
	}

	if (!DeleteService(pService.get())) {
		const auto err = GetLastError();
		wprintf(L"DeleteService failed w/err 0x%08lx\n", err);
		return err;
	}

	wprintf(L"%s is removed.\n", g_serviceDisplayName);

	return 0;
}
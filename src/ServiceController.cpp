#include "ServiceController.hpp"
#include <shellapi.h>
#include <iostream>

namespace kfdpi {

static std::function<void()> g_worker_func;
static SERVICE_STATUS g_svc_status;
static SERVICE_STATUS_HANDLE g_svc_handle = NULL;
static std::string g_svc_name;

static VOID WINAPI ServiceCtrlHandler(DWORD request) {
    switch (request) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_svc_status.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_svc_handle, &g_svc_status);
            g_svc_status.dwCurrentState = SERVICE_STOPPED;
            SetServiceStatus(g_svc_handle, &g_svc_status);
            break;
        default:
            break;
    }
}

static VOID WINAPI ServiceMain(DWORD, LPSTR*) {
    g_svc_handle = RegisterServiceCtrlHandlerA(g_svc_name.c_str(), ServiceCtrlHandler);
    if (!g_svc_handle) return;

    g_svc_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_svc_status.dwCurrentState = SERVICE_START_PENDING;
    g_svc_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_svc_status.dwWin32ExitCode = 0;
    g_svc_status.dwServiceSpecificExitCode = 0;
    g_svc_status.dwCheckPoint = 0;
    g_svc_status.dwWaitHint = 0;
    SetServiceStatus(g_svc_handle, &g_svc_status);

    g_svc_status.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_svc_handle, &g_svc_status);

    if (g_worker_func) {
        g_worker_func();
    }

    g_svc_status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_svc_handle, &g_svc_status);
}

bool ServiceController::IsAdmin() {
    BOOL is_adm = FALSE;
    PSID admin_group = NULL;
    SID_IDENTIFIER_AUTHORITY nt_auth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt_auth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
        CheckTokenMembership(NULL, admin_group, &is_adm);
        FreeSid(admin_group);
    }
    return is_adm == TRUE;
}

bool ServiceController::Elevate(int argc, char* argv[]) {
    char szPath[MAX_PATH];
    if (!GetModuleFileNameA(NULL, szPath, MAX_PATH)) return false;

    std::string args;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) args += " ";
        args += argv[i];
    }

    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "runas";
    sei.lpFile = szPath;
    sei.lpParameters = args.c_str();
    sei.nShow = SW_NORMAL;

    return ShellExecuteExA(&sei) == TRUE;
}

bool ServiceController::Install(const std::string& name, const std::string& display_name, const std::string& exe_path) {
    SC_HANDLE sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sc_manager) return false;

    std::string bin_path = "\"" + exe_path + "\" --service";

    SC_HANDLE service = CreateServiceA(
        sc_manager,
        name.c_str(),
        display_name.c_str(),
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        bin_path.c_str(),
        NULL, NULL, NULL, NULL, NULL
    );

    if (!service) {
        CloseServiceHandle(sc_manager);
        return false;
    }

    SERVICE_DESCRIPTIONA desc;
    char desc_str[] = "Автономная C++ служба обхода DPI (YouTube 4K, Discord, сайты, CS2/FACEIT/Xplay)";
    desc.lpDescription = desc_str;
    ChangeServiceConfig2A(service, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return true;
}

bool ServiceController::Uninstall(const std::string& name) {
    SC_HANDLE sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sc_manager) return false;

    SC_HANDLE service = OpenServiceA(sc_manager, name.c_str(), SERVICE_STOP | DELETE);
    if (!service) {
        CloseServiceHandle(sc_manager);
        return false;
    }

    SERVICE_STATUS status;
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    BOOL res = DeleteService(service);

    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return res == TRUE;
}

bool ServiceController::Start(const std::string& name) {
    SC_HANDLE sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sc_manager) return false;

    SC_HANDLE service = OpenServiceA(sc_manager, name.c_str(), SERVICE_START);
    if (!service) {
        CloseServiceHandle(sc_manager);
        return false;
    }

    BOOL res = StartServiceA(service, 0, NULL);
    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return res == TRUE;
}

bool ServiceController::Stop(const std::string& name) {
    SC_HANDLE sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!sc_manager) return false;

    SC_HANDLE service = OpenServiceA(sc_manager, name.c_str(), SERVICE_STOP);
    if (!service) {
        CloseServiceHandle(sc_manager);
        return false;
    }

    SERVICE_STATUS status;
    BOOL res = ControlService(service, SERVICE_CONTROL_STOP, &status);
    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return res == TRUE;
}

int ServiceController::RunAsService(const std::string& name, std::function<void()> worker_func) {
    g_svc_name = name;
    g_worker_func = worker_func;

    SERVICE_TABLE_ENTRYA dispatch_table[] = {
        { const_cast<LPSTR>(g_svc_name.c_str()), (LPSERVICE_MAIN_FUNCTIONA)ServiceMain },
        { NULL, NULL }
    };
    return StartServiceCtrlDispatcherA(dispatch_table) ? 0 : 1;
}

} // namespace kfdpi

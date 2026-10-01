#include <fstream>
#include <sstream>
#include <map>
#include <dirent.h>
#include <thread>
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include "resource.h"

#define APPWM_ICONNOTIFY (WM_APP + 1)
#define APPWM_ICONNOTIFY_RESTORE (WM_APP + 2)

HINSTANCE hInst;
HWND mainDlg;
NOTIFYICONDATA nid;
HANDLE hMutexHandle;
PROCESS_INFORMATION execApache = {};
PROCESS_INFORMATION execMySQL = {};

std::ifstream fi;
std::ofstream fo;
std::map<int, std::string> binPath = {
    {3, "php"},
    {7, "mysql"}
};
std::map<std::string, std::string> config = {
    {"php", ""},
    {"apache_host", "127.0.0.1"},
    {"apache_port", "80"},
    {"mysql", ""},
    {"mysql_host", "127.0.0.1"},
    {"mysql_port", "3306"}
};
std::string appPath = "";
int configType = 0;
char buffer[MAX_PATH];
bool isServerRun = false;
bool isDatabaseRun = false;

std::map<std::string, std::string> GetApachePhpModule()
{
    DIR *dir;
    struct dirent *ent;
    std::string phpPath = appPath + "\\bin\\php\\" + config["php"];
    std::map<std::string, std::string> ret = {
        {"name", ""},
        {"file", ""}
    };

    if ((dir = opendir(phpPath.c_str())) != NULL)
    {
        // print all the directories within directory
        while ((ent = readdir(dir)) != NULL)
        {
            std::string fileName = std::string(ent->d_name);
            std::string lowerFileName = fileName;

            for(auto &c : lowerFileName)
            {
               c = std::tolower(c);
            }

            if (
                lowerFileName.find("php") != std::string::npos &&
                lowerFileName.find("apache") != std::string::npos &&
                lowerFileName.find(".dll") != std::string::npos
            )
            {
                fi.open(std::string(phpPath + "\\" + fileName).c_str());
                if (fi.is_open())
                {
                    ret["file"] = fileName;

                    int firstPos = lowerFileName.find("php") + 3; // +3 from std::string("php").length()
                    int lastPos = lowerFileName.find("apache");
                    bool isNumber = true;
                    std::string phpVer = lowerFileName.substr(firstPos, lastPos - firstPos);

                    for (char const &ch : phpVer)
                    {
                        if (std::isdigit(ch) == 0)
                        {
                            isNumber = false;
                        }
                    }

                    ret["name"] = "php" + phpVer + "_module";
                    if (isNumber && atoi(phpVer.c_str()) > 7) // php 8 and above
                    {
                        ret["name"] = "php_module";
                    }
                }
                fi.close();
            }
        }
        closedir(dir);
    }

    return ret;
}

std::map<std::string, std::string> GetReplaceText()
{
    std::string slashPath = appPath;

    while (slashPath.find("\\") != std::string::npos)
    {
        slashPath.replace(slashPath.find("\\"), 1, "/");
    }

    std::map<std::string, std::string> phpModule = GetApachePhpModule();

    return {
        {"{APACHE_PATH}", slashPath + "/bin/apache"},
        {"{APACHE_HOST}", config["apache_host"]},
        {"{APACHE_PORT}", config["apache_port"]},
        {"{PHP_PATH}", slashPath + "/bin/php/" + config["php"]},
        {"{PHP_MODULE_NAME}", phpModule["name"]},
        {"{PHP_MODULE_FILE}", phpModule["file"]},
        {"{MYSQL_PATH}", slashPath + "/bin/mysql/" + config["mysql"]},
        {"{MYSQL_HOST}", config["mysql_host"]},
        {"{MYSQL_PORT}", config["mysql_port"]},
        {"{TMP_PATH}", slashPath + "/tmp"},
        {"{WWW_PATH}", slashPath + "/www"}
    };
}

void ReadWriteSettings(bool rewrite = false)
{
    std::string configFile = appPath + "\\wpAMP.ini";

    std::ifstream fi(configFile);

    if (!fi.is_open() || rewrite)
    {
        fi.close();
        fo.open(configFile, std::ios::out | std::ios::trunc);
        fo << "php=" << config["php"].c_str() << "\n";
        fo << "apache_host=" << config["apache_host"].c_str() << "\n";
        fo << "apache_port=" << config["apache_port"].c_str() << "\n";
        fo << "mysql=" << config["mysql"].c_str() << "\n";
        fo << "mysql_host=" << config["mysql_host"].c_str() << "\n";
        fo << "mysql_port=" << config["mysql_port"].c_str();
        fo.close();
        std::ifstream fi(configFile);
    }

    std::string line;
    while (std::getline(fi, line))
    {
        std::istringstream is_line(line);
        std::string key;
        if (std::getline(is_line, key, '='))
        {
            std::string value;
            if (std::getline(is_line, value))
            {
                if (config.find(key) != config.end() && value.length() > 0)
                {
                    config[key] = value;
                }
            }
        }
    }

    fi.close();
}

void AppendLog(const std::string& line)
{
    HWND hLog = GetDlgItem(mainDlg, 10);
    int len = GetWindowTextLengthA(hLog);
    SendMessageA(hLog, EM_SETSEL, len, len);
    SendMessageA(hLog, EM_REPLACESEL, 0, (LPARAM)(line + "\r\n").c_str());
    SendMessageA(hLog, EM_SCROLLCARET, 0, 0);
    SendMessageA(hLog, EM_SETSEL, -1, -1);
}

void OpenConfigFile(std::string filePath)
{
    fi.open(filePath);
    if (!fi.is_open())
    {
        MessageBoxA(mainDlg, std::string("Can't find \"" + filePath + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        fi.close();
        return;
    }
    fi.close();
    ShellExecuteA(NULL, "open", "notepad.exe", filePath.c_str(), NULL, SW_NORMAL);
    ExitThread(0);
}

void EnableDisableItem(bool enabled, const char *label = "", int item = 0)
{
    for (int i = 0; i < 10; i++)
    {
        EnableWindow(GetDlgItem(mainDlg, i), enabled);
    }

    if (enabled) SendDlgItemMessageA(mainDlg, 0, WM_SETTEXT, 0, (LPARAM)(isServerRun || isDatabaseRun ? "Stop All" : "Start All"));
    SendDlgItemMessageA(mainDlg, 1, WM_SETTEXT, 0, (LPARAM)(isServerRun ? "Stop Server" : "Start Server"));
    SendDlgItemMessageA(mainDlg, 2, WM_SETTEXT, 0, (LPARAM)(isDatabaseRun ? "Stop MySQL" : "Start MySQL"));

    if (!enabled)
    {
        SendDlgItemMessageA(mainDlg, 0, WM_SETTEXT, 0, (LPARAM)label);
        SendDlgItemMessageA(mainDlg, item, WM_SETTEXT, 0, (LPARAM)label);
    }
}

bool RewriteAppConfig(std::string fromFile, std::string toFile)
{
    std::string strTemp;
    std::map<std::string, std::string> replaceText = GetReplaceText();

    fi.open(fromFile);
    if (!fi.is_open())
    {
        MessageBoxA(mainDlg, std::string("Can't find \"" + fromFile + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        fi.close();
        return false;
    }

    fo.open(toFile, std::ios::out | std::ios::trunc);
    if (!fo.is_open())
    {
        MessageBoxA(mainDlg, std::string("Error writing to \"" + toFile + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        fi.close();
        return false;
    }

    while (getline(fi, strTemp))
    {
        for (auto entry : replaceText)
        {
            auto text_1 = entry.first;
            auto text_2 = entry.second;
            while (strTemp.find(text_1) != std::string::npos)
            {
                strTemp.replace(strTemp.find(text_1), text_1.length(), text_2);
            }
        }
        fo << strTemp << "\n";
    }
    fo.close();
    fi.close();
    return true;
}

bool CheckIpAddress(std::string ipAddress)
{
    return (int)inet_addr(ipAddress.c_str()) != (int)-1;
}

bool CheckHostPort(std::string host, int port)
{
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        MessageBoxA(mainDlg, "Checking port failed.", "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    SOCKADDR_IN client;
    SOCKET sock;

    client.sin_family = AF_INET;
    client.sin_addr.s_addr = inet_addr(host.c_str());
    client.sin_port = htons(port);

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (bind(sock, (PSOCKADDR)&client, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
        MessageBoxA(mainDlg, std::string("Can't use host \"" + host + "\" port \"" + std::to_string(port) + "\"").c_str(), "Error", MB_OK | MB_ICONERROR);
        WSACleanup();
        return false;
    }

    WSACleanup();
    closesocket(sock);

    return true;
}

void KillProcessTree(DWORD procID)
{
    PROCESSENTRY32 pe;

    memset(&pe, 0, sizeof(PROCESSENTRY32));
    pe.dwSize = sizeof(PROCESSENTRY32);

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (Process32First(hSnap, &pe))
    {
        do
        {
            if (pe.th32ParentProcessID == procID)
            {
                KillProcessTree(pe.th32ProcessID);
            }
        }
        while (Process32Next(hSnap, &pe));
    }

    // kill the main process
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, procID);

    if (hProc)
    {
        TerminateProcess(hProc, 0);
        WaitForSingleObject(hProc, INFINITE);
        CloseHandle(hProc);
    }
}

void HandleClosed(HANDLE &hProc, bool &isRun)
{
    WaitForSingleObject(hProc, INFINITE);
    isRun = false;
    EnableDisableItem(true);
    ExitThread(0);
}

void StartServer()
{
    if (isServerRun) return;

    AppendLog("Starting Server ...");

    bool nextStep = true;
    std::string apachePath, apacheExe, phpPath, configFrom, configTo;

    if (config["php"].length() == 0)
    {
        MessageBoxA(mainDlg, "No PHP selected.", "Warning", MB_OK | MB_ICONEXCLAMATION);
        nextStep = false;
    }

    if (!nextStep) return;

    apachePath = appPath + "\\bin\\apache";
    apacheExe = apachePath + "\\bin\\httpd.exe";
    configFrom = apachePath + "\\conf\\httpd_wpamp.conf";
    configTo = apachePath + "\\conf\\httpd.conf";

    nextStep = RewriteAppConfig(configFrom, configTo);

    if (!nextStep) return;

    fi.open(apacheExe);
    if (!fi.is_open())
    {
        MessageBoxA(mainDlg, std::string("Can't find \"" + apacheExe + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        nextStep = false;
    }
    fi.close();

    if (!nextStep) return;

    phpPath = appPath + "\\bin\\php\\" + config["php"];
    configFrom = phpPath + "\\php_wpamp.ini";
    configTo = phpPath + "\\php.ini";

    nextStep = RewriteAppConfig(configFrom, configTo);

    if (!nextStep) return;

    std::map<std::string, std::string> phpModule = GetApachePhpModule();
    if (phpModule["file"] == std::string("") || phpModule["file"] == std::string(""))
    {
        MessageBoxA(mainDlg, "Can't find PHP Apache module.", "Error", MB_OK | MB_ICONERROR);
        nextStep = false;
    }

    if (!nextStep) return;

    nextStep = CheckHostPort(config["apache_host"], stoi(config["apache_port"]));

    if (!nextStep) return;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hRead = NULL, hWrite = NULL;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        MessageBoxA(mainDlg, "Failed to create pipe.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    STARTUPINFOA si = { 0 };

    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    std::string cmdLine = "\"" + apacheExe + "\"";

    BOOL success = CreateProcessA(
        NULL,
        (LPSTR)cmdLine.c_str(),
        NULL, NULL,
        TRUE,
        0,
        NULL,
        apachePath.c_str(),
        &si,
        &execApache
    );

    CloseHandle(hWrite);

    DWORD waitResult = WaitForSingleObject(execApache.hProcess, 1000);
    DWORD exitCode = 0;
    GetExitCodeProcess(execApache.hProcess, &exitCode);

    if (waitResult == WAIT_OBJECT_0 && exitCode != 0) {
        char buffer[512];
        DWORD bytesRead;
        std::string output;

        while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            output += buffer;
        }

        MessageBoxA(mainDlg, "Error starting Apache.", "Error", MB_OK | MB_ICONERROR);

        if (!output.empty()) {
            AppendLog(output);
        }

        CloseHandle(hRead);
        if (execApache.hProcess) CloseHandle(execApache.hProcess);
        if (execApache.hThread) CloseHandle(execApache.hThread);
        execApache = {};
    } else {
        CloseHandle(hRead);
        isServerRun = true;
        std::thread(HandleClosed, std::ref(execApache.hProcess), std::ref(isServerRun)).detach();
        AppendLog("Server started");
    }
}

void StopServer()
{
    if (!isServerRun) return;

    AppendLog("Stopping Server ...");

    if (WaitForSingleObject(execApache.hProcess, 0) == WAIT_TIMEOUT)
    {
        DWORD pid = GetProcessId(execApache.hProcess);
        KillProcessTree(pid);
        if (execApache.hProcess) CloseHandle(execApache.hProcess);
        if (execApache.hThread) CloseHandle(execApache.hThread);
        execApache = {};
    }

    AppendLog("Server stopped");

    isServerRun = false;
}

void StartDatabase()
{
    if (isDatabaseRun) return;

    AppendLog("Starting MySQL ...");

    bool nextStep = true;
    std::string mysqlPath, mysqlExe, configFrom, configTo;

    if (config["mysql"].length() == 0)
    {
        MessageBoxA(mainDlg, "No MySQL selected.", "Warning", MB_OK | MB_ICONEXCLAMATION);
        nextStep = false;
    }

    if (!nextStep) return;

    mysqlPath = appPath + "\\bin\\mysql\\" + config["mysql"];
    mysqlExe = mysqlPath + "\\bin\\mysqld.exe";
    configFrom = mysqlPath + "\\my_wpamp.ini";
    configTo = mysqlPath + "\\my.ini";

    nextStep = RewriteAppConfig(configFrom, configTo);

    if (!nextStep) return;

    fi.open(mysqlExe);
    if (!fi.is_open())
    {
        MessageBoxA(mainDlg, std::string("Can't find \"" + mysqlExe + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        nextStep = false;
    }
    fi.close();

    if (!nextStep) return;

    nextStep = CheckHostPort(config["mysql_host"], stoi(config["mysql_port"]));

    if (!nextStep) return;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hRead = NULL, hWrite = NULL;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        MessageBoxA(mainDlg, "Failed to create pipe.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    STARTUPINFOA si = { 0 };

    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    std::string cmdLine = "\"" + mysqlExe + "\" --console";

    BOOL success = CreateProcessA(
        NULL,
        (LPSTR)cmdLine.c_str(),
        NULL, NULL,
        TRUE,
        0,
        NULL,
        mysqlPath.c_str(),
        &si,
        &execMySQL
    );

    CloseHandle(hWrite);

    DWORD waitResult = WaitForSingleObject(execMySQL.hProcess, 1000);
    DWORD exitCode = 0;
    GetExitCodeProcess(execMySQL.hProcess, &exitCode);

    if (waitResult == WAIT_OBJECT_0 && exitCode != 0) {
        char buffer[512];
        DWORD bytesRead;
        std::string output;

        while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            output += buffer;
        }

        MessageBoxA(mainDlg, "Error starting MySQL.", "Error", MB_OK | MB_ICONERROR);

        if (!output.empty()) {
            AppendLog(output);
        }

        CloseHandle(hRead);
        if (execMySQL.hProcess) CloseHandle(execMySQL.hProcess);
        if (execMySQL.hThread) CloseHandle(execMySQL.hThread);
        execMySQL = {};
    } else {
        CloseHandle(hRead);
        isDatabaseRun = true;
        std::thread(HandleClosed, std::ref(execMySQL.hProcess), std::ref(isDatabaseRun)).detach();
        AppendLog("MySQL started");
    }
}

void StopDatabase()
{
    if (!isDatabaseRun) return;

    AppendLog("Stopping MySQL ...");

    if (WaitForSingleObject(execMySQL.hProcess, 0) == WAIT_TIMEOUT)
    {
        DWORD pid = GetProcessId(execMySQL.hProcess);
        KillProcessTree(pid);
        if (execMySQL.hProcess) CloseHandle(execMySQL.hProcess);
        if (execMySQL.hThread) CloseHandle(execMySQL.hThread);
        execMySQL = {};
    }

    AppendLog("MySQL stopped");

    isDatabaseRun = false;
}

void StartAll(bool server, bool database)
{
    ReadWriteSettings(true);
    if (server)
    {
        EnableDisableItem(false, "Starting...", 1);
        StartServer();
    }
    if (database)
    {
        EnableDisableItem(false, "Starting...", 2);
        StartDatabase();
    }
    EnableDisableItem(true);
    ExitThread(0);
}

void StopAll(bool server, bool database)
{
    if (server)
    {
        EnableDisableItem(false, "Stopping...", 1);
        StopServer();
    }
    if (database)
    {
        EnableDisableItem(false, "Stopping...", 2);
        StopDatabase();
    }
    EnableDisableItem(true);
    ExitThread(0);
}

bool CALLBACK DlgConf(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            RECT parentRect, childRect;
            GetWindowRect(mainDlg, &parentRect);
            GetWindowRect(hwndDlg, &childRect);

            int parentWidth = parentRect.right - parentRect.left;
            int parentHeight = parentRect.bottom - parentRect.top;
            int childWidth = childRect.right - childRect.left;
            int childHeight = childRect.bottom - childRect.top;

            int posLeft = parentRect.left + ((parentWidth / 2) - (childWidth / 2));
            int posTop = parentRect.top + ((parentHeight / 2) - (childHeight / 2));

            MoveWindow(hwndDlg, posLeft, posTop, childWidth, childHeight, FALSE);

            switch (configType)
            {
                case 1:
                    SendMessageA(hwndDlg, WM_SETTEXT, 0, (LPARAM)"Apache Config");
                    // host port
                    SetDlgItemText(hwndDlg, 2, config["apache_host"].c_str());
                    SetDlgItemText(hwndDlg, 3, config["apache_port"].c_str());
                    break;
                case 2:
                    SendMessageA(hwndDlg, WM_SETTEXT, 0, (LPARAM)"MySQL Config");
                    // host port
                    SetDlgItemText(hwndDlg, 2, config["mysql_host"].c_str());
                    SetDlgItemText(hwndDlg, 3, config["mysql_port"].c_str());
                    break;
            }
        }
        return true;

        case WM_CLOSE:
        {
            configType = 0;
            EndDialog(hwndDlg, 0);
        }
        return true;

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case 0:
                {
                    GetDlgItemText(hwndDlg, 2, buffer, MAX_PATH);
                    std::string hostConfig = std::string(buffer);
                    if (hostConfig.length() == 0)
                    {
                        MessageBoxA(hwndDlg, "Host can't be empty!", "Warning", MB_OK | MB_ICONEXCLAMATION);
                        return false;
                    }
                    if (!CheckIpAddress(hostConfig))
                    {
                        MessageBoxA(hwndDlg, "Invalid Host IP address!", "Warning", MB_OK | MB_ICONEXCLAMATION);
                        return false;
                    }
                    GetDlgItemText(hwndDlg, 3, buffer, MAX_PATH);
                    std::string portConfig = std::string(buffer);
                    if (portConfig.length() == 0)
                    {
                        MessageBoxA(hwndDlg, "Port can't be empty!", "Warning", MB_OK | MB_ICONEXCLAMATION);
                        return false;
                    }
                    switch (configType)
                    {
                        case 1:
                        {
                            if (hostConfig == config["mysql_host"] && portConfig == config["mysql_port"])
                            {
                                MessageBoxA(hwndDlg, "Host and Port can't be the same as MySQL config.", "Warning", MB_OK | MB_ICONEXCLAMATION);
                                return false;
                            }
                            config["apache_host"] = hostConfig;
                            config["apache_port"] = portConfig;
                        }
                        break;

                        case 2:
                        {
                            if (hostConfig == config["apache_host"] && portConfig == config["apache_port"])
                            {
                                MessageBoxA(hwndDlg, "Host and Port can't be the same as Apache config.", "Warning", MB_OK | MB_ICONEXCLAMATION);
                                return false;
                            }
                            config["mysql_host"] = hostConfig;
                            config["mysql_port"] = portConfig;
                        }
                        break;
                    }
                    ReadWriteSettings(true);
                    PostMessageA(hwndDlg, WM_CLOSE, 0, 0);
                }
                break;

                case 1:
                {
                    PostMessageA(hwndDlg, WM_CLOSE, 0, 0);
                }
                break;
            }
        }
        return true;
    }
    return false;
}

bool CALLBACK DlgMain(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            // initialize dialog window
            mainDlg = hwndDlg;
            HICON hIcon = LoadIcon(hInst, MAKEINTRESOURCE(MAINICON));
            SendMessageA(hwndDlg, WM_SETICON, 0, (LPARAM)hIcon);
            SetFocus(hwndDlg);

            // initialize notify icon
            nid.cbSize = sizeof(NOTIFYICONDATA);
            nid.hWnd = hwndDlg;
            nid.uID = 1;
            nid.uVersion = NOTIFYICON_VERSION;
            nid.hIcon = hIcon;
            strcpy_s(nid.szTip, sizeof(nid.szTip), "wpAMP");
            nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
            nid.uCallbackMessage = APPWM_ICONNOTIFY;

            // create directory
            CreateDirectoryA(std::string(appPath + "\\bin").c_str(), NULL);
            CreateDirectoryA(std::string(appPath + "\\tmp").c_str(), NULL);
            CreateDirectoryA(std::string(appPath + "\\www").c_str(), NULL);

            // find app path
            DIR *dir;
            struct dirent *ent;
            std::string currdir;

            for (auto entry : binPath) {
                auto key = entry.first;
                auto value = entry.second;
                currdir = appPath + "\\bin\\" + value + "\\";
                if ((dir = opendir(currdir.c_str())) != NULL)
                {
                    // print all the directories within directory
                    while ((ent = readdir(dir)) != NULL)
                    {
                        fi.open(std::string(currdir + std::string(ent->d_name)).c_str());
                        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 && !fi.is_open())
                        {
                            SendDlgItemMessageA(hwndDlg, key, CB_ADDSTRING, 0, (LPARAM)ent->d_name);
                        }
                        fi.close();
                    }
                    closedir(dir);

                    if (config[value].length() == 0)
                    {
                        SendDlgItemMessageA(hwndDlg, key, CB_SETCURSEL, 0, 0);
                        SendDlgItemMessageA(hwndDlg, key, CB_GETLBTEXT, 0, (LPARAM)(LPCTSTR)buffer);
                        config[value] = std::string(buffer);
                    }
                    else
                    {
                        SendDlgItemMessageA(hwndDlg, key, CB_SELECTSTRING, 0, (LPARAM)config[value].c_str());
                        if (SendDlgItemMessageA(hwndDlg, key, CB_GETCURSEL, 0, 0) == CB_ERR)
                        {
                            SendDlgItemMessageA(hwndDlg, key, CB_SETCURSEL, 0, 0);
                            SendDlgItemMessageA(hwndDlg, key, CB_GETLBTEXT, 0, (LPARAM)(LPCTSTR)buffer);
                            config[value] = std::string(buffer);
                        }
                    }
                }
            }

            // write settings
            ReadWriteSettings(true);
        }
        return true;

        case WM_CLOSE:
        {
            if (!isServerRun && !isDatabaseRun) {
                ReleaseMutex(hMutexHandle);
                CloseHandle(hMutexHandle);
                EndDialog(hwndDlg, 0);
            } else {
                if (MessageBoxA(hwndDlg, "Are you sure want to exit?\nThis will stop all server and database.", "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    EnableDisableItem(false, "Stopping...", 1);
                    StopServer();
                    EnableDisableItem(false, "Stopping...", 2);
                    StopDatabase();
                    EnableDisableItem(true);
                    ReleaseMutex(hMutexHandle);
                    CloseHandle(hMutexHandle);
                    EndDialog(hwndDlg, 0);
                }
            }
        }
        return true;

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case 0:
                {
                    if (!isServerRun && !isDatabaseRun) // start
                    {
                        std::thread(StartAll, true, true).detach();
                    }
                    else // stop
                    {
                        std::thread(StopAll, true, true).detach();
                    }
                }
                break;

                case 1:
                {
                    if (!isServerRun) // start
                    {
                        std::thread(StartAll, true, false).detach();
                    }
                    else // stop
                    {
                        std::thread(StopAll, true, false).detach();
                    }
                }
                break;

                case 2:
                {
                    if (!isDatabaseRun) // start
                    {
                        std::thread(StartAll, false, true).detach();
                    }
                    else // stop
                    {
                        std::thread(StopAll, false, true).detach();
                    }
                }
                break;

                case 3:
                {
                    GetDlgItemText(hwndDlg, 3, buffer, MAX_PATH);
                    if (config["php"] != std::string(buffer))
                    {
                        config["php"] = std::string(buffer);
                    }
                }
                break;

                case 4:
                {
                    std::thread(OpenConfigFile, appPath + "\\bin\\php\\" + config["php"] + "\\php_wpamp.ini").detach();
                }
                break;

                case 5:
                {
                    configType = 1;
                    DialogBox(hInst, MAKEINTRESOURCE(DLG_CONF), hwndDlg, (DLGPROC)DlgConf);
                }
                break;

                case 6:
                {
                    std::thread(OpenConfigFile, appPath + "\\bin\\apache\\conf\\httpd_wpamp.conf").detach();
                }
                break;

                case 7:
                {
                    GetDlgItemText(hwndDlg, 7, buffer, MAX_PATH);
                    if (config["mysql"] != std::string(buffer))
                    {
                        config["mysql"] = std::string(buffer);
                    }
                }
                break;

                case 8:
                {
                    configType = 2;
                    DialogBox(hInst, MAKEINTRESOURCE(DLG_CONF), hwndDlg, (DLGPROC)DlgConf);
                }
                break;

                case 9:
                {
                    std::thread(OpenConfigFile, appPath + "\\bin\\mysql\\" + config["mysql"] + "\\my_wpamp.ini").detach();
                }
                break;
            }
        }
        return true;

        case WM_SIZE:
        {
            if (wParam == SIZE_MINIMIZED)
            {
                if (isServerRun || isDatabaseRun)
                {
                    ShowWindow(hwndDlg, SW_HIDE);
                    Shell_NotifyIcon(NIM_ADD, &nid);
                }
            }
        }
        return true;

        case APPWM_ICONNOTIFY:
        {
            switch (lParam)
            {
                case WM_LBUTTONDBLCLK:
                {
                    PostMessageA(hwndDlg, APPWM_ICONNOTIFY_RESTORE, 0, 0);
                }
                break;

                case WM_RBUTTONUP:
                {
                    POINT pt;
                    GetCursorPos(&pt);

                    HMENU popupMenu = CreatePopupMenu();
                    InsertMenuA(popupMenu, 0, MF_BYPOSITION | MF_STRING, 1, "Restore");
                    InsertMenuA(popupMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 2, NULL);
                    InsertMenuA(popupMenu, 2, MF_BYPOSITION | MF_STRING, 3, "Exit");

                    SetForegroundWindow(hwndDlg);

                    int popupAction = TrackPopupMenu(popupMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hwndDlg, NULL);

                    if (popupAction == 1)
                    {
                        PostMessageA(hwndDlg, APPWM_ICONNOTIFY_RESTORE, 0, 0);
                    }
                    else if (popupAction == 3)
                    {
                        PostMessageA(hwndDlg, WM_CLOSE, 0, 0);
                    }
                }
                break;
            }
            return true;
        }
        return true;

        case APPWM_ICONNOTIFY_RESTORE:
        {
            Shell_NotifyIcon(NIM_DELETE, &nid);
            ShowWindow(hwndDlg, SW_SHOW);
            SetForegroundWindow(hwndDlg);
            ShowWindow(hwndDlg, SW_RESTORE);
        }
        return true;
    }
    return false;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    hInst = hInstance;

    // check another instasnce
    hMutexHandle = CreateMutex(NULL, TRUE, "wp.amp.instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxA(NULL, "wpAMP is already running.", "Error", MB_OK | MB_ICONSTOP);
        return 0;
    }

    // set app path
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    appPath = std::string(buffer).substr(0, pos);

    // read wpAMP.ini settings
    ReadWriteSettings();

    return DialogBox(hInst, MAKEINTRESOURCE(DLG_MAIN), NULL, (DLGPROC)DlgMain);
}

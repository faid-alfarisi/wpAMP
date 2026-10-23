#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <dirent.h>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <richedit.h>
#include <tlhelp32.h>
#include <ctime>
#include "resource.h"

#define APPWM_ICONNOTIFY (WM_APP + 1)
#define APPWM_ICONNOTIFY_RESTORE (WM_APP + 2)

HINSTANCE hInst;
HWND mainDlg;
NOTIFYICONDATA nid;
HANDLE hMutexHandle;
PROCESS_INFORMATION execApache = {};
PROCESS_INFORMATION execMySQL = {};

const COLORREF COLOR_INFO  = RGB(0, 0, 0);     // Black
const COLORREF COLOR_OK    = RGB(0, 140, 0);   // Green
const COLORREF COLOR_ERROR = RGB(200, 0, 0);   // Red

// Control ID Constants
const int START_ALL_BTN     = ID_START_ALL_BTN;      // 0
const int START_SERVER_BTN  = ID_START_SERVER_BTN;   // 1
const int START_MYSQL_BTN   = ID_START_MYSQL_BTN;    // 2
const int LOG_BOX           = ID_LOG_BOX;            // 3
const int CLEAR_LOG_BTN     = ID_CLEAR_LOG_BTN;      // 4
const int BROWSE_WWW_BTN    = ID_BROWSE_WWW_BTN;     // 5
const int TERMINAL_BTN      = ID_TERMINAL_BTN;       // 6
const int PHP_DROPDOWN      = ID_PHP_DROPDOWN;       // 7
const int PHP_CONF_BTN      = ID_PHP_CONF_BTN;       // 8
const int APACHE_PORT_BTN   = ID_APACHE_PORT_BTN;    // 9
const int APACHE_CONF_BTN   = ID_APACHE_CONF_BTN;    // 10
const int MYSQL_DROPDOWN    = ID_MYSQL_DROPDOWN;     // 11
const int MYSQL_PORT_BTN    = ID_MYSQL_PORT_BTN;     // 12
const int MYSQL_CONF_BTN    = ID_MYSQL_CONF_BTN;     // 13

std::map<int, std::string> binPath = {
    {PHP_DROPDOWN, "php"},
    {MYSQL_DROPDOWN, "mysql"}
};
std::map<std::string, std::string> config = {
    {"php", ""},
    {"apache_port", "80"},
    {"mysql", ""},
    {"mysql_port", "3306"}
};
std::string appPath = "";
int configType = 0;
char buffer[MAX_PATH];
bool isServerRun = false;
bool isDatabaseRun = false;
std::atomic<bool> isBusy(false);
std::atomic<bool> isStoppingServer(false);
std::atomic<bool> isStoppingDatabase(false);

bool FileExists(const std::string& path)
{
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool DirectoryExists(const std::string& path)
{
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

std::string GetTimeNow()
{
    time_t now = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&now));
    return buf;
}

void AppendLog(const std::string& line, COLORREF color = COLOR_INFO, bool withTimestamp = true)
{
    HWND hLog = GetDlgItem(mainDlg, LOG_BOX);
    if (!hLog) return;

    int len = GetWindowTextLengthA(hLog);
    SendMessageA(hLog, EM_SETSEL, len, len);

    CHARFORMAT2A cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color;
    cf.dwEffects = 0;
    SendMessageA(hLog, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    std::string formatted = (withTimestamp ? ("[" + GetTimeNow() + "] ") : "") + line + "\r\n";
    SendMessageA(hLog, EM_REPLACESEL, FALSE, (LPARAM)formatted.c_str());

    int newLen = GetWindowTextLengthA(hLog);
    SendMessageA(hLog, EM_SETSEL, newLen, newLen);
    SendMessageA(hLog, EM_SCROLLCARET, 0, 0);
    SendMessageA(hLog, WM_VSCROLL, SB_BOTTOM, 0);
}

std::map<std::string, std::string> GetApachePhpModule()
{
    DIR *dir;
    struct dirent *ent;
    std::string phpPath = appPath + "\\bin\\php\\" + config["php"];
    std::map<std::string, std::string> ret = {
        {"name", ""},
        {"file", ""}
    };

    if (config["php"].empty()) return ret;

    if ((dir = opendir(phpPath.c_str())) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            std::string fileName = std::string(ent->d_name);
            std::string lowerFileName = fileName;

            for (auto &c : lowerFileName)
            {
                c = std::tolower(static_cast<unsigned char>(c));
            }

            if (
                lowerFileName.find("php") != std::string::npos &&
                lowerFileName.find("apache") != std::string::npos &&
                lowerFileName.find(".dll") != std::string::npos
            )
            {
                std::string fullModulePath = phpPath + "\\" + fileName;
                if (FileExists(fullModulePath))
                {
                    ret["file"] = fileName;

                    size_t firstPos = lowerFileName.find("php") + 3;
                    size_t lastPos = lowerFileName.find("apache");
                    bool isNumber = true;
                    std::string phpVer = "";

                    if (lastPos != std::string::npos && lastPos >= firstPos)
                    {
                        phpVer = lowerFileName.substr(firstPos, lastPos - firstPos);
                        if (phpVer.empty()) isNumber = false;
                        for (char const &ch : phpVer)
                        {
                            if (!std::isdigit(static_cast<unsigned char>(ch)))
                            {
                                isNumber = false;
                                break;
                            }
                        }
                    }
                    else
                    {
                        isNumber = false;
                    }

                    ret["name"] = "php" + phpVer + "_module";
                    if (isNumber && atoi(phpVer.c_str()) > 7) // php 8 and above
                    {
                        ret["name"] = "php_module";
                    }
                    break;
                }
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
        {"{APACHE_PORT}", config["apache_port"]},
        {"{PHP_PATH}", slashPath + "/bin/php/" + config["php"]},
        {"{PHP_MODULE_NAME}", phpModule["name"]},
        {"{PHP_MODULE_FILE}", phpModule["file"]},
        {"{MYSQL_PATH}", slashPath + "/bin/mysql/" + config["mysql"]},
        {"{MYSQL_PORT}", config["mysql_port"]},
        {"{TMP_PATH}", slashPath + "/tmp"},
        {"{WWW_PATH}", slashPath + "/www"}
    };
}

void ReadWriteSettings(bool rewrite = false)
{
    std::string configFile = appPath + "\\wpAMP.ini";

    if (rewrite || !FileExists(configFile))
    {
        std::ofstream fo(configFile, std::ios::out | std::ios::trunc);
        if (fo.is_open())
        {
            fo << "php=" << config["php"] << "\n";
            fo << "apache_port=" << config["apache_port"] << "\n";
            fo << "mysql=" << config["mysql"] << "\n";
            fo << "mysql_port=" << config["mysql_port"];
            fo.close();
        }
    }

    std::ifstream fi(configFile);
    if (fi.is_open())
    {
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
}

void OpenConfigFile(std::string filePath)
{
    if (!FileExists(filePath))
    {
        MessageBoxA(mainDlg, std::string("Can't find \"" + filePath + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        return;
    }
    ShellExecuteA(NULL, "open", "notepad.exe", filePath.c_str(), NULL, SW_NORMAL);
}

bool RewriteAppConfig(std::string fromFile, std::string toFile)
{
    if (!FileExists(fromFile))
    {
        MessageBoxA(mainDlg, std::string("Can't find \"" + fromFile + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    std::ifstream fi(fromFile);
    if (!fi.is_open())
    {
        MessageBoxA(mainDlg, std::string("Can't open \"" + fromFile + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    std::ofstream fo(toFile, std::ios::out | std::ios::trunc);
    if (!fo.is_open())
    {
        fi.close();
        MessageBoxA(mainDlg, std::string("Error writing to \"" + toFile + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        return false;
    }

    std::map<std::string, std::string> replaceText = GetReplaceText();
    std::string strTemp;
    std::string phpLibpq = appPath + "\\bin\\php\\" + config["php"] + "\\libpq.dll";
    bool libpqAvailable = FileExists(phpLibpq);

    while (getline(fi, strTemp))
    {
        // Safe check for libpq.dll directive in apache config
        if (!libpqAvailable && strTemp.find("libpq.dll") != std::string::npos && strTemp.find("LoadFile") != std::string::npos)
        {
            if (strTemp.empty() || strTemp[0] != '#')
            {
                strTemp = "#" + strTemp;
            }
        }

        for (const auto &entry : replaceText)
        {
            const std::string &search = entry.first;
            const std::string &replace = entry.second;
            size_t pos = 0;
            while ((pos = strTemp.find(search, pos)) != std::string::npos)
            {
                strTemp.replace(pos, search.length(), replace);
                pos += replace.length();
            }
        }
        fo << strTemp << "\n";
    }
    fo.close();
    fi.close();
    return true;
}

void EnableDisableItem(bool enabled, const char *label = "", int item = 0)
{
    if (!enabled)
    {
        const int actionControls[] = {
            START_ALL_BTN, START_SERVER_BTN, START_MYSQL_BTN,
            CLEAR_LOG_BTN, BROWSE_WWW_BTN, TERMINAL_BTN,
            PHP_DROPDOWN, PHP_CONF_BTN, APACHE_PORT_BTN, APACHE_CONF_BTN,
            MYSQL_DROPDOWN, MYSQL_PORT_BTN, MYSQL_CONF_BTN
        };
        for (int id : actionControls)
        {
            EnableWindow(GetDlgItem(mainDlg, id), FALSE);
        }
        SendDlgItemMessageA(mainDlg, START_ALL_BTN, WM_SETTEXT, 0, (LPARAM)label);
        SendDlgItemMessageA(mainDlg, item, WM_SETTEXT, 0, (LPARAM)label);
    }
    else
    {
        // Primary start/stop buttons
        EnableWindow(GetDlgItem(mainDlg, START_ALL_BTN), TRUE);
        EnableWindow(GetDlgItem(mainDlg, START_SERVER_BTN), TRUE);
        EnableWindow(GetDlgItem(mainDlg, START_MYSQL_BTN), TRUE);

        SendDlgItemMessageA(mainDlg, START_ALL_BTN, WM_SETTEXT, 0, (LPARAM)(isServerRun || isDatabaseRun ? "Stop All" : "Start All"));
        SendDlgItemMessageA(mainDlg, START_SERVER_BTN, WM_SETTEXT, 0, (LPARAM)(isServerRun ? "Stop Server" : "Start Server"));
        SendDlgItemMessageA(mainDlg, START_MYSQL_BTN, WM_SETTEXT, 0, (LPARAM)(isDatabaseRun ? "Stop MySQL" : "Start MySQL"));

        // Utility buttons
        EnableWindow(GetDlgItem(mainDlg, CLEAR_LOG_BTN), TRUE);
        EnableWindow(GetDlgItem(mainDlg, BROWSE_WWW_BTN), TRUE);
        EnableWindow(GetDlgItem(mainDlg, TERMINAL_BTN), TRUE);

        // Server / PHP controls: locked if server is running
        EnableWindow(GetDlgItem(mainDlg, PHP_DROPDOWN), !isServerRun);
        EnableWindow(GetDlgItem(mainDlg, PHP_CONF_BTN), TRUE);
        EnableWindow(GetDlgItem(mainDlg, APACHE_PORT_BTN), !isServerRun);
        EnableWindow(GetDlgItem(mainDlg, APACHE_CONF_BTN), TRUE);

        // MySQL controls: locked if MySQL is running
        EnableWindow(GetDlgItem(mainDlg, MYSQL_DROPDOWN), !isDatabaseRun);
        EnableWindow(GetDlgItem(mainDlg, MYSQL_PORT_BTN), !isDatabaseRun);
        EnableWindow(GetDlgItem(mainDlg, MYSQL_CONF_BTN), TRUE);
    }
}

void OpenTerminal()
{
    if (config["php"].empty() && config["mysql"].empty())
    {
        MessageBoxA(mainDlg, "No PHP or MySQL selected.", "Warning", MB_OK | MB_ICONEXCLAMATION);
        return;
    }

    std::string wwwPath = appPath + "\\www";
    if (!DirectoryExists(wwwPath))
    {
        CreateDirectoryA(wwwPath.c_str(), NULL);
    }

    // Refresh php.ini from php_wpamp.ini so changes made via [...] take effect immediately in CLI
    if (!config["php"].empty())
    {
        std::string phpPath = appPath + "\\bin\\php\\" + config["php"];
        RewriteAppConfig(phpPath + "\\php_wpamp.ini", phpPath + "\\php.ini");
    }

    // Refresh my.ini from my_wpamp.ini if MySQL is not currently running
    if (!config["mysql"].empty() && !isDatabaseRun)
    {
        std::string mysqlPath = appPath + "\\bin\\mysql\\" + config["mysql"];
        RewriteAppConfig(mysqlPath + "\\my_wpamp.ini", mysqlPath + "\\my.ini");
    }

    std::string phpBin = "";
    if (!config["php"].empty())
    {
        phpBin = appPath + "\\bin\\php\\" + config["php"] + ";";
    }

    std::string mysqlBin = "";
    if (!config["mysql"].empty())
    {
        mysqlBin = appPath + "\\bin\\mysql\\" + config["mysql"] + "\\bin;";
    }

    std::string apacheBin = appPath + "\\bin\\apache\\bin;";

    char envBuf[32767];
    DWORD len = GetEnvironmentVariableA("PATH", envBuf, sizeof(envBuf));
    std::string origPath = (len > 0) ? std::string(envBuf) : "";
    std::string newPath = phpBin + mysqlBin + apacheBin + origPath;

    SetEnvironmentVariableA("PATH", newPath.c_str());

    char origMysqlTcpPort[32];
    DWORD portLen = GetEnvironmentVariableA("MYSQL_TCP_PORT", origMysqlTcpPort, sizeof(origMysqlTcpPort));
    bool hadMysqlTcpPort = (portLen > 0);

    if (!config["mysql_port"].empty())
    {
        SetEnvironmentVariableA("MYSQL_TCP_PORT", config["mysql_port"].c_str());
    }

    std::string cmdTitle = "wpAMP CLI";
    if (!config["php"].empty() && !config["mysql"].empty())
    {
        cmdTitle += " (PHP: " + config["php"] + ", MySQL: " + config["mysql"] + ")";
    }
    else if (!config["php"].empty())
    {
        cmdTitle += " (PHP: " + config["php"] + ")";
    }
    else if (!config["mysql"].empty())
    {
        cmdTitle += " (MySQL: " + config["mysql"] + ")";
    }

    std::string cmdArgs = "/k \"title " + cmdTitle;
    cmdArgs += "&& echo [wpAMP] CLI Environment";
    if (!config["php"].empty())
    {
        cmdArgs += "&& echo PHP       : " + config["php"];
    }
    if (!config["mysql"].empty())
    {
        std::string mysqlStatus = isDatabaseRun ? ("[RUNNING on port " + config["mysql_port"] + "]") : "[STOPPED - start MySQL in wpAMP to connect]";
        cmdArgs += "&& echo MySQL     : " + config["mysql"] + " " + mysqlStatus;
    }
    cmdArgs += "&& echo Directory : " + wwwPath;

    if (!config["php"].empty() && FileExists(appPath + "\\bin\\php\\" + config["php"] + "\\php.exe"))
    {
        cmdArgs += "&& echo.&& php -v";
    }
    if (!config["mysql"].empty() && FileExists(appPath + "\\bin\\mysql\\" + config["mysql"] + "\\bin\\mysql.exe"))
    {
        cmdArgs += "&& echo.&& mysql --version";
    }
    cmdArgs += "&& echo.\"";

    ShellExecuteA(NULL, "open", "cmd.exe", cmdArgs.c_str(), wwwPath.c_str(), SW_NORMAL);

    SetEnvironmentVariableA("PATH", origPath.c_str());
    if (hadMysqlTcpPort)
    {
        SetEnvironmentVariableA("MYSQL_TCP_PORT", origMysqlTcpPort);
    }
    else
    {
        SetEnvironmentVariableA("MYSQL_TCP_PORT", NULL);
    }
}

std::vector<DWORD> GetProcessTreePids(DWORD rootPid)
{
    std::vector<DWORD> pids = { rootPid };
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32 pe;
        memset(&pe, 0, sizeof(pe));
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnap, &pe))
        {
            do
            {
                for (size_t i = 0; i < pids.size(); i++)
                {
                    if (pe.th32ParentProcessID == pids[i] && pe.th32ProcessID != pids[i])
                    {
                        bool exists = false;
                        for (DWORD existing : pids)
                        {
                            if (existing == pe.th32ProcessID) { exists = true; break; }
                        }
                        if (!exists) pids.push_back(pe.th32ProcessID);
                    }
                }
            }
            while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return pids;
}

bool IsProcessTreeListeningOnPort(DWORD rootPid, int targetPort)
{
    std::vector<DWORD> pids = GetProcessTreePids(rootPid);

    auto isOurPid = [&](DWORD pid) {
        for (DWORD targetPid : pids) {
            if (pid == targetPid) return true;
        }
        return false;
    };

    bool anyListening = false;

    // 1. Check IPv4 TCP listeners
    DWORD size = 0;
    if (GetExtendedTcpTable(NULL, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<char> buffer(size);
        if (GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR)
        {
            PMIB_TCPTABLE_OWNER_PID pTable = (PMIB_TCPTABLE_OWNER_PID)buffer.data();
            for (DWORD i = 0; i < pTable->dwNumEntries; i++)
            {
                if (isOurPid(pTable->table[i].dwOwningPid))
                {
                    int port = ntohs((u_short)pTable->table[i].dwLocalPort);
                    if (targetPort <= 0 || port == targetPort) return true;
                    anyListening = true;
                }
            }
        }
    }

    // 2. Check IPv6 TCP listeners
    size = 0;
    if (GetExtendedTcpTable(NULL, &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_LISTENER, 0) == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<char> buffer(size);
        if (GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR)
        {
            PMIB_TCP6TABLE_OWNER_PID pTable = (PMIB_TCP6TABLE_OWNER_PID)buffer.data();
            for (DWORD i = 0; i < pTable->dwNumEntries; i++)
            {
                if (isOurPid(pTable->table[i].dwOwningPid))
                {
                    int port = ntohs((u_short)pTable->table[i].dwLocalPort);
                    if (targetPort <= 0 || port == targetPort) return true;
                    anyListening = true;
                }
            }
        }
    }

    return anyListening;
}

bool WaitForPortReady(HANDLE hProc, int port, int timeoutMs = 4000)
{
    DWORD procId = GetProcessId(hProc);
    int elapsed = 0;
    const int stepMs = 100;

    while (elapsed < timeoutMs)
    {
        // If child process terminated or crashed, abort polling immediately!
        if (WaitForSingleObject(hProc, 0) == WAIT_OBJECT_0)
        {
            break;
        }

        if (IsProcessTreeListeningOnPort(procId, port))
        {
            return true;
        }

        Sleep(stepMs);
        elapsed += stepMs;
    }

    return false;
}

void KillProcessTree(DWORD procID)
{
    PROCESSENTRY32 pe;
    memset(&pe, 0, sizeof(PROCESSENTRY32));
    pe.dwSize = sizeof(PROCESSENTRY32);

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE)
    {
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
        CloseHandle(hSnap);
    }

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, procID);
    if (hProc)
    {
        TerminateProcess(hProc, 0);
        WaitForSingleObject(hProc, 3000);
        CloseHandle(hProc);
    }
}

void MonitorProcess(std::string serviceName, HANDLE hProc, HANDLE hRead, bool &isRunFlag, std::atomic<bool> &isStoppingFlag)
{
    std::string capturedOutput;
    char buf[1024];
    DWORD bytesRead;

    while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0)
    {
        buf[bytesRead] = '\0';
        capturedOutput += buf;
        if (capturedOutput.length() > 65536)
        {
            capturedOutput = capturedOutput.substr(capturedOutput.length() - 32768);
        }
    }
    CloseHandle(hRead);

    DWORD exitCode = 0;
    WaitForSingleObject(hProc, INFINITE);
    GetExitCodeProcess(hProc, &exitCode);
    CloseHandle(hProc);

    bool stoppedByUser = isStoppingFlag.exchange(false);
    isRunFlag = false;
    isBusy = false;
    EnableDisableItem(true);

    if (!stoppedByUser)
    {
        AppendLog("[ERROR] " + serviceName + " stopped unexpectedly (Exit code: " + std::to_string(exitCode) + ")", COLOR_ERROR);
        if (!capturedOutput.empty())
        {
            AppendLog(capturedOutput, COLOR_ERROR, false);
        }
        MessageBoxA(mainDlg, (serviceName + " stopped unexpectedly (Exit code: " + std::to_string(exitCode) + ").").c_str(), "Error", MB_OK | MB_ICONERROR);
    }
}

void StartServer()
{
    if (isServerRun) return;

    if (config["php"].empty())
    {
        MessageBoxA(mainDlg, "No PHP selected.", "Warning", MB_OK | MB_ICONEXCLAMATION);
        return;
    }

    AppendLog("Starting Server (" + config["php"] + ")", COLOR_INFO);

    std::string apachePath = appPath + "\\bin\\apache";
    std::string apacheExe = apachePath + "\\bin\\httpd.exe";
    std::string configFrom = apachePath + "\\conf\\httpd_wpamp.conf";
    std::string configTo = apachePath + "\\conf\\httpd.conf";

    if (!RewriteAppConfig(configFrom, configTo)) return;

    if (!FileExists(apacheExe))
    {
        MessageBoxA(mainDlg, std::string("Can't find \"" + apacheExe + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        return;
    }

    std::string phpPath = appPath + "\\bin\\php\\" + config["php"];
    configFrom = phpPath + "\\php_wpamp.ini";
    configTo = phpPath + "\\php.ini";

    if (!RewriteAppConfig(configFrom, configTo)) return;

    std::map<std::string, std::string> phpModule = GetApachePhpModule();
    if (phpModule["file"].empty() || phpModule["name"].empty())
    {
        MessageBoxA(mainDlg, "Can't find PHP Apache module in selected PHP folder.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    int portNum = atoi(config["apache_port"].c_str());
    if (portNum <= 0 || portNum > 65535) portNum = 80;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hRead = NULL, hWrite = NULL;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        MessageBoxA(mainDlg, "Failed to create pipe.", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    // Set PATH environment variable to include PHP directory so php8.dll / dependencies are found!
    char envBuf[32767];
    DWORD envLen = GetEnvironmentVariableA("PATH", envBuf, sizeof(envBuf));
    std::string origPath = (envLen > 0) ? std::string(envBuf) : "";
    std::string updatedPath = phpPath + ";" + apachePath + "\\bin;" + origPath;
    SetEnvironmentVariableA("PATH", updatedPath.c_str());

    std::string cmdLine = "\"" + apacheExe + "\" -d \"" + apachePath + "\"";

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

    SetEnvironmentVariableA("PATH", origPath.c_str());
    CloseHandle(hWrite);

    if (!success) {
        CloseHandle(hRead);
        AppendLog("[ERROR] Failed to execute Apache (Error: " + std::to_string(GetLastError()) + ")", COLOR_ERROR);
        MessageBoxA(mainDlg, "Failed to execute Apache.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    isStoppingServer = false;
    isServerRun = true;
    HANDLE hProc = execApache.hProcess;
    if (execApache.hThread) { CloseHandle(execApache.hThread); execApache.hThread = NULL; }

    std::thread(MonitorProcess, "Apache", hProc, hRead, std::ref(isServerRun), std::ref(isStoppingServer)).detach();

    if (WaitForPortReady(execApache.hProcess, portNum, 4000))
    {
        AppendLog("Server (" + config["php"] + ") started", COLOR_OK);
    }
}

void StopServer()
{
    if (!isServerRun) return;
    AppendLog("Stopping Server (" + config["php"] + ")", COLOR_INFO);
    isStoppingServer = true;

    if (execApache.hProcess && WaitForSingleObject(execApache.hProcess, 0) == WAIT_TIMEOUT)
    {
        std::string apachePath = appPath + "\\bin\\apache";
        std::string apacheExe = apachePath + "\\bin\\httpd.exe";

        if (FileExists(apacheExe))
        {
            std::string stopCmd = "\"" + apacheExe + "\" -k stop -d \"" + apachePath + "\"";
            STARTUPINFOA siStop = { sizeof(siStop) };
            siStop.dwFlags = STARTF_USESHOWWINDOW;
            siStop.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION piStop = {};

            if (CreateProcessA(NULL, (LPSTR)stopCmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, apachePath.c_str(), &siStop, &piStop))
            {
                WaitForSingleObject(piStop.hProcess, 4000);
                CloseHandle(piStop.hProcess);
                CloseHandle(piStop.hThread);
            }
        }

        if (WaitForSingleObject(execApache.hProcess, 3000) == WAIT_TIMEOUT)
        {
            DWORD pid = GetProcessId(execApache.hProcess);
            KillProcessTree(pid);
        }
    }

    isServerRun = false;
    execApache.hProcess = NULL;
    AppendLog("Server (" + config["php"] + ") stopped", COLOR_INFO);
}

void StartDatabase()
{
    if (isDatabaseRun) return;

    if (config["mysql"].empty())
    {
        MessageBoxA(mainDlg, "No MySQL selected.", "Warning", MB_OK | MB_ICONEXCLAMATION);
        return;
    }

    AppendLog("Starting MySQL (" + config["mysql"] + ")", COLOR_INFO);

    std::string mysqlPath = appPath + "\\bin\\mysql\\" + config["mysql"];
    std::string mysqlExe = mysqlPath + "\\bin\\mysqld.exe";
    std::string configFrom = mysqlPath + "\\my_wpamp.ini";
    std::string configTo = mysqlPath + "\\my.ini";

    if (!RewriteAppConfig(configFrom, configTo)) return;

    if (!FileExists(mysqlExe))
    {
        MessageBoxA(mainDlg, std::string("Can't find \"" + mysqlExe + "\".").c_str(), "Error", MB_OK | MB_ICONERROR);
        return;
    }

    int portNum = atoi(config["mysql_port"].c_str());
    if (portNum <= 0 || portNum > 65535) portNum = 3306;

    // Check if data directory exists; if not, attempt auto-initialization for MySQL 5.7+
    std::string dataDir = mysqlPath + "\\data";
    if (!DirectoryExists(dataDir))
    {
        std::string initCmd = "\"" + mysqlExe + "\" \"--defaults-file=" + configTo + "\" --initialize-insecure --console";
        STARTUPINFOA siInit = { sizeof(siInit) };
        siInit.dwFlags = STARTF_USESHOWWINDOW;
        siInit.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION piInit = {};
        if (CreateProcessA(NULL, (LPSTR)initCmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, mysqlPath.c_str(), &siInit, &piInit))
        {
            WaitForSingleObject(piInit.hProcess, 30000);
            CloseHandle(piInit.hProcess);
            CloseHandle(piInit.hThread);
        }
    }

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hRead = NULL, hWrite = NULL;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        MessageBoxA(mainDlg, "Failed to create pipe.", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { 0 };
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    std::string cmdLine = "\"" + mysqlExe + "\" \"--defaults-file=" + configTo + "\" --console";

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

    if (!success) {
        CloseHandle(hRead);
        AppendLog("[ERROR] Failed to execute MySQL (Error: " + std::to_string(GetLastError()) + ")", COLOR_ERROR);
        MessageBoxA(mainDlg, "Failed to execute MySQL.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    isStoppingDatabase = false;
    isDatabaseRun = true;
    HANDLE hProc = execMySQL.hProcess;
    if (execMySQL.hThread) { CloseHandle(execMySQL.hThread); execMySQL.hThread = NULL; }

    std::thread(MonitorProcess, "MySQL", hProc, hRead, std::ref(isDatabaseRun), std::ref(isStoppingDatabase)).detach();

    if (WaitForPortReady(execMySQL.hProcess, portNum, 5000))
    {
        AppendLog("MySQL (" + config["mysql"] + ") started", COLOR_OK);
    }
}

void StopDatabase()
{
    if (!isDatabaseRun) return;
    AppendLog("Stopping MySQL (" + config["mysql"] + ")", COLOR_INFO);
    isStoppingDatabase = true;

    if (execMySQL.hProcess && WaitForSingleObject(execMySQL.hProcess, 0) == WAIT_TIMEOUT)
    {
        std::string mysqlAdminExe = appPath + "\\bin\\mysql\\" + config["mysql"] + "\\bin\\mysqladmin.exe";

        if (FileExists(mysqlAdminExe))
        {
            std::string adminCmd = "\"" + mysqlAdminExe + "\" -u root -h 127.0.0.1 -P " + config["mysql_port"] + " shutdown";
            STARTUPINFOA siAdmin = { sizeof(siAdmin) };
            siAdmin.dwFlags = STARTF_USESHOWWINDOW;
            siAdmin.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION piAdmin = {};

            if (CreateProcessA(NULL, (LPSTR)adminCmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &siAdmin, &piAdmin))
            {
                WaitForSingleObject(piAdmin.hProcess, 5000);
                CloseHandle(piAdmin.hProcess);
                CloseHandle(piAdmin.hThread);
            }
        }

        if (WaitForSingleObject(execMySQL.hProcess, 4000) == WAIT_TIMEOUT)
        {
            DWORD pid = GetProcessId(execMySQL.hProcess);
            KillProcessTree(pid);
        }
    }

    isDatabaseRun = false;
    execMySQL.hProcess = NULL;
    AppendLog("MySQL (" + config["mysql"] + ") stopped", COLOR_INFO);
}

void StartAll(bool server, bool database)
{
    ReadWriteSettings(true);
    if (server)
    {
        EnableDisableItem(false, "Starting...", START_SERVER_BTN);
        StartServer();
    }
    if (database)
    {
        EnableDisableItem(false, "Starting...", START_MYSQL_BTN);
        StartDatabase();
    }
    EnableDisableItem(true);
    isBusy = false;
}

void StopAll(bool server, bool database)
{
    if (server)
    {
        EnableDisableItem(false, "Stopping...", START_SERVER_BTN);
        StopServer();
    }
    if (database)
    {
        EnableDisableItem(false, "Stopping...", START_MYSQL_BTN);
        StopDatabase();
    }
    EnableDisableItem(true);
    isBusy = false;
}

INT_PTR CALLBACK DlgConf(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
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

            // Limit port text to max 5 digits (ports are 1..65535)
            SendMessageA(GetDlgItem(hwndDlg, 2), EM_LIMITTEXT, 5, 0);

            switch (configType)
            {
                case 1:
                    SendMessageA(hwndDlg, WM_SETTEXT, 0, (LPARAM)"Apache Port");
                    SetDlgItemText(hwndDlg, 2, config["apache_port"].c_str());
                    break;
                case 2:
                    SendMessageA(hwndDlg, WM_SETTEXT, 0, (LPARAM)"MySQL Port");
                    SetDlgItemText(hwndDlg, 2, config["mysql_port"].c_str());
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
                    std::string portConfig = std::string(buffer);
                    if (portConfig.length() == 0)
                    {
                        MessageBoxA(hwndDlg, "Port can't be empty!", "Warning", MB_OK | MB_ICONEXCLAMATION);
                        return false;
                    }
                    int p = atoi(portConfig.c_str());
                    if (p <= 0 || p > 65535)
                    {
                        MessageBoxA(hwndDlg, "Port must be between 1 and 65535!", "Warning", MB_OK | MB_ICONEXCLAMATION);
                        return false;
                    }
                    switch (configType)
                    {
                        case 1:
                        {
                            if (portConfig == config["mysql_port"])
                            {
                                MessageBoxA(hwndDlg, "Apache and MySQL cannot use the same port.", "Warning", MB_OK | MB_ICONEXCLAMATION);
                                return false;
                            }
                            config["apache_port"] = portConfig;
                        }
                        break;

                        case 2:
                        {
                            if (portConfig == config["apache_port"])
                            {
                                MessageBoxA(hwndDlg, "Apache and MySQL cannot use the same port.", "Warning", MB_OK | MB_ICONEXCLAMATION);
                                return false;
                            }
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

INT_PTR CALLBACK DlgMain(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        {
            if (GetDlgCtrlID((HWND)lParam) == LOG_BOX)
            {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, RGB(0, 0, 0));
                SetBkColor(hdc, RGB(255, 255, 255));
                return (INT_PTR)GetStockObject(WHITE_BRUSH);
            }
        }
        break;

        case WM_INITDIALOG:
        {
            mainDlg = hwndDlg;
            HICON hIcon = LoadIcon(hInst, MAKEINTRESOURCE(MAINICON));
            SendMessageA(hwndDlg, WM_SETICON, 0, (LPARAM)hIcon);
            SetFocus(hwndDlg);

            // Set white background for the log control
            SendMessageA(GetDlgItem(hwndDlg, LOG_BOX), EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(255, 255, 255));

            nid.cbSize = sizeof(NOTIFYICONDATA);
            nid.hWnd = hwndDlg;
            nid.uID = 1;
            nid.uVersion = NOTIFYICON_VERSION;
            nid.hIcon = hIcon;
            strcpy_s(nid.szTip, sizeof(nid.szTip), "wpAMP");
            nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
            nid.uCallbackMessage = APPWM_ICONNOTIFY;

            CreateDirectoryA(std::string(appPath + "\\bin").c_str(), NULL);
            CreateDirectoryA(std::string(appPath + "\\tmp").c_str(), NULL);
            CreateDirectoryA(std::string(appPath + "\\www").c_str(), NULL);

            DIR *dir;
            struct dirent *ent;
            std::string currdir;

            for (auto entry : binPath) {
                auto key = entry.first;
                auto value = entry.second;
                currdir = appPath + "\\bin\\" + value + "\\";
                if ((dir = opendir(currdir.c_str())) != NULL)
                {
                    while ((ent = readdir(dir)) != NULL)
                    {
                        std::string fullItemPath = currdir + ent->d_name;
                        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 && DirectoryExists(fullItemPath))
                        {
                            SendDlgItemMessageA(hwndDlg, key, CB_ADDSTRING, 0, (LPARAM)ent->d_name);
                        }
                    }
                    closedir(dir);

                    if (config[value].length() == 0)
                    {
                        SendDlgItemMessageA(hwndDlg, key, CB_SETCURSEL, 0, 0);
                        if (SendDlgItemMessageA(hwndDlg, key, CB_GETLBTEXT, 0, (LPARAM)(LPCTSTR)buffer) != CB_ERR)
                        {
                            config[value] = std::string(buffer);
                        }
                    }
                    else
                    {
                        SendDlgItemMessageA(hwndDlg, key, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)config[value].c_str());
                        if (SendDlgItemMessageA(hwndDlg, key, CB_GETCURSEL, 0, 0) == CB_ERR)
                        {
                            SendDlgItemMessageA(hwndDlg, key, CB_SETCURSEL, 0, 0);
                            if (SendDlgItemMessageA(hwndDlg, key, CB_GETLBTEXT, 0, (LPARAM)(LPCTSTR)buffer) != CB_ERR)
                            {
                                config[value] = std::string(buffer);
                            }
                        }
                    }
                }
            }

            ReadWriteSettings(true);
            EnableDisableItem(true);
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
                    EnableDisableItem(false, "Stopping...", START_SERVER_BTN);
                    StopServer();
                    EnableDisableItem(false, "Stopping...", START_MYSQL_BTN);
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
                case START_ALL_BTN:
                {
                    if (isBusy.exchange(true)) break;
                    if (!isServerRun && !isDatabaseRun)
                    {
                        std::thread(StartAll, true, true).detach();
                    }
                    else
                    {
                        std::thread(StopAll, true, true).detach();
                    }
                }
                break;

                case START_SERVER_BTN:
                {
                    if (isBusy.exchange(true)) break;
                    if (!isServerRun)
                    {
                        std::thread(StartAll, true, false).detach();
                    }
                    else
                    {
                        std::thread(StopAll, true, false).detach();
                    }
                }
                break;

                case START_MYSQL_BTN:
                {
                    if (isBusy.exchange(true)) break;
                    if (!isDatabaseRun)
                    {
                        std::thread(StartAll, false, true).detach();
                    }
                    else
                    {
                        std::thread(StopAll, false, true).detach();
                    }
                }
                break;

                case CLEAR_LOG_BTN:
                {
                    SetWindowTextA(GetDlgItem(hwndDlg, LOG_BOX), "");
                }
                break;

                case BROWSE_WWW_BTN:
                {
                    std::string wwwPath = appPath + "\\www";
                    if (!DirectoryExists(wwwPath))
                    {
                        CreateDirectoryA(wwwPath.c_str(), NULL);
                    }
                    ShellExecuteA(NULL, "open", "explorer.exe", wwwPath.c_str(), NULL, SW_NORMAL);
                }
                break;

                case TERMINAL_BTN:
                {
                    OpenTerminal();
                }
                break;

                case PHP_DROPDOWN:
                {
                    GetDlgItemText(hwndDlg, PHP_DROPDOWN, buffer, MAX_PATH);
                    if (config["php"] != std::string(buffer))
                    {
                        config["php"] = std::string(buffer);
                    }
                }
                break;

                case PHP_CONF_BTN:
                {
                    std::thread(OpenConfigFile, appPath + "\\bin\\php\\" + config["php"] + "\\php_wpamp.ini").detach();
                }
                break;

                case APACHE_PORT_BTN:
                {
                    configType = 1;
                    DialogBox(hInst, MAKEINTRESOURCE(DLG_CONF), hwndDlg, (DLGPROC)DlgConf);
                }
                break;

                case APACHE_CONF_BTN:
                {
                    std::thread(OpenConfigFile, appPath + "\\bin\\apache\\conf\\httpd_wpamp.conf").detach();
                }
                break;

                case MYSQL_DROPDOWN:
                {
                    GetDlgItemText(hwndDlg, MYSQL_DROPDOWN, buffer, MAX_PATH);
                    if (config["mysql"] != std::string(buffer))
                    {
                        config["mysql"] = std::string(buffer);
                    }
                }
                break;

                case MYSQL_PORT_BTN:
                {
                    configType = 2;
                    DialogBox(hInst, MAKEINTRESOURCE(DLG_CONF), hwndDlg, (DLGPROC)DlgConf);
                }
                break;

                case MYSQL_CONF_BTN:
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
    // Enable Per-Monitor v2 DPI awareness if supported (Windows 10 1703+)
    typedef BOOL(WINAPI *SetProcessDpiAwarenessContextProc)(HANDLE);
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32)
    {
        SetProcessDpiAwarenessContextProc setDpi = (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (setDpi)
        {
            setDpi((HANDLE)-4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        }
    }

    hInst = hInstance;

    // Load RichEdit library so CONTROL RichEdit20A can be instantiated
    LoadLibraryA("riched20.dll");

    hMutexHandle = CreateMutex(NULL, TRUE, "wp.amp.instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxA(NULL, "wpAMP is already running.", "Error", MB_OK | MB_ICONSTOP);
        return 0;
    }

    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    appPath = std::string(buffer).substr(0, pos);

    ReadWriteSettings();

    return DialogBox(hInst, MAKEINTRESOURCE(DLG_MAIN), NULL, (DLGPROC)DlgMain);
}

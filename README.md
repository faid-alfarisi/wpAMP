# wpAMP

Lightweight Windows Webserver (Apache, PHP, MySQL)

Download Apache, PHP, and MySQL in ZIP format and place them in the `bin` folder.

## How To

1. All applications must be placed in the `bin` folder:
   - Apache in `bin\apache`
   - PHP versions in `bin\php\<version>` (e.g., `bin\php\php-8.1.20`)
   - MySQL versions in `bin\mysql\<version>` (e.g., `bin\mysql\mysql-8.0.30`)
2. All application template configurations must end with `_wpamp` (e.g., `httpd_wpamp.conf`, `php_wpamp.ini`, `my_wpamp.ini`).
3. wpAMP automatically generates the active configuration files (`httpd.conf`, `php.ini`, `my.ini`) needed by the applications upon startup.
4. Variables available for auto-replacement in `*_wpamp.*` configuration templates:
   - `{APACHE_PATH}` = Apache root path (uses forward slashes)
   - `{APACHE_PORT}` = Apache port (configured in wpAMP GUI)
   - `{PHP_PATH}` = Selected PHP path (uses forward slashes)
   - `{PHP_MODULE_NAME}` = PHP Apache module name (e.g., `php_module` or `php7_module`)
   - `{PHP_MODULE_FILE}` = PHP Apache module DLL filename (e.g., `php8apache2_4.dll`)
   - `{MYSQL_PATH}` = Selected MySQL path (uses forward slashes)
   - `{MYSQL_PORT}` = MySQL port (configured in wpAMP GUI)
   - `{TMP_PATH}` = Temporary files directory path
   - `{WWW_PATH}` = Web root directory path (place your PHP project files here)

---

## Changelog

### Version 1.3

- **Developer Utility Controls:**
  - Added `Clear Log` button to reset and clear the log display instantly.
  - Added `Browse www` button to open the web root project directory directly in Windows Explorer.
  - Added `Terminal` button to spawn a dedicated Command Prompt window inside `www` with the active PHP, MySQL, and Apache binary directories automatically injected into the session `PATH`.

- **In-GUI Word-Wrapped Logging (Zero Disk Clutter):**
  - All status events, errors, and process diagnostic outputs are streamed directly into the interactive GUI log control in real-time.
  - Enabled automatic word-wrapping with vertical-only scrolling (`WS_VSCROLL`), eliminating awkward horizontal scrolling.
  - Zero disk clutter: No temporary `.txt` or log files are created on disk.
  - Clean timestamp format `[HH:MM:SS]` for every lifecycle event and `[ERROR]` tags for diagnostics.

- **Active Process & Crash Monitoring:**
  - Replaced the static 1-second startup guessing with a real-time monitor thread (`MonitorProcess`) running across the full service lifecycle.
  - Continuously reads child process pipes and detects unexpected terminations, capturing process exit codes and error output immediately upon crash.
  - Distinguishes intentional user stops from unexpected crashes, alerting the user with a dialog and displaying full diagnostic output in the log box.

- **Database Reliability & Graceful Shutdown:**
  - Implemented safe MySQL shutdown via `mysqladmin.exe shutdown` to flush InnoDB buffers to disk and prevent database corruption, retaining forced termination only as a fallback.
  - Added `httpd.exe -k stop` handling to gracefully stop Apache worker threads before process tree cleanup.
  - Added `--defaults-file` flag when executing `mysqld.exe` to guarantee configuration isolation from other MySQL installations on the host.
  - Added automatic initialization for missing MySQL data directories (`mysqld --initialize-insecure`).

- **Dynamic Environment & PHP Integration:**
  - Automatically prepends the active PHP directory to the process `PATH` environment variable before spawning Apache, resolving missing DLL errors (`php8.dll`, `php7ts.dll`, and extension libraries).
  - Kept pipe read handles open throughout process runtime, eliminating broken pipe crashes when Apache or PHP writes to standard output/error.
  - Added offset-based template replacement to prevent infinite loops and added safety checks for optional modules (e.g. auto-commenting missing `LoadFile ... libpq.dll`).

- **Networking, File System & Concurrency Hardening:**
  - Streamlined configuration to port-only management with widened input fields, numeric input styling, and 5-digit length enforcement (`EM_LIMITTEXT`).
  - Added port collision detection preventing Apache and MySQL from sharing the same port.
  - Implemented IP-agnostic process listener verification (`GetExtendedTcpTable` via `iphlpapi.dll`), accurately detecting when the process tree enters the `LISTEN` state regardless of bound IP, custom config edits, or dual-stack configuration.
  - Eliminated redundant pre-flight port probing to avoid socket lingering (`TIME_WAIT`), allowing Apache and MySQL to report native bind errors directly to the in-GUI log box.
  - Fixed Winsock cleanup sequence (`closesocket` before `WSACleanup`) and eliminated socket leaks.
  - Added port range validation (1–65535).
  - Replaced stream-based directory detection with native Win32 `GetFileAttributes` checking (`FILE_ATTRIBUTE_DIRECTORY`).
  - Removed low-level `ExitThread(0)` calls inside `std::thread`, replaced global file streams with localized instances, and added atomic button debouncing (`isBusy`).

### Version 1.2

- **Integrated GUI Log Window:**
  - Expanded main dialog window height from 60 to 160 (`180, 160`) to accommodate an integrated scrollable multi-line log control (`EDITTEXT 10`).
  - Added `AppendLog()` to output real-time service activity and status messages directly onto the GUI interface.
  - Removed separate log files (`apache_error.txt`, `mysql_error.txt`).

### Version 1.1

- **Process Execution Overhaul:**
  - Replaced `ShellExecuteExA` with `CreateProcessA` and anonymous pipe redirection (`CreatePipe`).
  - Added startup exit code checks (`GetExitCodeProcess`) and error logging to `apache_error.txt` and `mysql_error.txt`.
  - Bundled Apache 2.4 binaries, sample directory layouts (`tmp`, `www`), and configuration templates (`*_wpamp.ini`).
  - Cleaned up unneeded headers and updated window caption to "wpAMP 1.1".

### Version 1.0

- **Initial Release:**
  - Lightweight Windows GUI control panel for Apache, PHP, and MySQL.
  - Dynamic discovery and switching of PHP and MySQL versions placed in the `bin` directory.
  - Template configuration engine using `*_wpamp` files with automatic placeholder substitution (`{APACHE_PATH}`, `{PHP_PATH}`, `{MYSQL_PATH}`, etc.).
  - Port checking and IP validation for Apache and MySQL.
  - Start/Stop controls ("Start All", "Start Server", "Start MySQL").
  - System tray minimization and context menu.
  - Single application instance enforcement using Windows Mutex.

---

## Download Usable Build

- [Version 1.1](https://1drv.ms/u/c/06c4ce7d42e23ed5/EY_Nq1maQYxEvPcRQj1nFLMBVdJ5eo6ORbAWU5E7GirD1Q?e=I1KDWf)

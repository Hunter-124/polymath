#include "single_instance.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <chrono>
#include <thread>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <tlhelp32.h>
#endif

namespace polymath {
namespace {

#ifdef Q_OS_WIN

// Held for the process lifetime so the named mutex stays owned until exit.
// Intentionally never closed — the OS releases it when the process dies.
HANDLE g_instanceMutex = nullptr;
bool   g_claimed       = false;

// Basename of this process image, e.g. L"Polymath.exe".
std::wstring selfImageName() {
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return L"Polymath.exe";
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/')
            base = p + 1;
    }
    return base;
}

// Terminate every other process whose image basename matches ours.
// Returns how many TerminateProcess calls succeeded.
int killOtherInstances() {
    const DWORD selfPid = GetCurrentProcessId();
    const std::wstring selfName = selfImageName();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    int killed = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == selfPid)
                continue;
            if (_wcsicmp(pe.szExeFile, selfName.c_str()) != 0)
                continue;

            HANDLE proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE,
                                      FALSE, pe.th32ProcessID);
            if (!proc)
                continue;

            if (TerminateProcess(proc, 1)) {
                // Give the process a moment to actually die (DB handles, etc.).
                WaitForSingleObject(proc, 8000);
                ++killed;
            }
            CloseHandle(proc);
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return killed;
}

// Wait until no other same-name processes remain (best-effort).
bool waitUntilOthersGone(int timeoutMs) {
    const DWORD selfPid = GetCurrentProcessId();
    const std::wstring selfName = selfImageName();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return true;

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        bool found = false;
        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ProcessID == selfPid)
                    continue;
                if (_wcsicmp(pe.szExeFile, selfName.c_str()) == 0) {
                    found = true;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);

        if (!found)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

SingleInstanceResult claimWindows() {
    SingleInstanceResult r;

    // Named mutex shared by all Polymath builds on this user session.
    // Local\ = per-session (correct for interactive desktop apps).
    constexpr wchar_t kMutexName[] = L"Local\\Polymath.SingleInstance.v1";

    g_instanceMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!g_instanceMutex) {
        r.detail = "CreateMutex failed";
        return r;
    }

    // Always kill prior Polymath processes first. Covers:
    //  - live prior instance holding the mutex
    //  - hung / "zombie" instances
    //  - older builds that never took this mutex
    r.killed = killOtherInstances();
    if (r.killed > 0) {
        waitUntilOthersGone(10000);
        // Brief settle so SQLite WAL / port binds can release.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Take ownership. If a dying peer still held the mutex, WAIT_ABANDONED
    // means we still get exclusive ownership.
    DWORD wait = WaitForSingleObject(g_instanceMutex, 15000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        // Last resort: kill again and retry once.
        r.killed += killOtherInstances();
        waitUntilOthersGone(8000);
        wait = WaitForSingleObject(g_instanceMutex, 5000);
    }

    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        r.detail = "timed out waiting for single-instance mutex after killing peers";
        return r;
    }

    // Sweep once more in case a peer spawned between kill and acquire.
    const int extra = killOtherInstances();
    r.killed += extra;
    if (extra > 0)
        waitUntilOthersGone(5000);

    g_claimed = true;
    r.ok = true;
    if (r.killed > 0)
        r.detail = "claimed single-instance lock after terminating "
                   + std::to_string(r.killed) + " prior process(es)";
    else
        r.detail = "claimed single-instance lock (no prior processes)";
    return r;
}

#else // !Q_OS_WIN

// Non-Windows: QLockFile-based exclusive lock (no process kill).
// Full replace-on-launch kill is Windows-only for now (primary target).
#  include <QLockFile>
#  include <QDir>
#  include <QStandardPaths>
#  include <memory>

std::unique_ptr<QLockFile> g_lock;
bool g_claimed = false;

SingleInstanceResult claimPosix() {
    SingleInstanceResult r;
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/polymath-single-instance.lock");

    g_lock = std::make_unique<QLockFile>(path);
    g_lock->setStaleLockTime(5000); // ms — treat abandoned locks as free quickly

    if (g_lock->tryLock(100)) {
        g_claimed = true;
        r.ok = true;
        r.detail = "claimed single-instance lock file";
        return r;
    }

    // Stale lock from a dead process — remove and retry.
    if (g_lock->removeStaleLockFile() && g_lock->tryLock(100)) {
        g_claimed = true;
        r.ok = true;
        r.detail = "claimed single-instance lock after removing stale lock file";
        return r;
    }

    r.detail = "another Polymath instance holds the lock file "
               "(non-Windows build does not force-kill peers)";
    return r;
}

#endif

} // namespace

SingleInstanceResult claimSingleInstance(bool allowMultiple) {
#ifdef Q_OS_WIN
    if (g_claimed) {
        SingleInstanceResult r;
        r.ok = true;
        r.detail = "already claimed";
        return r;
    }
#else
    if (g_claimed) {
        SingleInstanceResult r;
        r.ok = true;
        r.detail = "already claimed";
        return r;
    }
#endif

    // CLI override: --allow-multiple (also accepts allowMultiple arg).
    if (!allowMultiple && QCoreApplication::instance()) {
        const QStringList args = QCoreApplication::arguments();
        for (const QString& a : args) {
            if (a == QLatin1String("--allow-multiple")) {
                allowMultiple = true;
                break;
            }
        }
    }

    if (allowMultiple) {
        SingleInstanceResult r;
        r.ok = true;
        r.detail = "single-instance guard skipped (--allow-multiple)";
        return r;
    }

#ifdef Q_OS_WIN
    return claimWindows();
#else
    return claimPosix();
#endif
}

} // namespace polymath

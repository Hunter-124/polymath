#pragma once
//
// Single-instance guard for Polymath.exe.
//
// A new launch always wins: any other live Polymath process is terminated, then
// this process takes exclusive ownership of a named OS mutex for its lifetime.
// That covers:
//   - accidental double-clicks / second shortcuts
//   - "stale" instances still alive after a crash, hang, or partial quit
//   - old builds that never held the mutex (still killed by image name)
//
// Pass allowMultiple=true (or --allow-multiple on the command line) to skip
// the guard for debugging / multi-profile experiments.
//
#include <string>

namespace polymath {

struct SingleInstanceResult {
    bool ok = false;          // false → caller should exit
    int  killed = 0;          // other Polymath processes terminated
    std::string detail;       // human-readable status / failure reason
};

// Must be called after QCoreApplication/QGuiApplication exists (needs argv
// parsing for --allow-multiple). Safe to call once; subsequent calls are no-ops.
SingleInstanceResult claimSingleInstance(bool allowMultiple = false);

} // namespace polymath

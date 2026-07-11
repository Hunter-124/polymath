// Overhaul2 C2 — system tools unit tests (temp-dir sandbox).
//
// Pure ITool invoke() coverage: fs_list/read/write (create+append round-trip),
// fs_delete → trash (gone from original path), clipboard write+read when a
// QGuiApplication is available. No network, no model, no SafetyPolicy re-test
// (A4 owns path/cmd gates; AgentLoop wires them).

#include "tool_registry.h"
#include "tools/system_tools.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace polymath;

namespace {

std::string join(const std::filesystem::path& root, const std::string& rel) {
    return (root / rel).string();
}

} // namespace

int main(int argc, char** argv) {
    // Gui app for clipboard; offscreen platform via ENVIRONMENT in CMake.
    QGuiApplication app(argc, argv);

    const auto root = std::filesystem::temp_directory_path() / "pm_system_tools_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "sub");

    ToolContext ctx;  // system tools ignore ctx services
    std::puts("test_system_tools: C2 fs / clipboard");

    // --- registry names + risk classes --------------------------------------
    {
        ToolRegistry reg;
        registerBuiltinTools(reg);
        for (const char* n : {"fs_list", "fs_read", "fs_write", "fs_move", "fs_delete",
                              "run_command", "app_launch", "clipboard_read",
                              "clipboard_write"}) {
            assert(reg.get(n) != nullptr && "missing C2 tool");
        }
        assert(reg.riskOf("fs_list") == ToolRiskClass::Read);
        assert(reg.riskOf("fs_read") == ToolRiskClass::Read);
        assert(reg.riskOf("clipboard_read") == ToolRiskClass::Read);
        assert(reg.riskOf("fs_write") == ToolRiskClass::WriteLocal);
        assert(reg.riskOf("clipboard_write") == ToolRiskClass::WriteLocal);
        assert(reg.riskOf("fs_move") == ToolRiskClass::Destructive);
        assert(reg.riskOf("fs_delete") == ToolRiskClass::Destructive);
        assert(reg.riskOf("run_command") == ToolRiskClass::Destructive);
        assert(reg.riskOf("app_launch") == ToolRiskClass::External);
        assert(reg.requiresConfirmation("fs_delete"));
        assert(reg.requiresConfirmation("run_command"));
        assert(!reg.requiresConfirmation("fs_list"));
        std::puts("  registry + risk classes OK");
    }

    FsListTool list;
    FsReadTool read;
    FsWriteTool write;
    FsMoveTool move;
    FsDeleteTool del;
    RunCommandTool run;
    ClipboardWriteTool clipW;
    ClipboardReadTool clipR;

    // --- fs_write create + fs_read round-trip -------------------------------
    {
        const std::string path = join(root, "hello.txt");
        auto w = write.invoke({{"path", path},
                               {"content", "hello polymath"},
                               {"mode", "create"}}, ctx);
        assert(w.ok && "fs_write create should succeed");
        assert(w.content.value("bytes", 0) == 14);

        // create again must fail
        auto w2 = write.invoke({{"path", path},
                                {"content", "nope"},
                                {"mode", "create"}}, ctx);
        assert(!w2.ok && "fs_write create on existing must fail");

        auto r = read.invoke({{"path", path}}, ctx);
        assert(r.ok);
        assert(r.content.value("content", "") == "hello polymath");
        std::puts("  fs_write create + fs_read OK");
    }

    // --- fs_write append ----------------------------------------------------
    {
        const std::string path = join(root, "hello.txt");
        auto a = write.invoke({{"path", path},
                               {"content", "!"},
                               {"mode", "append"}}, ctx);
        assert(a.ok);
        auto r = read.invoke({{"path", path}}, ctx);
        assert(r.ok);
        assert(r.content.value("content", "") == "hello polymath!");
        std::puts("  fs_write append OK");
    }

    // --- fs_write overwrite -------------------------------------------------
    {
        const std::string path = join(root, "hello.txt");
        auto o = write.invoke({{"path", path},
                               {"content", "reset"},
                               {"mode", "overwrite"}}, ctx);
        assert(o.ok);
        auto r = read.invoke({{"path", path}}, ctx);
        assert(r.ok && r.content.value("content", "") == "reset");
        std::puts("  fs_write overwrite OK");
    }

    // --- fs_list ------------------------------------------------------------
    {
        // seed a second file under sub/
        write.invoke({{"path", join(root, "sub/a.txt")},
                      {"content", "x"},
                      {"mode", "create"}}, ctx);
        auto L = list.invoke({{"path", root.string()}}, ctx);
        assert(L.ok);
        assert(L.content.value("count", 0) >= 2);  // hello.txt + sub/
        bool sawHello = false;
        bool sawSub = false;
        for (const auto& e : L.content["entries"]) {
            if (e.value("name", "") == "hello.txt") {
                sawHello = true;
                assert(e.value("type", "") == "file");
            }
            if (e.value("name", "") == "sub") {
                sawSub = true;
                assert(e.value("type", "") == "dir");
            }
        }
        assert(sawHello && sawSub);
        std::puts("  fs_list OK");
    }

    // --- fs_move ------------------------------------------------------------
    {
        const std::string src = join(root, "hello.txt");
        const std::string dst = join(root, "moved.txt");
        auto m = move.invoke({{"src", src}, {"dst", dst}}, ctx);
        assert(m.ok);
        assert(!QFileInfo::exists(QString::fromStdString(src)));
        assert(QFileInfo::exists(QString::fromStdString(dst)));
        std::puts("  fs_move OK");
    }

    // --- fs_delete → recycle bin / trash ------------------------------------
    {
        const std::string path = join(root, "to_delete.txt");
        auto w = write.invoke({{"path", path},
                               {"content", "bye"},
                               {"mode", "create"}}, ctx);
        assert(w.ok);
        assert(QFileInfo::exists(QString::fromStdString(path)));

        auto d = del.invoke({{"path", path}}, ctx);
        assert(d.ok && "fs_delete should succeed");
        assert(d.content.value("recycled", false) == true);
        // Original path must be gone (file is in trash).
        assert(!QFileInfo::exists(QString::fromStdString(path)) &&
               "fs_delete must remove from original path");
        std::puts("  fs_delete (trash) OK");
    }

    // --- run_command (benign) -----------------------------------------------
    {
        auto r = run.invoke({{"command", "Write-Output 'pm-ok'"},
                             {"cwd", root.string()},
                             {"timeout_s", 20}}, ctx);
        assert(r.ok && "run_command should succeed");
        const std::string out = r.content.value("output", "");
        assert(out.find("pm-ok") != std::string::npos && "stdout should contain pm-ok");
        assert(r.content.value("exit_code", -1) == 0);
        std::puts("  run_command OK");
    }

    // --- missing path errors ------------------------------------------------
    {
        auto r = read.invoke({{"path", join(root, "nope-missing.txt")}}, ctx);
        assert(!r.ok);
        auto L = list.invoke({{"path", join(root, "no-such-dir")}}, ctx);
        assert(!L.ok);
        std::puts("  missing-path errors OK");
    }

    // --- clipboard write + read ---------------------------------------------
    {
        auto w = clipW.invoke({{"text", "polymath-clipboard-test"}}, ctx);
        assert(w.ok && "clipboard_write");
        auto r = clipR.invoke({}, ctx);
        assert(r.ok && "clipboard_read");
        assert(r.content.value("text", "") == "polymath-clipboard-test");
        std::puts("  clipboard write+read OK");
    }

    // cleanup (best-effort; trash may still hold to_delete.txt)
    std::filesystem::remove_all(root, ec);

    std::puts("test_system_tools: ALL CHECKS PASSED");
    return 0;
}

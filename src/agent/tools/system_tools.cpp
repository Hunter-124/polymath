#include "system_tools.h"
#include "logging.h"

#include <QByteArray>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIODevice>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  ifdef _MSC_VER
#    pragma comment(lib, "shell32.lib")
#  endif
#endif

// system_tools — fs_* / run_command / app_launch / clipboard_* (overhaul2 C2).
// AgentLoop already runs SafetyPolicy before invoke(); we do not re-gate here.

namespace polymath {

namespace {

constexpr int kDefaultReadMaxKb = 64;
constexpr int kDefaultCmdTimeoutS = 30;
constexpr int kMaxCmdTimeoutS = 300;
constexpr int kOutputCapBytes = 8192;   // stdout+stderr total for the model

// Absolute, cleaned path string (native separators preferred for Windows APIs).
QString normalizePath(const std::string& raw) {
    if (raw.empty()) return {};
    QFileInfo fi(QString::fromStdString(raw));
    // absoluteFilePath resolves relative against the process cwd.
    return QDir::cleanPath(fi.absoluteFilePath());
}

std::string qToStd(const QString& s) { return s.toStdString(); }

// Truncate for tool results; note if clipped.
std::string capBytes(const QByteArray& data, int maxBytes, bool* truncated) {
    if (truncated) *truncated = data.size() > maxBytes;
    if (data.size() <= maxBytes) return QString::fromUtf8(data).toStdString();
    QByteArray head = data.left(maxBytes);
    return QString::fromUtf8(head).toStdString();
}

QStringList parseArgsField(const nlohmann::json& args) {
    QStringList out;
    if (!args.contains("args")) return out;
    const auto& a = args["args"];
    if (a.is_array()) {
        for (const auto& v : a) {
            if (v.is_string()) out << QString::fromStdString(v.get<std::string>());
            else out << QString::fromStdString(v.dump());
        }
    } else if (a.is_string()) {
        // Single string → one argv element (caller quoted if needed).
        out << QString::fromStdString(a.get<std::string>());
    }
    return out;
}

// Send path to Recycle Bin / Trash. Never hard-deletes.
bool moveToTrash(const QString& absPath, QString* errOut) {
    if (!QFileInfo::exists(absPath)) {
        if (errOut) *errOut = QStringLiteral("path does not exist");
        return false;
    }
#ifdef Q_OS_WIN
    // SHFileOperation requires a double-NUL-terminated absolute path list.
    const QString native = QDir::toNativeSeparators(absPath);
    std::wstring w = native.toStdWString();
    std::vector<wchar_t> from(w.begin(), w.end());
    from.push_back(L'\0');
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.hwnd   = nullptr;
    op.wFunc  = FO_DELETE;
    op.pFrom  = from.data();
    op.pTo    = nullptr;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    op.fAnyOperationsAborted = FALSE;
    const int rc = SHFileOperationW(&op);
    if (rc != 0 || op.fAnyOperationsAborted) {
        if (errOut)
            *errOut = QStringLiteral("SHFileOperation failed (code %1)").arg(rc);
        return false;
    }
    return true;
#else
    QString trashPath;
    if (!QFile::moveToTrash(absPath, &trashPath)) {
        if (errOut) *errOut = QStringLiteral("moveToTrash failed");
        return false;
    }
    return true;
#endif
}

} // namespace

// --- fs_list ----------------------------------------------------------------

std::string FsListTool::name() const { return "fs_list"; }
std::string FsListTool::description() const {
    return "List files and folders in a directory. "
           "Example: {\"path\":\"C:/Users/Me/Documents\"} → names, types, sizes.";
}

nlohmann::json FsListTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"},
                      {"description", "Directory to list (absolute preferred)"}}},
        }},
        {"required", {"path"}},
    };
}

ToolResult FsListTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string raw = args.value("path", "");
    if (raw.empty())
        return {false, {{"error", "path required"}}, "fs_list: missing path"};

    const QString path = normalizePath(raw);
    QFileInfo fi(path);
    if (!fi.exists())
        return {false, {{"error", "not found"}, {"path", qToStd(path)}},
                "fs_list: not found: " + qToStd(path)};
    if (!fi.isDir())
        return {false, {{"error", "not a directory"}, {"path", qToStd(path)}},
                "fs_list: not a directory: " + qToStd(path)};

    QDir dir(path);
    const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                           QDir::DirsFirst | QDir::Name);
    nlohmann::json items = nlohmann::json::array();
    for (const QFileInfo& e : entries) {
        nlohmann::json row = {
            {"name", e.fileName().toStdString()},
            {"type", e.isDir() ? "dir" : (e.isSymLink() ? "symlink" : "file")},
            {"path", e.absoluteFilePath().toStdString()},
        };
        if (e.isFile()) row["size"] = static_cast<int64_t>(e.size());
        items.push_back(std::move(row));
    }
    return {true,
            {{"path", qToStd(path)}, {"count", items.size()}, {"entries", items}},
            "Listed " + std::to_string(items.size()) + " entries in " + qToStd(path)};
}

// --- fs_read ----------------------------------------------------------------

std::string FsReadTool::name() const { return "fs_read"; }
std::string FsReadTool::description() const {
    return "Read a text file (UTF-8). Optional max_kb caps size (default 64). "
           "Example: {\"path\":\"C:/Users/Me/notes.txt\",\"max_kb\":32}";
}

nlohmann::json FsReadTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path",   {{"type", "string"}, {"description", "File path to read"}}},
            {"max_kb", {{"type", "integer"},
                        {"description", "Max kilobytes to return (default 64)"}}},
        }},
        {"required", {"path"}},
    };
}

ToolResult FsReadTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string raw = args.value("path", "");
    if (raw.empty())
        return {false, {{"error", "path required"}}, "fs_read: missing path"};

    int maxKb = kDefaultReadMaxKb;
    if (args.contains("max_kb") && args["max_kb"].is_number_integer())
        maxKb = args["max_kb"].get<int>();
    if (maxKb < 1) maxKb = 1;
    if (maxKb > 4096) maxKb = 4096;
    const qint64 maxBytes = static_cast<qint64>(maxKb) * 1024;

    const QString path = normalizePath(raw);
    QFileInfo fi(path);
    if (!fi.exists())
        return {false, {{"error", "not found"}, {"path", qToStd(path)}},
                "fs_read: not found: " + qToStd(path)};
    if (!fi.isFile())
        return {false, {{"error", "not a file"}, {"path", qToStd(path)}},
                "fs_read: not a file: " + qToStd(path)};

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {false, {{"error", "open failed"}, {"path", qToStd(path)}},
                "fs_read: cannot open " + qToStd(path)};

    const qint64 total = f.size();
    const QByteArray data = f.read(maxBytes);
    f.close();
    const bool truncated = total > static_cast<qint64>(data.size());
    const std::string text = QString::fromUtf8(data).toStdString();

    return {true,
            {{"path", qToStd(path)},
             {"size", static_cast<int64_t>(total)},
             {"truncated", truncated},
             {"content", text}},
            truncated ? "Read (truncated) " + qToStd(path)
                      : "Read " + qToStd(path)};
}

// --- fs_write ---------------------------------------------------------------

std::string FsWriteTool::name() const { return "fs_write"; }
std::string FsWriteTool::description() const {
    return "Write text to a file. mode: create (fail if exists), overwrite, or append. "
           "Example: {\"path\":\"C:/Users/Me/Desktop/hi.txt\",\"content\":\"hello\","
           "\"mode\":\"create\"}";
}

nlohmann::json FsWriteTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path",    {{"type", "string"}, {"description", "File path to write"}}},
            {"content", {{"type", "string"}, {"description", "UTF-8 text to write"}}},
            {"mode",    {{"type", "string"},
                         {"description", "create | overwrite | append (default create)"}}},
        }},
        {"required", {"path", "content"}},
    };
}

ToolResult FsWriteTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string raw = args.value("path", "");
    if (raw.empty())
        return {false, {{"error", "path required"}}, "fs_write: missing path"};

    // content may be string or other JSON (stringify non-strings).
    std::string content;
    if (args.contains("content")) {
        if (args["content"].is_string()) content = args["content"].get<std::string>();
        else content = args["content"].dump();
    }

    std::string mode = args.value("mode", "create");
    // normalize case-insensitive-ish
    if (mode != "create" && mode != "overwrite" && mode != "append")
        return {false, {{"error", "mode must be create|overwrite|append"}, {"mode", mode}},
                "fs_write: bad mode"};

    const QString path = normalizePath(raw);
    QFileInfo fi(path);
    const bool exists = fi.exists();

    if (mode == "create" && exists)
        return {false, {{"error", "file already exists (use mode=overwrite or append)"},
                        {"path", qToStd(path)}},
                "fs_write: exists (create refused): " + qToStd(path)};

    // Ensure parent directory: create intermediate dirs when missing.
    // SafetyPolicy already restricted path roots; keep mkdir simple.
    const QString parent = fi.absolutePath();
    if (!parent.isEmpty() && !QDir(parent).exists()) {
        if (!QDir().mkpath(parent))
            return {false, {{"error", "cannot create parent directory"},
                            {"parent", qToStd(parent)}},
                    "fs_write: mkpath failed: " + qToStd(parent)};
    }

    QIODevice::OpenMode flags = QIODevice::WriteOnly;
    if (mode == "append")
        flags = QIODevice::WriteOnly | QIODevice::Append;
    else
        flags = QIODevice::WriteOnly | QIODevice::Truncate;

    QFile f(path);
    if (!f.open(flags))
        return {false, {{"error", "open failed"}, {"path", qToStd(path)}},
                "fs_write: cannot open " + qToStd(path)};

    const QByteArray bytes(content.data(), static_cast<int>(content.size()));
    const qint64 n = f.write(bytes);
    f.close();
    if (n != bytes.size())
        return {false, {{"error", "short write"}, {"path", qToStd(path)}},
                "fs_write: short write to " + qToStd(path)};

    PM_INFO("fs_write: {} bytes mode={} path={}", n, mode, qToStd(path));
    return {true,
            {{"path", qToStd(path)},
             {"mode", mode},
             {"bytes", static_cast<int64_t>(n)},
             {"created", !exists && mode != "append"}},
            "Wrote " + std::to_string(n) + " bytes (" + mode + ") to " + qToStd(path)};
}

// --- fs_move ----------------------------------------------------------------

std::string FsMoveTool::name() const { return "fs_move"; }
std::string FsMoveTool::description() const {
    return "Move or rename a file/folder (Destructive — user confirm). "
           "Example: {\"src\":\"C:/Users/Me/a.txt\",\"dst\":\"C:/Users/Me/b.txt\"}";
}

nlohmann::json FsMoveTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"src", {{"type", "string"}, {"description", "Source path"}}},
            {"dst", {{"type", "string"}, {"description", "Destination path"}}},
        }},
        {"required", {"src", "dst"}},
    };
}

ToolResult FsMoveTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string srcRaw = args.value("src", "");
    const std::string dstRaw = args.value("dst", "");
    if (srcRaw.empty() || dstRaw.empty())
        return {false, {{"error", "src and dst required"}}, "fs_move: missing src/dst"};

    const QString src = normalizePath(srcRaw);
    const QString dst = normalizePath(dstRaw);
    if (!QFileInfo::exists(src))
        return {false, {{"error", "source not found"}, {"src", qToStd(src)}},
                "fs_move: source missing: " + qToStd(src)};
    if (QFileInfo::exists(dst))
        return {false, {{"error", "destination already exists"}, {"dst", qToStd(dst)}},
                "fs_move: destination exists: " + qToStd(dst)};

    // Create destination parent if needed.
    const QString parent = QFileInfo(dst).absolutePath();
    if (!parent.isEmpty() && !QDir(parent).exists()) {
        if (!QDir().mkpath(parent))
            return {false, {{"error", "cannot create destination parent"},
                            {"parent", qToStd(parent)}},
                    "fs_move: mkpath failed"};
    }

    if (!QFile::rename(src, dst)) {
        // Cross-volume rename can fail; try copy+trash source as fallback? Keep simple.
        return {false, {{"error", "rename failed"},
                        {"src", qToStd(src)}, {"dst", qToStd(dst)}},
                "fs_move: rename failed"};
    }
    return {true, {{"src", qToStd(src)}, {"dst", qToStd(dst)}},
            "Moved " + qToStd(src) + " → " + qToStd(dst)};
}

// --- fs_delete --------------------------------------------------------------

std::string FsDeleteTool::name() const { return "fs_delete"; }
std::string FsDeleteTool::description() const {
    return "Delete a file/folder by sending it to the Recycle Bin (never hard-deletes). "
           "Destructive — user confirm. Example: {\"path\":\"C:/Users/Me/old.txt\"}";
}

nlohmann::json FsDeleteTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Path to recycle"}}},
        }},
        {"required", {"path"}},
    };
}

ToolResult FsDeleteTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string raw = args.value("path", "");
    if (raw.empty())
        return {false, {{"error", "path required"}}, "fs_delete: missing path"};

    const QString path = normalizePath(raw);
    if (!QFileInfo::exists(path))
        return {false, {{"error", "not found"}, {"path", qToStd(path)}},
                "fs_delete: not found: " + qToStd(path)};

    QString err;
    if (!moveToTrash(path, &err))
        return {false, {{"error", err.toStdString()}, {"path", qToStd(path)}},
                "fs_delete: " + err.toStdString()};

    // Confirm it's gone from the original location (Recycle Bin may retain it).
    const bool gone = !QFileInfo::exists(path);
    PM_INFO("fs_delete: recycled {} (gone={})", qToStd(path), gone);
    return {true,
            {{"path", qToStd(path)}, {"recycled", true}, {"removed_from_path", gone}},
            "Sent to Recycle Bin: " + qToStd(path)};
}

// --- run_command ------------------------------------------------------------

std::string RunCommandTool::name() const { return "run_command"; }
std::string RunCommandTool::description() const {
    return "Run a shell command via PowerShell (-NoProfile -NonInteractive). "
           "Destructive — user confirm. Captures stdout+stderr (max 8k). "
           "Example: {\"command\":\"Get-Date\",\"cwd\":\"C:/Users/Me\",\"timeout_s\":15}";
}

nlohmann::json RunCommandTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"command",   {{"type", "string"},
                           {"description", "PowerShell / shell command line"}}},
            {"cwd",       {{"type", "string"},
                           {"description", "Working directory (optional)"}}},
            {"timeout_s", {{"type", "integer"},
                           {"description", "Timeout seconds (default 30, max 300)"}}},
        }},
        {"required", {"command"}},
    };
}

ToolResult RunCommandTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string command = args.value("command", "");
    if (command.empty())
        return {false, {{"error", "command required"}}, "run_command: missing command"};

    int timeoutS = kDefaultCmdTimeoutS;
    if (args.contains("timeout_s") && args["timeout_s"].is_number_integer())
        timeoutS = args["timeout_s"].get<int>();
    if (timeoutS < 1) timeoutS = 1;
    if (timeoutS > kMaxCmdTimeoutS) timeoutS = kMaxCmdTimeoutS;

    QString cwd;
    if (args.contains("cwd") && args["cwd"].is_string()) {
        const std::string rawCwd = args["cwd"].get<std::string>();
        if (!rawCwd.empty()) {
            cwd = normalizePath(rawCwd);
            QFileInfo cfi(cwd);
            if (!cfi.exists() || !cfi.isDir())
                return {false, {{"error", "cwd not a usable directory"},
                                {"cwd", qToStd(cwd)}},
                        "run_command: bad cwd: " + qToStd(cwd)};
        }
    }

    QProcess proc;
    if (!cwd.isEmpty()) proc.setWorkingDirectory(cwd);
    proc.setProcessChannelMode(QProcess::MergedChannels);

#ifdef Q_OS_WIN
    // PowerShell -NoProfile -NonInteractive -Command <command>
    const QString program = QStringLiteral("powershell.exe");
    const QStringList argv = {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-NonInteractive"),
        QStringLiteral("-Command"),
        QString::fromStdString(command),
    };
#else
    const QString program = QStringLiteral("/bin/sh");
    const QStringList argv = {QStringLiteral("-c"), QString::fromStdString(command)};
#endif

    proc.start(program, argv);
    if (!proc.waitForStarted(5000)) {
        return {false,
                {{"error", "failed to start process"},
                 {"program", program.toStdString()}},
                "run_command: start failed"};
    }

    const bool finished = proc.waitForFinished(timeoutS * 1000);
    if (!finished) {
        proc.kill();
        proc.waitForFinished(3000);
        QByteArray partial = proc.readAll();
        bool trunc = false;
        const std::string out = capBytes(partial, kOutputCapBytes, &trunc);
        return {false,
                {{"error", "timeout"},
                 {"timeout_s", timeoutS},
                 {"output", out},
                 {"truncated", trunc}},
                "run_command: timed out after " + std::to_string(timeoutS) + "s"};
    }

    QByteArray all = proc.readAll();
    // Also drain any leftover split channels if mode changed.
    all += proc.readAllStandardOutput();
    all += proc.readAllStandardError();
    bool trunc = false;
    const std::string out = capBytes(all, kOutputCapBytes, &trunc);
    const int exitCode = proc.exitCode();
    const bool ok = (proc.exitStatus() == QProcess::NormalExit);

    nlohmann::json content = {
        {"command", command},
        {"exit_code", exitCode},
        {"output", out},
        {"truncated", trunc},
        {"timed_out", false},
    };
    if (!cwd.isEmpty()) content["cwd"] = qToStd(cwd);

    if (!ok) {
        content["error"] = "process crashed";
        return {false, std::move(content), "run_command: process crashed"};
    }
    // Non-zero exit is still a successful tool invocation (command ran); surface code.
    return {true, std::move(content),
            "ran (exit " + std::to_string(exitCode) + ")"};
}

// --- app_launch -------------------------------------------------------------

std::string AppLaunchTool::name() const { return "app_launch"; }
std::string AppLaunchTool::description() const {
    return "Launch a desktop app by path or simple name (detached). "
           "Example: {\"name_or_path\":\"notepad.exe\"} or "
           "{\"name_or_path\":\"C:/Windows/System32/notepad.exe\",\"args\":[\"notes.txt\"]}";
}

nlohmann::json AppLaunchTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"name_or_path", {{"type", "string"},
                              {"description", "Executable path or PATH/simple name"}}},
            {"args", {{"description", "Optional args: string array or one string"}}},
        }},
        {"required", {"name_or_path"}},
    };
}

ToolResult AppLaunchTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string raw = args.value("name_or_path", "");
    if (raw.empty())
        return {false, {{"error", "name_or_path required"}}, "app_launch: missing name_or_path"};

    const QStringList argv = parseArgsField(args);
    QString program = QString::fromStdString(raw);

    // Prefer absolute/existing path when the string looks like a path.
    const QFileInfo asPath(program);
    if (asPath.exists() && asPath.isFile()) {
        program = asPath.absoluteFilePath();
    }

    qint64 pid = 0;
    const bool ok = QProcess::startDetached(program, argv, QString(), &pid);
    if (!ok) {
#ifdef Q_OS_WIN
        // Fallback: ShellExecuteW for Start-Menu / registered apps.
        const std::wstring wprog = program.toStdWString();
        std::wstring wparams;
        for (int i = 0; i < argv.size(); ++i) {
            if (i) wparams += L' ';
            // Quote args with spaces.
            const QString a = argv[i];
            if (a.contains(QLatin1Char(' ')))
                wparams += L'"' + a.toStdWString() + L'"';
            else
                wparams += a.toStdWString();
        }
        const HINSTANCE hi = ShellExecuteW(
            nullptr, L"open", wprog.c_str(),
            wparams.empty() ? nullptr : wparams.c_str(),
            nullptr, SW_SHOWNORMAL);
        const auto code = reinterpret_cast<intptr_t>(hi);
        if (code <= 32) {
            return {false,
                    {{"error", "launch failed"},
                     {"name_or_path", raw},
                     {"shell_code", static_cast<int>(code)}},
                    "app_launch: failed for " + raw};
        }
        return {true,
                {{"name_or_path", raw}, {"launched", true}, {"via", "ShellExecute"}},
                "Launched " + raw};
#else
        return {false, {{"error", "launch failed"}, {"name_or_path", raw}},
                "app_launch: failed for " + raw};
#endif
    }

    nlohmann::json content = {
        {"name_or_path", raw},
        {"program", program.toStdString()},
        {"launched", true},
        {"pid", static_cast<int64_t>(pid)},
    };
    return {true, std::move(content), "Launched " + raw};
}

// --- clipboard_read ---------------------------------------------------------

std::string ClipboardReadTool::name() const { return "clipboard_read"; }
std::string ClipboardReadTool::description() const {
    return "Read the current system clipboard text. No arguments. "
           "Example: {} → {\"text\":\"copied stuff\"}";
}

nlohmann::json ClipboardReadTool::parametersSchema() const {
    return {{"type", "object"}, {"properties", nlohmann::json::object()}};
}

ToolResult ClipboardReadTool::invoke(const nlohmann::json&, ToolContext&) {
    if (!QGuiApplication::instance())
        return {false, {{"error", "no QGuiApplication (clipboard unavailable)"}},
                "clipboard_read: no GUI app"};
    QClipboard* cb = QGuiApplication::clipboard();
    if (!cb)
        return {false, {{"error", "clipboard unavailable"}}, "clipboard_read: no clipboard"};
    const QString text = cb->text(QClipboard::Clipboard);
    return {true, {{"text", text.toStdString()}, {"length", text.size()}},
            "Read clipboard (" + std::to_string(text.size()) + " chars)"};
}

// --- clipboard_write --------------------------------------------------------

std::string ClipboardWriteTool::name() const { return "clipboard_write"; }
std::string ClipboardWriteTool::description() const {
    return "Set the system clipboard text. "
           "Example: {\"text\":\"hello from Polymath\"}";
}

nlohmann::json ClipboardWriteTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"text", {{"type", "string"}, {"description", "Text to place on clipboard"}}},
        }},
        {"required", {"text"}},
    };
}

ToolResult ClipboardWriteTool::invoke(const nlohmann::json& args, ToolContext&) {
    if (!args.contains("text"))
        return {false, {{"error", "text required"}}, "clipboard_write: missing text"};
    std::string text;
    if (args["text"].is_string()) text = args["text"].get<std::string>();
    else text = args["text"].dump();

    if (!QGuiApplication::instance())
        return {false, {{"error", "no QGuiApplication (clipboard unavailable)"}},
                "clipboard_write: no GUI app"};
    QClipboard* cb = QGuiApplication::clipboard();
    if (!cb)
        return {false, {{"error", "clipboard unavailable"}}, "clipboard_write: no clipboard"};
    cb->setText(QString::fromStdString(text), QClipboard::Clipboard);
    return {true, {{"length", static_cast<int>(text.size())}, {"written", true}},
            "Wrote " + std::to_string(text.size()) + " chars to clipboard"};
}

} // namespace polymath

// C4 — agent sessions end-to-end (docs/overhaul/05_AGENT_SESSIONS.md §6).
//
// Deterministic (always run):
//   * provider registry + availability gating
//   * SpawnSpec / cwd allowlist enforcement
//   * stream-json fixture parsing → AgentEvent sequences
//   * SessionsModel state transitions
//   * EventBus → NotificationsModel delivery (NeedsInput → Notice)
// Live (skip-green if `claude` not on PATH):
//   * spawn claude -p "say READY" in a temp dir → Result containing READY
//
#include "agent_session_service.h"
#include "claude_stream_parse.h"
#include "claude_code_provider.h"
#include "codex_provider.h"
#include "generic_pty_provider.h"
#include "sessions_model.h"
#include "notifications_model.h"
#include "config.h"
#include "database.h"
#include "event_bus.h"
#include "paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace polymath;

static std::string g_fixtures;

static QString readFixture(const char* name) {
    const auto path = std::filesystem::path(g_fixtures) / name;
    QFile f(QString::fromStdString(path.string()));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "missing fixture %s\n", path.string().c_str());
        assert(false && "fixture missing");
    }
    return QString::fromUtf8(f.readAll());
}

static void test_fixture_simple_result() {
    std::puts("  fixture: simple_result");
    QString native;
    const auto events = parseClaudeStreamTranscript(
        readFixture("simple_result.jsonl"), QStringLiteral("pm-1"), &native);
    assert(native == QLatin1String("sess-simple-001"));
    bool saw_started = false, saw_assistant = false, saw_result = false;
    double cost = 0;
    for (const auto& e : events) {
        assert(e.session_id == QLatin1String("pm-1"));
        if (e.kind == AgentEvent::Started) saw_started = true;
        if (e.kind == AgentEvent::AssistantText) {
            saw_assistant = true;
            assert(e.text.contains(QLatin1String("READY")));
        }
        if (e.kind == AgentEvent::Result) {
            saw_result = true;
            assert(e.text.contains(QLatin1String("READY")));
        }
        if (e.kind == AgentEvent::CostUpdate || e.cost_usd > 0)
            cost = e.cost_usd > 0 ? e.cost_usd : cost;
    }
    assert(saw_started && saw_assistant && saw_result);
    assert(cost > 0.01 && cost < 0.02);
}

static void test_fixture_needs_input() {
    std::puts("  fixture: needs_input");
    const auto events = parseClaudeStreamTranscript(
        readFixture("needs_input.jsonl"), QStringLiteral("pm-2"), nullptr);
    bool saw_needs = false;
    for (const auto& e : events) {
        if (e.kind == AgentEvent::NeedsInput || e.kind == AgentEvent::PermissionRequest) {
            saw_needs = true;
            assert(!e.text.isEmpty() || !e.raw_json.isEmpty());
        }
        // Must never invent an auto-approve Result from a permission line.
        if (e.kind == AgentEvent::Result)
            assert(false && "permission transcript must not yield Result");
    }
    assert(saw_needs);
}

static void test_fixture_error() {
    std::puts("  fixture: error");
    const auto events = parseClaudeStreamTranscript(
        readFixture("error.jsonl"), QStringLiteral("pm-3"), nullptr);
    bool saw_error = false;
    for (const auto& e : events) {
        if (e.kind == AgentEvent::Error) {
            saw_error = true;
            assert(e.text.contains(QLatin1String("rate limited"))
                   || e.text.contains(QLatin1String("error")));
        }
    }
    assert(saw_error);
}

static void test_allowlist_and_registry() {
    std::puts("  allowlist + registry");
    QTemporaryDir tmp;
    assert(tmp.isValid());
    const auto dbPath = (std::filesystem::path(tmp.path().toStdString()) / "t.db").string();
    Database db;
    assert(db.open(dbPath));
    Config cfg(db);
    cfg.seedDefaults();

    AgentSessionService svc(db, cfg);
    // Register only a stub-free set; start() will add defaults if empty — call
    // ensureSchema + register manually without start's process wiring noise.
    svc.ensureSchema();
    svc.registerProvider(std::make_unique<ClaudeCodeProvider>());
    svc.registerProvider(std::make_unique<CodexProvider>());
    svc.registerProvider(std::make_unique<GenericPtyProvider>());

    // Empty allowed_dirs → spawn refused.
    cfg.set(keys::AgentsAllowedDirs, "");
    assert(svc.validateCwd(tmp.path()).contains(QLatin1String("disabled"))
           || svc.validateCwd(tmp.path()).contains(QLatin1String("empty")));
    const QString id0 = svc.spawn(QStringLiteral("claude-code"), tmp.path(),
                                  QStringLiteral("hi"), QStringLiteral("t"));
    assert(id0.isEmpty());
    assert(!svc.lastError().isEmpty());

    // Allow the temp dir → validate passes.
    cfg.set(keys::AgentsAllowedDirs, tmp.path().toStdString());
    assert(svc.validateCwd(tmp.path()).isEmpty());
    // Nested path also allowed.
    const QString nested = tmp.path() + QStringLiteral("/sub");
    QDir().mkpath(nested);
    assert(svc.validateCwd(nested).isEmpty());
    // Outside path refused.
    assert(!svc.validateCwd(QStringLiteral("C:/Windows/System32")).isEmpty()
           || !svc.validateCwd(QStringLiteral("/etc")).isEmpty());

    // Registry
    const auto names = svc.providerNames();
    assert(names.contains(QStringLiteral("claude-code")));
    assert(names.contains(QStringLiteral("codex")));
    assert(names.contains(QStringLiteral("pty")));
    // Availability: claude may or may not be present; gating must not crash.
    (void)svc.providerAvailable(QStringLiteral("claude-code"));
    (void)svc.providerAvailable(QStringLiteral("codex"));
    assert(svc.providerAvailable(QStringLiteral("pty")) == true
           || svc.provider(QStringLiteral("pty")) != nullptr);

    // Concurrency cap: set max=0 effectively by max=1 and filling — just check reader.
    cfg.set(keys::AgentsMaxConcurrent, "2");
    assert(svc.maxConcurrent() == 2);

    db.close();
}

static void test_sessions_model_states() {
    std::puts("  SessionsModel state transitions");
    QTemporaryDir tmp;
    assert(tmp.isValid());
    Database db;
    assert(db.open((std::filesystem::path(tmp.path().toStdString()) / "m.db").string()));
    Config cfg(db);
    cfg.seedDefaults();
    cfg.set(keys::AgentsAllowedDirs, tmp.path().toStdString());

    AgentSessionService svc(db, cfg);
    svc.ensureSchema();
    // Don't call start() — avoid provider process wiring; feed events directly.

    SessionsModel model(db);
    model.setService(&svc);

    // Inject Started → working
    {
        AgentSessionEvent e;
        e.session_id = QStringLiteral("s1");
        e.kind = QStringLiteral("Started");
        e.text = QStringLiteral("started");
        e.ts = QDateTime::currentMSecsSinceEpoch();
        model.onAgentSessionEvent(e);
    }
    assert(model.rowCount() == 1);
    assert(model.data(model.index(0), SessionsModel::StatusRole).toString()
           == QLatin1String("working"));
    assert(model.data(model.index(0), SessionsModel::IdRole).toString()
           == QLatin1String("s1"));

    // AssistantText keeps working
    {
        AgentSessionEvent e;
        e.session_id = QStringLiteral("s1");
        e.kind = QStringLiteral("AssistantText");
        e.text = QStringLiteral("thinking about it");
        e.ts = QDateTime::currentMSecsSinceEpoch();
        model.onAgentSessionEvent(e);
    }
    assert(model.data(model.index(0), SessionsModel::StatusRole).toString()
           == QLatin1String("working"));
    assert(model.data(model.index(0), SessionsModel::LastMessageRole).toString()
           .contains(QLatin1String("thinking")));

    // NeedsInput → needs_input + unreadPing
    {
        AgentSessionEvent e;
        e.session_id = QStringLiteral("s1");
        e.kind = QStringLiteral("NeedsInput");
        e.text = QStringLiteral("Allow edit?");
        e.ts = QDateTime::currentMSecsSinceEpoch();
        model.onAgentSessionEvent(e);
    }
    assert(model.data(model.index(0), SessionsModel::StatusRole).toString()
           == QLatin1String("needs_input"));
    assert(model.data(model.index(0), SessionsModel::UnreadPingRole).toBool() == true);

    model.clearPing(QStringLiteral("s1"));
    assert(model.data(model.index(0), SessionsModel::UnreadPingRole).toBool() == false);

    // Result → done + cost
    {
        AgentSessionEvent e;
        e.session_id = QStringLiteral("s1");
        e.kind = QStringLiteral("Result");
        e.text = QStringLiteral("READY");
        e.cost_usd = 0.05;
        e.ts = QDateTime::currentMSecsSinceEpoch();
        model.onAgentSessionEvent(e);
    }
    assert(model.data(model.index(0), SessionsModel::StatusRole).toString()
           == QLatin1String("done"));
    assert(model.data(model.index(0), SessionsModel::CostUsdRole).toDouble() > 0.04);

    // Error session
    {
        AgentSessionEvent e;
        e.session_id = QStringLiteral("s2");
        e.kind = QStringLiteral("Error");
        e.text = QStringLiteral("boom");
        e.ts = QDateTime::currentMSecsSinceEpoch();
        model.onAgentSessionEvent(e);
    }
    assert(model.rowCount() == 2);
    // Find s2
    bool found_err = false;
    for (int i = 0; i < model.rowCount(); ++i) {
        if (model.data(model.index(i), SessionsModel::IdRole).toString() == QLatin1String("s2")) {
            assert(model.data(model.index(i), SessionsModel::StatusRole).toString()
                   == QLatin1String("error"));
            found_err = true;
        }
    }
    assert(found_err);

    // Event log retained
    assert(!model.eventLog(QStringLiteral("s1")).isEmpty());

    db.close();
}

static void test_eventbus_notice_delivery() {
    std::puts("  EventBus NeedsInput → Notice → NotificationsModel");
    QTemporaryDir tmp;
    assert(tmp.isValid());
    Database db;
    assert(db.open((std::filesystem::path(tmp.path().toStdString()) / "n.db").string()));
    Config cfg(db);
    cfg.seedDefaults();
    cfg.set(keys::AgentsAllowedDirs, tmp.path().toStdString());
    cfg.set(keys::AgentsSpeakNeedsInput, "0");  // no TTS in test

    AgentSessionService svc(db, cfg);
    // Manually register providers and connect without full start() process load —
    // start() is fine; it only probes binaries.
    svc.start();

    NotificationsModel notes(db);
    QObject::connect(&EventBus::instance(), &EventBus::notice,
                     &notes, &NotificationsModel::onNotice);

    // Drive a NeedsInput through the service's provider-event path.
    AgentEvent e;
    e.session_id = QStringLiteral("bus-1");
    e.kind = AgentEvent::NeedsInput;
    e.text = QStringLiteral("Allow editing foo.cpp?");
    e.ts = QDateTime::currentMSecsSinceEpoch();
    // Seed a live row so notice title is nice.
    svc.onProviderEvent(e);

    // Process queued connections if any (DirectConnection for notice from same thread).
    QCoreApplication::processEvents();

    assert(notes.rowCount() >= 1);
    bool found = false;
    for (int i = 0; i < notes.rowCount(); ++i) {
        const QString src = notes.data(notes.index(i), NotificationsModel::SourceRole).toString();
        const QString body = notes.data(notes.index(i), NotificationsModel::BodyRole).toString();
        if (src == QLatin1String("Agents") || body.contains(QLatin1String("needs input"), Qt::CaseInsensitive)
            || body.contains(QLatin1String("Allow editing"))) {
            found = true;
            break;
        }
    }
    assert(found);

    svc.stop();
    db.close();
}

static void test_pty_classify() {
    std::puts("  GenericPtyProvider classify");
    PtyProfile p;
    p.needs_input_patterns = {QRegularExpression(QStringLiteral("\\(y/n\\)"),
                                                 QRegularExpression::CaseInsensitiveOption)};
    p.done_patterns = {QRegularExpression(QStringLiteral("done\\."))};
    p.error_patterns = {QRegularExpression(QStringLiteral("fatal:"),
                                           QRegularExpression::CaseInsensitiveOption)};
    assert(GenericPtyProvider::classifyChunk(QStringLiteral("Continue? (y/n)"), p)
           == AgentEvent::NeedsInput);
    assert(GenericPtyProvider::classifyChunk(QStringLiteral("All done."), p)
           == AgentEvent::Result);
    assert(GenericPtyProvider::classifyChunk(QStringLiteral("fatal: boom"), p)
           == AgentEvent::Error);
    assert(GenericPtyProvider::classifyChunk(QStringLiteral("hello world"), p)
           == AgentEvent::AssistantText);
}

static void test_live_claude_skip_green() {
    std::puts("  live claude (skip if missing)");
    const QString bin = ClaudeCodeProvider::resolveBinary();
    if (bin.isEmpty()) {
        std::puts("  SKIP live claude — not on PATH");
        return;
    }

    QTemporaryDir tmp;
    assert(tmp.isValid());
    Database db;
    assert(db.open((std::filesystem::path(tmp.path().toStdString()) / "live.db").string()));
    Config cfg(db);
    cfg.seedDefaults();
    cfg.set(keys::AgentsAllowedDirs, tmp.path().toStdString());
    cfg.set(keys::AgentsSpeakNeedsInput, "0");

    AgentSessionService svc(db, cfg);
    svc.start();

    bool got_result = false;
    QString result_text;
    QObject::connect(&svc, &AgentSessionService::sessionEvent, &svc,
                     [&](const AgentEvent& e) {
                         if (e.kind == AgentEvent::Result || e.kind == AgentEvent::AssistantText) {
                             if (e.text.contains(QLatin1String("READY"), Qt::CaseInsensitive)) {
                                 got_result = true;
                                 result_text = e.text;
                             }
                         }
                         if (e.kind == AgentEvent::Result && e.cost_usd > 0) {
                             // cost captured — good
                         }
                     });

    const QString id = svc.spawn(QStringLiteral("claude-code"), tmp.path(),
                                 QStringLiteral("say READY and nothing else"),
                                 QStringLiteral("live-test"));
    if (id.isEmpty()) {
        std::printf("  SKIP live claude — spawn failed: %s\n",
                    qPrintable(svc.lastError()));
        svc.stop();
        db.close();
        return;
    }

    // Wait up to 90s for a READY result (network + CLI).
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (got_result) loop.quit();
    });
    poll.start(200);
    deadline.start(90000);
    loop.exec();

    if (!got_result) {
        std::puts("  SKIP live claude — timed out waiting for READY (auth/network?)");
        svc.stop(id);
        svc.stop();
        db.close();
        return;
    }
    assert(result_text.contains(QLatin1String("READY"), Qt::CaseInsensitive));
    std::puts("  live claude: OK");
    svc.stop();
    db.close();
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

#ifdef PM_SESSIONS_FIXTURES
    g_fixtures = PM_SESSIONS_FIXTURES;
#else
    g_fixtures = (std::filesystem::path(__FILE__).parent_path() / "fixtures" / "sessions").string();
#endif

    // Quiet logging to temp.
    QTemporaryDir logTmp;
    if (logTmp.isValid())
        Paths::instance().setRoot(std::filesystem::path(logTmp.path().toStdString()));

    std::puts("test_sessions_e2e:");
    test_fixture_simple_result();
    test_fixture_needs_input();
    test_fixture_error();
    test_allowlist_and_registry();
    test_sessions_model_states();
    test_eventbus_notice_delivery();
    test_pty_classify();
    test_live_claude_skip_green();
    std::puts("test_sessions_e2e: OK");
    return 0;
}

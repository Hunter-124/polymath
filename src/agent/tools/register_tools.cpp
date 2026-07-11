#include "tool_registry.h"

#include "shopping_tool.h"
#include "web_search.h"
#include "fetch_page.h"
#include "youtube_search.h"
#include "browser_drive.h"
#include "documents.h"
#include "print.h"
#include "reminders.h"
#include "memory_tools.h"
#include "camera_tools.h"
#include "queue_tool.h"
#include "schedule_tools.h"
#include "skill_tools.h"
#include "uicontrol_tool.h"
#include "agent_session_tools.h"
#include "system_tools.h"
#include "screen_tools.h"
#include "skills/skill_registry.h"
#include "paths.h"
#include "logging.h"

#include <QObject>

#include <filesystem>
#include <memory>
#include <mutex>

// register_tools — the single place that wires every builtin ITool into the
// registry. Moved out of shopping_tool.cpp so each tool family lives in its own
// translation unit. The AgentRuntime constructs one ToolRegistry and calls this
// once; per-turn filtering against the active personality's allow-list happens
// later in ToolRegistry::specs().
//
// C5: run_skill / save_skill / agent_* / ui_control + risk classes (03 §5).

namespace polymath {

namespace {

std::mutex g_deps_mu;
QObject* g_sessions = nullptr;   // AgentSessionService* (opaque; Q_INVOKABLE)
std::unique_ptr<SkillRegistry> g_owned_skills;
SkillRegistry* g_skills = nullptr;

SkillRegistry* ensureSkills(SkillRegistry* provided) {
    if (provided) {
        g_skills = provided;
        return provided;
    }
    if (g_skills) return g_skills;
    if (!g_owned_skills) {
        g_owned_skills = std::make_unique<SkillRegistry>();
        // Prefer data/skills under the app root; fall back to source starters.
        auto dir = Paths::instance().root() / "skills";
        if (Paths::instance().root().empty()) {
            // Tests often set Paths after registration — seed from compile path.
#ifdef POLYMATH_SKILLS_DIR
            dir = std::filesystem::path(POLYMATH_SKILLS_DIR);
#else
            dir = std::filesystem::temp_directory_path() / "polymath_skills_default";
#endif
        }
        g_owned_skills->setSkillsDir(dir);
        g_owned_skills->seedStartersIfEmpty();
        g_owned_skills->load();
        // Optional hot-reload when a Qt event loop is running.
        g_owned_skills->installWatcher();
    }
    g_skills = g_owned_skills.get();
    return g_skills;
}

} // namespace

void setAgentSessionService(QObject* sessions) {
    std::lock_guard<std::mutex> lock(g_deps_mu);
    g_sessions = sessions;
}

QObject* agentSessionService() {
    std::lock_guard<std::mutex> lock(g_deps_mu);
    return g_sessions;
}

SkillRegistry* defaultSkillRegistry() {
    std::lock_guard<std::mutex> lock(g_deps_mu);
    return ensureSkills(nullptr);
}

void registerBuiltinTools(ToolRegistry& reg, BuiltinToolDeps deps) {
    SkillRegistry* skills = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_deps_mu);
        if (deps.sessions) g_sessions = deps.sessions;
        skills = ensureSkills(deps.skills);
    }

    // --- leaf tools with risk classes (03 §5) --------------------------------
    // read
    reg.add(std::make_shared<ShoppingListTool>(),       ToolRiskClass::Read);
    reg.add(std::make_shared<RecallTool>(),             ToolRiskClass::Read);
    reg.add(std::make_shared<SearchMemoryTool>(),       ToolRiskClass::Read);
    reg.add(std::make_shared<CameraSnapshotTool>(),     ToolRiskClass::Read);
    reg.add(std::make_shared<WhoIsHomeTool>(),          ToolRiskClass::Read);
    reg.add(std::make_shared<AgentStatusTool>(),        ToolRiskClass::Read);
    reg.add(std::make_shared<ListSchedulesTool>(),      ToolRiskClass::Read);

    // write_local
    reg.add(std::make_shared<ShoppingAddTool>(),        ToolRiskClass::WriteLocal);
    reg.add(std::make_shared<ShoppingRemoveTool>(),     ToolRiskClass::WriteLocal);
    reg.add(std::make_shared<DraftDocumentTool>(),      ToolRiskClass::WriteLocal);
    reg.add(std::make_shared<GenerateLabReportTool>(),  ToolRiskClass::WriteLocal);
    reg.add(std::make_shared<SetReminderTool>(),        ToolRiskClass::WriteLocal);
    reg.add(std::make_shared<RememberTool>(),           ToolRiskClass::WriteLocal);
    reg.add(std::make_shared<QueueDeepTaskTool>(),      ToolRiskClass::WriteLocal);
    reg.add(std::make_shared<UiControlTool>(),          ToolRiskClass::WriteLocal);
    // D1: schedule_task registers a *standing rule* whenever kind is every/rrule
    // (vs. a one-shot "at") — SafetyPolicy (A4, not yet landed) is the right
    // place to escalate that case to Confirm since it inspects argsJson per
    // Decision check(toolName, riskClass, argsJson); flagged in
    // docs/overhaul2/results/D1_config.md.
    reg.add(std::make_shared<ScheduleTaskTool>(),       ToolRiskClass::WriteLocal);
    reg.add(std::make_shared<CancelScheduleTool>(),     ToolRiskClass::WriteLocal);

    // external (network / CLI side effects — auto, logged + notice)
    reg.add(std::make_shared<WebSearchTool>(),          ToolRiskClass::External);
    reg.add(std::make_shared<FetchPageTool>(),          ToolRiskClass::External);
    reg.add(std::make_shared<YoutubeSearchTool>(),      ToolRiskClass::External);
    reg.add(std::make_shared<BrowserDriveTool>(),       ToolRiskClass::External);
    reg.add(std::make_shared<AgentSpawnTool>(),         ToolRiskClass::External);
    reg.add(std::make_shared<AgentSendTool>(),          ToolRiskClass::External);
    reg.add(std::make_shared<AgentStopTool>(),          ToolRiskClass::External);
    reg.add(std::make_shared<AgentWatchTool>(),         ToolRiskClass::External);

    // spend / destructive (require confirmation — harness parks waiting_user)
    reg.add(std::make_shared<PrintDocumentTool>(),     ToolRiskClass::Spend);
    reg.add(std::make_shared<PrintImageTool>(),         ToolRiskClass::Spend);

    // --- C2 system tools (fs / process / clipboard) ---------------------------
    // Path/command gates live in SafetyPolicy (AgentLoop); risk classes only.
    reg.add(std::make_shared<FsListTool>(),             ToolRiskClass::Read);
    reg.add(std::make_shared<FsReadTool>(),             ToolRiskClass::Read);
    reg.add(std::make_shared<ClipboardReadTool>(),      ToolRiskClass::Read);
    reg.add(std::make_shared<FsWriteTool>(),            ToolRiskClass::WriteLocal);
    reg.add(std::make_shared<ClipboardWriteTool>(),     ToolRiskClass::WriteLocal);
    // overwrite of existing files stays WriteLocal; C1/A4 Confirm is by risk
    // ceiling / mode, not a special mode=overwrite escalate (see C2_notes).
    reg.add(std::make_shared<FsMoveTool>(),             ToolRiskClass::Destructive);
    reg.add(std::make_shared<FsDeleteTool>(),           ToolRiskClass::Destructive);
    reg.add(std::make_shared<RunCommandTool>(),         ToolRiskClass::Destructive);
    reg.add(std::make_shared<AppLaunchTool>(),          ToolRiskClass::External);

    // C3: screen awareness (privacy-gated by privacy.screen_capture).
    reg.add(std::make_shared<ScreenCaptureTool>(),     ToolRiskClass::Read);
    reg.add(std::make_shared<ScreenDescribeTool>(),    ToolRiskClass::Read);

    // skills (write_local: expand/persist goals or author skill.json)
    if (skills) {
        reg.add(std::make_shared<RunSkillTool>(*skills),  ToolRiskClass::WriteLocal);
        reg.add(std::make_shared<SaveSkillTool>(*skills), ToolRiskClass::WriteLocal);
    } else {
        PM_WARN("registerBuiltinTools: no SkillRegistry — run_skill/save_skill skipped");
    }

    PM_INFO("registerBuiltinTools: {} tools registered", reg.names().size());
}

} // namespace polymath

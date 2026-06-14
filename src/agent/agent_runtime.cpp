#include "agent_runtime.h"
#include "turn_collector.h"
#include "persona.h"
#include "inference_manager.h"
#include "task_scheduler.h"
#include "database.h"
#include "activity_log.h"
#include "event_bus.h"
#include "paths.h"
#include "logging.h"
#include "grammar.h"

#include <QString>

#include <algorithm>
#include <sstream>

// AgentRuntime — the real tool-calling loop.
//
//   1. Load the ACTIVE personality (DB row + bundle persona.json): system prompt,
//      tool allow-list, sampling, voice, preferred model.
//   2. Assemble messages: system (persona + tool protocol + tool catalog) +
//      recent command history + the user turn.
//   3. Offer the allowed tools and constrain the model to emit exactly one
//      tool-call JSON object via a GBNF grammar (inference/grammar). A synthetic
//      `final_answer` tool is always offered so every constrained step yields a
//      parseable object and the loop has a deterministic exit.
//   4. Parse the tool call. If it is `final_answer`, finish. Otherwise dispatch
//      via the registry — inline, or, for ITool::isDeepTask() tools, enqueue on
//      the TaskScheduler and tell the user it is queued — then feed the result
//      back as a Role::Tool message and loop.
//   5. Produce the final answer with one *unconstrained* generation under the
//      turn's request_id (so it streams to the UI), and publish a SpeakRequest
//      with the persona voice for TTS.
//
// Constrained planning steps use an internal request_id suffix (":planN") so the
// intermediate tool-call JSON never reaches the chat UI (which keys on the turn
// request_id); only the final unconstrained answer streams under the real id.

namespace polymath {

namespace {

constexpr int    kMaxToolRounds   = 6;        // hard cap on tool-call iterations
constexpr int    kHistoryTurns    = 8;        // prior command turns to include
constexpr int    kStepTimeoutMs   = 120000;   // per-generation timeout
constexpr const char* kFinalAnswerTool = "final_answer";

// The synthetic "final_answer" tool's schema (not in the registry; only used to
// shape the grammar + catalog so the model can signal completion).
nlohmann::json finalAnswerSchema() {
    return {
        {"type", "object"},
        {"properties", {
            {"answer", {{"type", "string"},
                        {"description", "The complete answer to give the user"}}},
        }},
        {"required", {"answer"}},
    };
}

// Render the offered tools (registry specs + final_answer) as a compact catalog
// the model reads in the system prompt.
std::string renderCatalog(const nlohmann::json& specs) {
    std::ostringstream out;
    for (const auto& s : specs) {
        const auto& fn = s.value("function", nlohmann::json::object());
        out << "- " << fn.value("name", "") << ": " << fn.value("description", "") << "\n";
        const auto params = fn.value("parameters", nlohmann::json::object());
        const auto props = params.value("properties", nlohmann::json::object());
        if (!props.empty()) {
            out << "    args: ";
            bool first = true;
            for (auto it = props.begin(); it != props.end(); ++it) {
                if (!first) out << ", ";
                first = false;
                out << it.key();
                if (it.value().is_object() && it.value().contains("type"))
                    out << "(" << it.value()["type"].get<std::string>() << ")";
            }
            out << "\n";
        }
    }
    return out.str();
}

// Parse a constrained model reply into (tool, arguments). The grammar pins the
// shape to {"tool":...,"arguments":{...}}, but we stay defensive: scan for the
// first balanced JSON object if there is any stray text.
bool parseToolCall(const std::string& text, std::string& tool, nlohmann::json& args) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, /*allow_exceptions*/ false);
    if (j.is_discarded()) {
        const auto start = text.find('{');
        const auto end   = text.rfind('}');
        if (start == std::string::npos || end == std::string::npos || end <= start)
            return false;
        j = nlohmann::json::parse(text.substr(start, end - start + 1), nullptr, false);
        if (j.is_discarded()) return false;
    }
    if (!j.is_object() || !j.contains("tool")) return false;
    tool = j.value("tool", "");
    args = j.value("arguments", nlohmann::json::object());
    if (!args.is_object()) args = nlohmann::json::object();
    return !tool.empty();
}

} // namespace

AgentRuntime::AgentRuntime(Database& db, InferenceManager& inf, TaskScheduler& sched, QObject* parent)
    : QObject(parent), db_(db), inf_(inf), sched_(sched) {
    registerBuiltinTools(registry_);
}

void AgentRuntime::start() {
    // The collector must live on this service's thread (start() runs there).
    collector_ = std::make_unique<TurnCollector>();
    PM_INFO("AgentRuntime started: {} tools registered", registry_.names().size());
}

void AgentRuntime::stop() {
    collector_.reset();
}

// --- entry points -----------------------------------------------------------

void AgentRuntime::handleUtterance(const Utterance& u) {
    if (u.is_ambient) return;            // ambient text goes to memory, not the agent
    runTurn(u.text, QStringLiteral("voice"), /*from_voice*/ true);
}

void AgentRuntime::handleTextInput(const QString& text, const QString& request_id) {
    runTurn(text.toStdString(), request_id, /*from_voice*/ false);
}

// --- helpers ----------------------------------------------------------------

std::vector<ChatMessage> AgentRuntime::recentHistory(int max_turns,
                                                     const std::string& exclude_text) const {
    // Most-recent command turns, oldest-first. Speaker is unknown here; we treat
    // stored command transcripts as the running user/assistant dialogue context.
    // Pull one extra row so dropping the current utterance still yields max_turns.
    std::vector<ChatMessage> rev;
    bool dropped = false;
    db_.query("SELECT text FROM transcripts WHERE is_ambient=0 "
              "ORDER BY ts DESC LIMIT ?1",
              {max_turns + 1}, [&](const Row& r) {
                  std::string t = r.text(0);
                  if (!dropped && !exclude_text.empty() && t == exclude_text) {
                      dropped = true;            // skip the just-persisted current turn
                      return;
                  }
                  ChatMessage m;
                  m.role = Role::User;          // generic prior-context role
                  m.content = std::move(t);
                  rev.push_back(std::move(m));
              });
    if (static_cast<int>(rev.size()) > max_turns) rev.resize(max_turns);
    std::reverse(rev.begin(), rev.end());
    return rev;
}

void AgentRuntime::persistTranscript(const std::string& text, bool assistant) const {
    if (text.empty()) return;
    // speaker: -1 sentinel for the assistant, NULL for an unattributed user line.
    nlohmann::json speaker = assistant ? nlohmann::json(-1) : nlohmann::json(nullptr);
    db_.exec("INSERT INTO transcripts(text,speaker,is_ambient,ttl_at,ts) "
             "VALUES(?1,?2,0,0,?3)",
             {text, speaker, to_unix(Clock::now())});
}

namespace {
// Injected into the system prompt whenever the desktop-control tools are offered,
// so EVERY persona that can drive the computer does it under the same strict
// do / stop-conditions / avoid rules — not only the dedicated Operator persona.
const char* const kComputerUseProtocol =
R"(=== COMPUTER CONTROL PROTOCOL (you can operate this Windows PC) ===
You can see the screen and control the mouse and keyboard with look_at_screen,
computer_click, computer_type, computer_key and computer_scroll. Operate in a tight
observe-act-verify loop:
1. Call look_at_screen FIRST to see the current state before doing anything.
2. Decide the SINGLE next action and briefly tell the user what you are about to do.
3. Take exactly ONE action (one click / type / key / scroll). Prefer naming the
   element for computer_click (e.g. "the Save button") over raw coordinates.
4. Call look_at_screen again to CONFIRM the result before the next action.
5. Repeat until the task is done, then give a short summary of what you did.

STOP and ask the user (do NOT proceed) when: the task is complete; you are unsure, or
the same step fails twice; a login / password / 2FA / payment / credentials screen
appears; a Windows UAC, security, or permission dialog appears; or an action would be
destructive or hard to undo (deleting files, sending money, purchasing, sending or
posting messages publicly, uninstalling software, changing system or security settings).

NEVER: enter the user's passwords, 2FA codes, or financial details unless they
explicitly provided them for THIS task; make purchases, send emails/messages, or post to
social media without explicit confirmation; disable antivirus/firewall or change
security/privacy settings; delete user data; click ads or pop-ups; or act outside the
scope of what the user asked. When in doubt whether an action is safe or reversible,
STOP and ask first.
=== END COMPUTER CONTROL PROTOCOL ===)";
} // namespace

std::string AgentRuntime::buildSystemPrompt(const Persona& persona,
                                            const nlohmann::json& tool_specs) const {
    std::ostringstream sys;
    sys << persona.system_prompt << "\n\n";
    sys << "You can use tools to answer or act. On each step, respond with a SINGLE JSON "
           "object and nothing else, in the form:\n"
           "  {\"tool\": \"<tool_name>\", \"arguments\": { ... }}\n"
           "Call one tool at a time. After you have everything you need, call the "
           "\"" << kFinalAnswerTool << "\" tool with your complete reply in its "
           "\"answer\" argument. Do not invent tools or arguments outside the catalog.\n\n";
    // When the desktop-control tools are offered this turn, prepend the strict
    // operating protocol so the model always has its do/stop/avoid rules to hand.
    if (tool_specs.dump().find("computer_click") != std::string::npos)
        sys << kComputerUseProtocol << "\n\n";
    sys << "Available tools:\n" << renderCatalog(tool_specs);
    return sys.str();
}

// --- the loop ---------------------------------------------------------------

void AgentRuntime::runTurn(const std::string& user_text, const QString& request_id,
                           bool from_voice) {
    auto& bus = EventBus::instance();
    emit turnStarted(request_id);

    if (user_text.empty()) {
        emit turnFinished(request_id, QString());
        bus.publishToken({request_id, QString(), true});
        return;
    }
    if (!collector_) {                    // start() not yet run (defensive)
        PM_ERROR("AgentRuntime::runTurn: collector not ready");
        return;
    }

    // Serialize turns: tool dispatch spins a nested Qt event loop, so reject an
    // overlapping turn rather than let two interleave on this thread.
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        PM_WARN("agent: busy with another turn; ignoring '{}'", request_id.toStdString());
        bus.publishNotice({"warn", "agent", QStringLiteral("Still working on the previous request…")});
        return;
    }
    struct BusyGuard {
        std::atomic<bool>& b;
        ~BusyGuard() { b.store(false); }
    } guard{busy_};

    // 1) Active personality.
    const Persona persona = loadActivePersona(db_);

    // 2) Offered tools = persona allow-list (empty => all) + synthetic final_answer.
    nlohmann::json specs = registry_.specs(persona.tools);
    specs.push_back({
        {"type", "function"},
        {"function", {
            {"name", kFinalAnswerTool},
            {"description", "Provide the final answer to the user and end the turn."},
            {"parameters", finalAnswerSchema()},
        }},
    });

    // GBNF that constrains output to one of the offered tool-call objects.
    std::vector<grammar::ToolDef> defs;
    defs.reserve(specs.size());
    for (const auto& s : specs) {
        const auto& fn = s.value("function", nlohmann::json::object());
        defs.push_back({fn.value("name", ""),
                        fn.value("parameters", nlohmann::json::object())});
    }
    const std::string toolGrammar = grammar::buildToolCallGrammar(defs);

    // 3) Base message stack: system + history + user turn.
    std::vector<ChatMessage> messages;
    messages.push_back({Role::System, buildSystemPrompt(persona, specs)});
    for (auto& m : recentHistory(kHistoryTurns, user_text)) messages.push_back(std::move(m));
    messages.push_back({Role::User, user_text});

    // Persist the user turn for text-UI input (voice turns are stored by audio).
    if (!from_voice) persistTranscript(user_text, /*assistant*/ false);

    // 4) Tool loop.
    std::string finalAnswer;
    bool gotFinal = false;

    for (int round = 0; round < kMaxToolRounds && !gotFinal; ++round) {
        ChatRequest req;
        req.model_id   = (persona.preferred_model == "fast" ||
                          persona.preferred_model == "heavy")
                             ? std::string{}                 // role-based default
                             : persona.preferred_model;      // explicit registry id
        req.request_id = (request_id + QStringLiteral(":plan%1").arg(round)).toStdString();
        req.messages   = messages;
        req.sampling   = persona.sampling;
        req.sampling.grammar = toolGrammar;
        req.tool_names = persona.tools;

        bool ok = false;
        const std::string raw = collector_->run(inf_, req, kStepTimeoutMs, &ok);
        if (!ok) {
            PM_WARN("agent: tool-planning step {} timed out", round);
            break;
        }

        std::string tool;
        nlohmann::json args;
        if (!parseToolCall(raw, tool, args)) {
            PM_WARN("agent: unparseable tool call: {}", raw);
            // Treat a non-tool reply as the final answer text.
            finalAnswer = raw;
            gotFinal = true;
            break;
        }

        if (tool == kFinalAnswerTool) {
            finalAnswer = args.value("answer", "");
            gotFinal = true;
            break;
        }

        // Record the model's tool call in the running message stack.
        nlohmann::json callObj = nlohmann::json::object();
        callObj["tool"] = tool;
        callObj["arguments"] = args;
        messages.push_back({Role::Assistant, callObj.dump()});

        bus.publishToolCall({request_id, QString::fromStdString(tool),
                             QString::fromStdString(args.dump())});

        ITool* impl = registry_.get(tool);
        if (!impl) {
            PM_WARN("agent: model called unknown tool '{}'", tool);
            const std::string err = nlohmann::json({{"error", "unknown tool: " + tool}}).dump();
            messages.push_back({Role::Tool, err, tool});
            bus.publishToolResult({request_id, QString::fromStdString(tool),
                                   QString::fromStdString(err), false});
            continue;
        }

        ToolResult result;
        if (impl->isDeepTask()) {
            // Heavy/slow: queue it on the scheduler instead of running inline.
            const qint64 task_id = sched_.enqueue(tool, args, /*priority*/ 0);
            result.ok = true;
            result.content = {{"queued", true}, {"task_id", task_id}, {"tool", tool}};
            result.summary = "Queued " + tool + " as a background task (id " +
                             std::to_string(task_id) + ")";
            PM_INFO("agent: queued deep task '{}' as id {}", tool, task_id);
        } else {
            ToolContext tctx;
            tctx.inference          = &inf_;
            tctx.db                 = &db_;
            tctx.active_user_id     = -1;
            tctx.active_personality = persona.name;
            try {
                result = impl->invoke(args, tctx);
            } catch (const std::exception& e) {
                result.ok = false;
                result.content = {{"error", e.what()}};
                result.summary = std::string("tool threw: ") + e.what();
                PM_ERROR("agent: tool '{}' threw: {}", tool, e.what());
            }
        }

        const std::string resultJson = result.content.dump();
        messages.push_back({Role::Tool, resultJson, tool});
        bus.publishToolResult({request_id, QString::fromStdString(tool),
                               QString::fromStdString(resultJson), result.ok});
        if (!result.summary.empty()) {
            bus.publishNotice({result.ok ? "info" : "warn", "agent",
                               QString::fromStdString(result.summary)});
            // Durably record the action so the privacy/activity view can audit
            // "what did the assistant do on my behalf" after the fact.
            ActivityLog(db_).record(tool, result.summary, result.ok);
        }
    }

    // 5) Final answer. Prefer one unconstrained pass (streams to the UI under the
    // real request_id); fall back to the final_answer text if that yields nothing.
    streamFinalAnswer(messages, persona, request_id, finalAnswer);
}

// Issue the closing unconstrained generation (or replay `fallback`), stream it to
// the UI under `request_id`, persist + speak it, and emit turnFinished.
void AgentRuntime::streamFinalAnswer(std::vector<ChatMessage>& messages,
                                     const Persona& persona,
                                     const QString& request_id,
                                     const std::string& fallback) {
    auto& bus = EventBus::instance();

    // Nudge the model to now answer in natural language (no tool JSON).
    messages.push_back({Role::User,
        "Now write the final answer for the user in natural language. Do not output JSON "
        "or call any tool."});

    ChatRequest req;
    req.model_id   = (persona.preferred_model == "fast" || persona.preferred_model == "heavy")
                         ? std::string{} : persona.preferred_model;
    req.request_id = request_id.toStdString();      // streams under the real id
    req.messages   = std::move(messages);
    req.sampling   = persona.sampling;
    req.sampling.grammar.clear();                   // unconstrained natural text

    bool ok = false;
    std::string answer = collector_->run(inf_, req, kStepTimeoutMs, &ok);

    if (!ok || answer.empty()) {
        // The streamed pass produced nothing usable; replay the fallback text as
        // a single chunk so the UI/TTS still receive the answer.
        answer = fallback.empty() ? std::string("Sorry, I couldn't complete that.") : fallback;
        bus.publishToken({request_id, QString::fromStdString(answer), false});
        bus.publishToken({request_id, QString(), true});
    }

    persistTranscript(answer, /*assistant*/ true);

    // Hand off to TTS with the persona voice.
    bus.publishSpeak({QString::fromStdString(answer),
                      QString::fromStdString(persona.voice),
                      request_id});

    emit turnFinished(request_id, QString::fromStdString(answer));
    PM_INFO("agent: turn '{}' finished ({} chars)", request_id.toStdString(), answer.size());
}

} // namespace polymath

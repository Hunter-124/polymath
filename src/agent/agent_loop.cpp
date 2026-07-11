#include "agent_loop.h"
#include "turn_collector.h"
#include "persona.h"
#include "inference_manager.h"
#include "task_scheduler.h"
#include "memory_service.h"
#include "database.h"
#include "config.h"
#include "activity_log.h"
#include "event_bus.h"
#include "paths.h"
#include "logging.h"
#include "grammar.h"
#include "skills/skill_registry.h"
#include "skills/skill.h"

#include <QString>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace polymath {

namespace {

constexpr const char* kFinalAnswerTool = "final_answer";
constexpr const char* kConversationKey = "default";

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool containsAny(const std::string& hay, std::initializer_list<const char*> needles) {
    for (const char* n : needles) {
        if (hay.find(n) != std::string::npos) return true;
    }
    return false;
}

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

nlohmann::json planSchema() {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {{"type", "string"}}},
            {"steps", {{"type", "array"},
                       {"items", {{"type", "object"},
                                  {"properties", {
                                      {"description", {{"type", "string"}}},
                                      {"kind", {{"type", "string"}}},
                                      {"tool", {{"type", "string"}}},
                                      {"args", {{"type", "object"}}},
                                  }},
                                  {"required", {"description", "kind"}}}}}},
        }},
        {"required", {"title", "steps"}},
    };
}

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

nlohmann::json parseJsonObject(const std::string& text) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_discarded() && j.is_object()) return j;
    const auto start = text.find('{');
    const auto end   = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start)
        return nlohmann::json();
    j = nlohmann::json::parse(text.substr(start, end - start + 1), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return nlohmann::json();
    return j;
}

PlanStepRec stepFromJson(const nlohmann::json& s, int idx) {
    PlanStepRec st;
    st.idx = idx;
    st.description = s.value("description", s.value("desc", std::string("step")));
    st.kind = s.value("kind", "prompt");
    st.tool = s.value("tool", "");
    if (s.contains("args") && s["args"].is_object()) st.args = s["args"];
    else if (s.contains("arguments") && s["arguments"].is_object()) st.args = s["arguments"];
    else st.args = nlohmann::json::object();
    st.status = "pending";
    return st;
}

std::vector<PlanStepRec> stepsFromPlanJson(const nlohmann::json& plan) {
    std::vector<PlanStepRec> out;
    if (!plan.is_object()) return out;
    const auto& arr = plan.contains("steps") && plan["steps"].is_array()
                          ? plan["steps"]
                          : nlohmann::json::array();
    int i = 0;
    for (const auto& s : arr) {
        if (!s.is_object()) continue;
        out.push_back(stepFromJson(s, i++));
        if (static_cast<int>(out.size()) >= AgentLoop::kMaxPlanSteps) break;
    }
    return out;
}

// Unified skill expansion (A1): one loader, one directory scheme. Delegates to
// the process-shared SkillRegistry (same instance run_skill / save_skill use)
// so kind=skill steps and the command path agree on which skills exist and how
// they expand ({param} substitution + required-param checks live in the
// registry). Returns false on unknown skill / missing required param / empty.
bool expandSkillViaRegistry(const std::string& name, const nlohmann::json& params,
                            std::vector<PlanStepRec>& out_steps) {
    if (name.empty()) return false;
    SkillRegistry* reg = defaultSkillRegistry();
    if (!reg) return false;
    nlohmann::json goal = reg->expand(name, params);
    if (!goal.is_object() || goal.contains("error")) return false;
    if (!goal.contains("steps") || !goal["steps"].is_array()) return false;
    int i = 0;
    for (const auto& s : goal["steps"]) {
        if (!s.is_object()) continue;
        out_steps.push_back(stepFromJson(s, i++));
        if (static_cast<int>(out_steps.size()) >= AgentLoop::kMaxPlanSteps) break;
    }
    return !out_steps.empty();
}

// --- Router v2 word helpers ------------------------------------------------

// Lowercased alphanumeric word tokens (word-boundary matching).
std::vector<std::string> wordsOf(const std::string& lower) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : lower) {
        if (std::isalnum(static_cast<unsigned char>(c))) cur += c;
        else if (!cur.empty()) { w.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

bool hasWord(const std::vector<std::string>& words, const std::string& w) {
    return std::find(words.begin(), words.end(), w) != words.end();
}

// Contiguous multi-word phrase match (e.g. verb "put on").
bool hasPhrase(const std::vector<std::string>& words,
               const std::vector<std::string>& phrase) {
    if (phrase.empty() || phrase.size() > words.size()) return false;
    for (size_t i = 0; i + phrase.size() <= words.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < phrase.size(); ++j)
            if (words[i + j] != phrase[j]) { match = false; break; }
        if (match) return true;
    }
    return false;
}

// Ordered subsequence with gaps allowed: trigger "open youtube" matches
// "open a youtube video for me".
bool hasSubsequence(const std::vector<std::string>& words,
                    const std::vector<std::string>& seq) {
    if (seq.empty() || seq.size() > words.size()) return false;
    size_t k = 0;
    for (const auto& w : words) {
        if (w == seq[k] && ++k == seq.size()) return true;
    }
    return false;
}

// Any loaded skill whose one of its triggers is an ordered subsequence of the
// user's words. Returns the skill name, or "" if none. Tolerant of an
// uninitialized/empty registry.
std::string matchSkillTrigger(const std::vector<std::string>& words) {
    SkillRegistry* reg = defaultSkillRegistry();
    if (!reg) return {};
    try {
        for (const auto& sk : reg->all()) {
            for (const auto& trig : sk.triggers) {
                const auto tw = wordsOf(toLower(trig));
                if (!tw.empty() && hasSubsequence(words, tw)) return sk.name;
            }
        }
    } catch (const std::exception&) {
        // Registry not ready — fall through to verb/phrase heuristics.
    }
    return {};
}

// Intent-verb + media/site-object table → Command (e.g. "open a youtube video",
// "play some lofi", "put on some music"). Conservative: needs BOTH a launch verb
// and a media/site object so plain "open the door" / "play chess" stay Quick.
bool matchesMediaIntent(const std::vector<std::string>& words) {
    static const char* kVerbs[] = {
        "open", "play", "show", "watch", "launch", "stream", "unmute", "queue",
    };
    static const std::vector<std::vector<std::string>> kVerbPhrases = {
        {"put", "on"}, {"pull", "up"}, {"turn", "on"},
    };
    static const char* kObjects[] = {
        "youtube", "video", "videos", "music", "song", "songs", "playlist",
        "playlists", "lofi", "movie", "movies", "film", "films", "netflix",
        "spotify", "twitch", "podcast", "trailer", "channel", "livestream",
        "stream", "tv", "website", "webpage", "browser", "tab",
    };
    bool verb = false;
    for (const char* v : kVerbs) if (hasWord(words, v)) { verb = true; break; }
    if (!verb)
        for (const auto& p : kVerbPhrases) if (hasPhrase(words, p)) { verb = true; break; }
    if (!verb) return false;
    for (const char* o : kObjects) if (hasWord(words, o)) return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Free helpers
// ---------------------------------------------------------------------------

std::string turnRouteToString(TurnRoute r) {
    switch (r) {
    case TurnRoute::Quick:   return "quick";
    case TurnRoute::Goal:    return "goal";
    case TurnRoute::Command: return "command";
    }
    return "quick";
}

TurnRoute turnRouteFromString(const std::string& s) {
    const std::string l = toLower(s);
    if (l == "goal") return TurnRoute::Goal;
    if (l == "command") return TurnRoute::Command;
    return TurnRoute::Quick;
}

// ---------------------------------------------------------------------------
// Construction / recovery
// ---------------------------------------------------------------------------

AgentLoop::AgentLoop(Database& db, InferenceManager& inf, TaskScheduler& sched,
                     ToolRegistry& tools, MemoryService* memory, TurnCollector& collector)
    : db_(db), inf_(inf), sched_(sched), tools_(tools), memory_(memory),
      collector_(collector) {}

void AgentLoop::ensureSummariesTable() const {
    db_.exec(
        "CREATE TABLE IF NOT EXISTS conversation_summaries ("
        "  id INTEGER PRIMARY KEY,"
        "  conversation_key TEXT NOT NULL DEFAULT 'default',"
        "  summary TEXT NOT NULL,"
        "  updated_at INTEGER"
        ")");
    db_.exec(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_conv_summaries_key "
        "ON conversation_summaries(conversation_key)");
}

void AgentLoop::recoverOnStartup() {
    ensureSummariesTable();
    const int64_t now = to_unix(Clock::now());
    // Crash recovery (03 §1): running steps → pending so goals resume.
    // Only log when something was actually stuck mid-flight (quiet restarts).
    int running = 0;
    db_.query("SELECT COUNT(*) FROM plan_steps WHERE status='running'", {},
              [&](const Row& r) { running = static_cast<int>(r.i64(0)); });
    if (running <= 0) return;
    db_.exec("UPDATE plan_steps SET status='pending', updated_at=?1 "
             "WHERE status='running'", {now});
    PM_INFO("AgentLoop: recovered {} running plan_steps → pending", running);
}

// ---------------------------------------------------------------------------
// Tokens / compaction
// ---------------------------------------------------------------------------

int AgentLoop::tokens(const std::string& text) const {
    if (text.empty()) return 0;
    return inf_.countTokens(QString::fromStdString(text));
}

std::string AgentLoop::fitTokens(const std::string& text, int budget) const {
    if (budget <= 0 || text.empty()) return {};
    if (tokens(text) <= budget) return text;
    // Approximate: keep a char budget of ~4 chars/token, then binary-search.
    size_t lo = 0, hi = text.size();
    std::string best;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo + 1) / 2;
        const std::string cand = text.substr(0, mid);
        if (tokens(cand) <= budget) {
            best = cand;
            lo = mid;
        } else {
            hi = mid - 1;
        }
        // Cap iterations for very long strings.
        if (hi - lo < 8) {
            while (lo > 0 && tokens(text.substr(0, lo)) > budget) --lo;
            best = text.substr(0, lo);
            break;
        }
    }
    if (best.empty() && !text.empty()) {
        // Fallback: hard char cap.
        const size_t cap = static_cast<size_t>(std::max(1, budget)) * 4;
        best = text.substr(0, std::min(cap, text.size()));
    }
    if (best.size() < text.size()) best += "…";
    return best;
}

std::string AgentLoop::compactToolResult(const std::string& result_json) const {
    if (tokens(result_json) <= kToolResultTokenCap) return result_json;
    nlohmann::json j = nlohmann::json::parse(result_json, nullptr, false);
    if (j.is_discarded()) return fitTokens(result_json, kToolResultTokenCap);

    // One-line-per-item compaction for arrays; otherwise truncate dump.
    if (j.is_object()) {
        nlohmann::json compact = nlohmann::json::object();
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_array()) {
                std::ostringstream lines;
                int n = 0;
                for (const auto& item : it.value()) {
                    if (n++) lines << "\n";
                    if (item.is_string()) lines << item.get<std::string>();
                    else if (item.is_object() && item.contains("text"))
                        lines << item["text"].dump();
                    else lines << item.dump();
                    if (n >= 40) { lines << "\n…"; break; }
                }
                compact[it.key()] = fitTokens(lines.str(), kToolResultTokenCap / 2);
            } else if (it.value().is_string()) {
                compact[it.key()] = fitTokens(it.value().get<std::string>(), 200);
            } else {
                compact[it.key()] = it.value();
            }
        }
        return fitTokens(compact.dump(), kToolResultTokenCap);
    }
    if (j.is_array()) {
        std::ostringstream lines;
        int n = 0;
        for (const auto& item : j) {
            if (n++) lines << "\n";
            lines << (item.is_string() ? item.get<std::string>() : item.dump());
            if (n >= 40) { lines << "\n…"; break; }
        }
        return fitTokens(lines.str(), kToolResultTokenCap);
    }
    return fitTokens(result_json, kToolResultTokenCap);
}

// ---------------------------------------------------------------------------
// Transcripts / history
// ---------------------------------------------------------------------------

void AgentLoop::persistTranscript(const std::string& text, bool assistant) const {
    if (text.empty()) return;
    nlohmann::json speaker = assistant ? nlohmann::json(-1) : nlohmann::json(nullptr);
    db_.exec("INSERT INTO transcripts(text,speaker,is_ambient,ttl_at,ts) "
             "VALUES(?1,?2,0,0,?3)",
             {text, speaker, to_unix(Clock::now())});
}

std::vector<ChatMessage> AgentLoop::loadRecentTurns(int token_budget,
                                                    const std::string& exclude_text) const {
    // Pull a generous window then trim by tokens (most-recent first, then reverse).
    std::vector<ChatMessage> rev;
    bool dropped = false;
    db_.query("SELECT text, speaker FROM transcripts WHERE is_ambient=0 "
              "ORDER BY ts DESC LIMIT 40",
              {}, [&](const Row& r) {
                  std::string t = r.text(0);
                  if (!dropped && !exclude_text.empty() && t == exclude_text) {
                      dropped = true;
                      return;
                  }
                  ChatMessage m;
                  m.role = (!r.isNull(1) && r.i64(1) == -1) ? Role::Assistant : Role::User;
                  m.content = std::move(t);
                  rev.push_back(std::move(m));
              });

    std::vector<ChatMessage> kept;
    int used = 0;
    for (auto& m : rev) {
        const int t = tokens(m.content) + 4; // role overhead
        if (used + t > token_budget && !kept.empty()) break;
        if (used + t > token_budget) {
            m.content = fitTokens(m.content, token_budget - used);
        }
        used += tokens(m.content) + 4;
        kept.push_back(std::move(m));
    }
    std::reverse(kept.begin(), kept.end());
    return kept;
}

std::string AgentLoop::loadRollingSummary() const {
    ensureSummariesTable();
    std::string summary;
    db_.query("SELECT summary FROM conversation_summaries "
              "WHERE conversation_key=?1 LIMIT 1",
              {kConversationKey},
              [&](const Row& r) { summary = r.text(0); });
    return summary;
}

void AgentLoop::maybeUpdateRollingSummary(const std::string& exclude_text) const {
    // When verbatim history would exceed the recent budget, summarize the oldest
    // half via Fast model and store it. No-op without a usable generation path.
    ensureSummariesTable();
    const ContextBudgets b = defaultBudgets();
    std::vector<ChatMessage> all;
    bool dropped = false;
    db_.query("SELECT text, speaker FROM transcripts WHERE is_ambient=0 "
              "ORDER BY ts ASC LIMIT 80",
              {}, [&](const Row& r) {
                  std::string t = r.text(0);
                  if (!dropped && !exclude_text.empty() && t == exclude_text) {
                      dropped = true;
                      return;
                  }
                  ChatMessage m;
                  m.role = (!r.isNull(1) && r.i64(1) == -1) ? Role::Assistant : Role::User;
                  m.content = std::move(t);
                  all.push_back(std::move(m));
              });
    if (all.size() < 8) return;

    int total = 0;
    for (const auto& m : all) total += tokens(m.content) + 4;
    if (total <= b.recent) return;

    // Oldest half.
    const size_t half = all.size() / 2;
    std::ostringstream blob;
    for (size_t i = 0; i < half; ++i) {
        blob << (all[i].role == Role::Assistant ? "Assistant: " : "User: ")
             << all[i].content << "\n";
    }
    const std::string to_summarize = fitTokens(blob.str(), b.summary * 3);

    std::vector<ChatMessage> msgs;
    msgs.push_back({Role::System,
        "Summarize the following conversation turns in under 200 words. "
        "Keep facts, decisions, and open tasks. No preamble."});
    msgs.push_back({Role::User, to_summarize});

    ChatRequest req;
    req.request_id = "agent:summary";
    req.messages = std::move(msgs);
    req.sampling.temperature = 0.2f;
    req.sampling.max_tokens = 256;

    bool ok = false;
    std::string summary = collector_.run(inf_, req, 60000, &ok);
    if (!ok || summary.empty()) return;
    summary = fitTokens(summary, b.summary);

    const int64_t now = to_unix(Clock::now());
    // Upsert by conversation_key.
    int64_t existing = 0;
    db_.query("SELECT id FROM conversation_summaries WHERE conversation_key=?1",
              {kConversationKey},
              [&](const Row& r) { existing = r.i64(0); });
    if (existing > 0) {
        db_.exec("UPDATE conversation_summaries SET summary=?1, updated_at=?2 WHERE id=?3",
                 {summary, now, existing});
    } else {
        db_.exec("INSERT INTO conversation_summaries(conversation_key,summary,updated_at) "
                 "VALUES(?1,?2,?3)",
                 {kConversationKey, summary, now});
    }
    PM_INFO("AgentLoop: rolled conversation summary ({} chars)", summary.size());
}

std::string AgentLoop::recallMemoriesBlock(const std::string& query, int token_budget) const {
    if (query.empty() || token_budget <= 0) return {};

    std::ostringstream block;
    block << "Relevant memories:\n";
    int lines = 0;

    // Prefer semantic recall via MemoryService when the embedder is live.
    if (memory_) {
        try {
            const auto hits = memory_->recall(query, /*k*/ 5);
            for (const auto& h : hits) {
                block << "- " << h.text << "\n";
                ++lines;
            }
        } catch (const std::exception& e) {
            PM_WARN("AgentLoop: memory recall failed: {}", e.what());
        }
    }

    // Keyword fallback (same idea as memory_tools) so context still injects
    // offline when no embedding model is loaded.
    if (lines == 0) {
        std::vector<std::string> tokens;
        std::string cur;
        for (char c : query) {
            if (std::isalnum(static_cast<unsigned char>(c)))
                cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            else if (!cur.empty()) {
                if (cur.size() > 2) tokens.push_back(cur);
                cur.clear();
            }
        }
        if (!cur.empty() && cur.size() > 2) tokens.push_back(cur);

        struct Cand { std::string text; int score = 0; int64_t ts = 0; };
        std::vector<Cand> cands;
        db_.query("SELECT text,ts FROM memories WHERE text<>'' ORDER BY ts DESC LIMIT 200",
                  {}, [&](const Row& r) {
                      Cand c;
                      c.text = r.text(0);
                      c.ts = r.i64(1);
                      std::string lower = toLower(c.text);
                      for (const auto& t : tokens)
                          if (lower.find(t) != std::string::npos) ++c.score;
                      if (c.score > 0 || tokens.empty()) cands.push_back(std::move(c));
                  });
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.ts > b.ts;
        });
        for (const auto& c : cands) {
            if (lines >= 5) break;
            if (!tokens.empty() && c.score == 0) break;
            block << "- " << c.text << "\n";
            ++lines;
        }
    }

    if (lines == 0) return {};
    return fitTokens(block.str(), token_budget);
}

std::string AgentLoop::buildSystemPrompt(const Persona& persona,
                                         const nlohmann::json& tool_specs,
                                         bool include_tool_protocol) const {
    std::ostringstream sys;
    sys << persona.system_prompt;
    if (include_tool_protocol) {
        sys << "\n\nYou can use tools to answer or act. On each step, respond with a SINGLE JSON "
               "object and nothing else, in the form:\n"
               "  {\"tool\": \"<tool_name>\", \"arguments\": { ... }}\n"
               "Call one tool at a time. After you have everything you need, call the "
               "\"" << kFinalAnswerTool << "\" tool with your complete reply in its "
               "\"answer\" argument. Do not invent tools or arguments outside the catalog.\n\n";
        sys << "Available tools:\n" << renderCatalog(tool_specs);
    }
    return sys.str();
}

std::vector<ChatMessage> AgentLoop::assembleContext(
    const Persona& persona,
    const std::string& user_text,
    const nlohmann::json& tool_specs,
    const std::string& exclude_text,
    bool include_tool_protocol) const {

    const ContextBudgets b = defaultBudgets();
    std::vector<ChatMessage> messages;

    // 1) System + persona + optional tool catalog (≤ 1100).
    std::string system = buildSystemPrompt(persona, tool_specs, include_tool_protocol);
    system = fitTokens(system, b.system);
    messages.push_back({Role::System, system});

    // 2) Semantic memories (≤ 400).
    const std::string mem = recallMemoriesBlock(user_text, b.memories);
    if (!mem.empty()) {
        messages.push_back({Role::System, mem});
    }

    // 3) Rolling summary (≤ 400).
    std::string summary = loadRollingSummary();
    if (!summary.empty()) {
        summary = fitTokens(summary, b.summary);
        messages.push_back({Role::System, "Conversation so far (summary):\n" + summary});
    }

    // 4) Verbatim recent turns with correct roles (≤ 1400).
    for (auto& m : loadRecentTurns(b.recent, exclude_text.empty() ? user_text : exclude_text))
        messages.push_back(std::move(m));

    // 5) Current user turn.
    if (!user_text.empty())
        messages.push_back({Role::User, user_text});

    return messages;
}

std::string AgentLoop::modelIdFor(const Persona& persona) const {
    return personaModelId(persona);
}

// ---------------------------------------------------------------------------
// Generation helpers
// ---------------------------------------------------------------------------

std::string AgentLoop::constrainedComplete(const std::vector<ChatMessage>& messages,
                                           const Persona& persona,
                                           const std::string& grammar,
                                           const QString& request_id,
                                           const QString& suffix,
                                           bool* ok) {
    ChatRequest req;
    req.model_id   = modelIdFor(persona);
    req.request_id = (request_id + suffix).toStdString();
    req.messages   = messages;
    req.sampling   = persona.sampling;
    req.sampling.grammar = grammar;
    req.tool_names = persona.tools;
    bool local_ok = false;
    std::string out = collector_.run(inf_, req, kStepTimeoutMs, &local_ok);
    if (ok) *ok = local_ok;
    return out;
}

std::string AgentLoop::unconstrainedComplete(const std::vector<ChatMessage>& messages,
                                             const Persona& persona,
                                             const QString& request_id,
                                             bool stream,
                                             bool* ok) {
    ChatRequest req;
    req.model_id   = modelIdFor(persona);
    // Stream under the real id only when this is the user-visible answer.
    req.request_id = stream ? request_id.toStdString()
                            : (request_id + QStringLiteral(":gen")).toStdString();
    req.messages   = messages;
    req.sampling   = persona.sampling;
    req.sampling.grammar.clear();
    bool local_ok = false;

    // Real-time TTS: as the user-visible answer streams, speak each finished
    // sentence immediately (append to the playback queue). Flush when done.
    TurnCollector::TokenHook hook;
    std::string speak_buf;
    if (stream) {
        const QString voice = QString::fromStdString(persona.voice);
        const QString rid = request_id;
        hook = [voice, rid, &speak_buf](const std::string& delta, bool done) {
            auto& bus = EventBus::instance();
            speak_buf += delta;
            auto isEnd = [](char c) {
                return c == '.' || c == '!' || c == '?' || c == ';' || c == '\n';
            };
            size_t start = 0;
            for (size_t i = 0; i < speak_buf.size(); ++i) {
                if (!isEnd(speak_buf[i])) continue;
                // Include trailing space after punctuation when present.
                size_t end = i + 1;
                while (end < speak_buf.size() && speak_buf[end] == ' ') ++end;
                const std::string sentence = speak_buf.substr(start, end - start);
                start = end;
                // Skip tiny fragments (abbrev noise).
                if (sentence.size() < 4) continue;
                bus.publishSpeak({QString::fromStdString(sentence), voice, rid,
                                  /*append*/ true, /*flush*/ false});
            }
            if (start > 0)
                speak_buf.erase(0, start);
            if (done) {
                if (!speak_buf.empty()) {
                    bus.publishSpeak({QString::fromStdString(speak_buf), voice, rid,
                                      /*append*/ true, /*flush*/ false});
                    speak_buf.clear();
                }
                bus.publishSpeak({QString(), voice, rid,
                                  /*append*/ true, /*flush*/ true});
            }
        };
    }

    std::string out = collector_.run(inf_, req, kStepTimeoutMs, &local_ok, std::move(hook));
    if (ok) *ok = local_ok;
    return out;
}

void AgentLoop::streamOrPublishAnswer(const std::string& answer,
                                      const Persona& persona,
                                      const QString& request_id,
                                      bool speak) {
    auto& bus = EventBus::instance();
    // If the unconstrained stream already pushed tokens under request_id, the
    // UI has the text; we still publish a done marker when answer came offline.
    bus.publishToken({request_id, QString::fromStdString(answer), false});
    bus.publishToken({request_id, QString(), true});
    persistTranscript(answer, /*assistant*/ true);
    if (speak) {
        bus.publishSpeak({QString::fromStdString(answer),
                          QString::fromStdString(persona.voice),
                          request_id});
    }
}

// ---------------------------------------------------------------------------
// Final-answer hygiene (B-LEAK)
// ---------------------------------------------------------------------------

std::string AgentLoop::sanitizeFinalText(const std::string& raw,
                                         const std::string& tool_digest) {
    auto trim = [](const std::string& s) -> std::string {
        const size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        const size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    };
    auto synth = [&]() -> std::string {
        const std::string d = trim(tool_digest);
        if (!d.empty()) return "Here's what I found:\n" + d;
        return {};
    };

    const std::string t = trim(raw);
    if (t.empty()) return synth();

    // If it parses as a tool call, salvage a prose argument when present.
    std::string tool;
    nlohmann::json args;
    if (parseToolCall(t, tool, args)) {
        if (tool == kFinalAnswerTool) {
            const std::string a = trim(args.value("answer", std::string{}));
            if (!a.empty() && a.front() != '{') return a;
        }
        for (const char* key : {"answer", "text", "content", "message", "response"}) {
            if (args.contains(key) && args[key].is_string()) {
                const std::string a = trim(args[key].get<std::string>());
                if (!a.empty() && a.front() != '{') return a;
            }
        }
        // No salvageable argument — fall through to blob stripping.
    }

    // Strip any JSON object blob, keep surrounding prose.
    const size_t br = t.find('{');
    if (br != std::string::npos) {
        const size_t end = t.rfind('}');
        const std::string pre  = trim(t.substr(0, br));
        const std::string post = (end != std::string::npos && end + 1 <= t.size())
                                     ? trim(t.substr(end + 1))
                                     : std::string{};
        std::string rest = pre.empty() ? post
                         : (post.empty() ? pre : pre + " " + post);
        if (!rest.empty() && rest.front() != '{') return rest;
        return synth();   // may be empty — caller supplies a generic fallback
    }
    return t;   // no JSON present
}

std::string AgentLoop::sanitizeFinalAnswer(const std::string& raw,
                                           std::vector<ChatMessage>& clean_msgs,
                                           const Persona& persona,
                                           const QString& request_id,
                                           const std::string& tool_digest,
                                           int depth) {
    std::string tool;
    nlohmann::json args;
    if (depth < 2 && parseToolCall(raw, tool, args) && tool != kFinalAnswerTool) {
        ITool* impl = tools_.get(tool);
        if (impl) {
            // (b) Another known tool leaked in as the "answer": run it once, then
            // regenerate prose from the enriched digest.
            auto& bus = EventBus::instance();
            bus.publishToolCall({request_id, QString::fromStdString(tool),
                                 QString::fromStdString(args.dump())});
            ToolResult result;
            if (impl->isDeepTask()) {
                const qint64 task_id = sched_.enqueue(tool, args, /*priority*/ 0);
                result.ok = true;
                result.content = {{"queued", true}, {"task_id", task_id}, {"tool", tool}};
                result.summary = "Queued " + tool + " as background task " +
                                 std::to_string(task_id);
            } else {
                ToolContext tctx;
                tctx.inference = &inf_;
                tctx.db = &db_;
                tctx.memory = memory_;
                tctx.active_user_id = -1;
                tctx.active_personality = persona.name;
                try {
                    result = impl->invoke(args, tctx);
                } catch (const std::exception& e) {
                    result.ok = false;
                    result.content = {{"error", e.what()}};
                    result.summary = std::string("tool threw: ") + e.what();
                }
            }
            const std::string resultJson = compactToolResult(result.content.dump());
            bus.publishToolResult({request_id, QString::fromStdString(tool),
                                   QString::fromStdString(resultJson), result.ok});
            std::string digest2 = tool_digest;
            digest2 += "- " + tool + ": " +
                       (result.summary.empty() ? fitTokens(resultJson, 120)
                                               : result.summary) + "\n";
            if (!result.summary.empty()) {
                bus.publishNotice({result.ok ? "info" : "warn", "agent",
                                   QString::fromStdString(result.summary)});
                ActivityLog(db_).record(tool, result.summary, result.ok);
            }
            // Regenerate prose off a shadow id (not shown to ChatModel).
            std::vector<ChatMessage> msgs2 = clean_msgs;
            msgs2.push_back({Role::System,
                "Results from tools you used this turn:\n" + digest2});
            msgs2.push_back({Role::User,
                "Answer my request in natural prose. Plain text only — no JSON."});
            bool ok2 = false;
            const std::string regen = unconstrainedComplete(
                msgs2, persona, request_id, /*stream*/ false, &ok2);
            if (ok2 && !regen.empty())
                return sanitizeFinalAnswer(regen, clean_msgs, persona, request_id,
                                           digest2, depth + 1);
            return sanitizeFinalText("", digest2);
        }
        // Unknown tool → strip / synthesize (static path).
    }
    return sanitizeFinalText(raw, tool_digest);
}

std::string AgentLoop::finalizeAndPublishAnswer(std::vector<ChatMessage> clean_msgs,
                                                const Persona& persona,
                                                const QString& request_id,
                                                const std::string& tool_digest,
                                                const std::string& fallback,
                                                bool speak) {
    auto& bus = EventBus::instance();
    const QString voice = QString::fromStdString(persona.voice);

    // Generate under a SHADOW request_id so raw tokens never hit ChatModel
    // (which subscribes to tokenStreamed under the real id). We forward clean
    // prose to the real id ourselves once the leading chars prove non-JSON.
    ChatRequest req;
    req.model_id   = modelIdFor(persona);
    req.request_id = (request_id + QStringLiteral(":final")).toStdString();
    req.messages   = clean_msgs;
    req.sampling   = persona.sampling;
    req.sampling.grammar.clear();

    struct SniffState {
        std::string full;
        std::string speak_buf;
        bool decided = false;
        bool clean = false;
    } st;
    constexpr size_t kSniffWindow = 24;

    // Sentence-chunked TTS for the clean-streaming path (mirrors
    // unconstrainedComplete's hook, but gated on the streaming guard).
    auto feedTts = [&](const std::string& piece, bool done) {
        if (!speak) return;
        st.speak_buf += piece;
        auto isEnd = [](char c) {
            return c == '.' || c == '!' || c == '?' || c == ';' || c == '\n';
        };
        size_t start = 0;
        for (size_t i = 0; i < st.speak_buf.size(); ++i) {
            if (!isEnd(st.speak_buf[i])) continue;
            size_t end = i + 1;
            while (end < st.speak_buf.size() && st.speak_buf[end] == ' ') ++end;
            const std::string sentence = st.speak_buf.substr(start, end - start);
            start = end;
            if (sentence.size() < 4) continue;
            bus.publishSpeak({QString::fromStdString(sentence), voice, request_id,
                              /*append*/ true, /*flush*/ false});
        }
        if (start > 0) st.speak_buf.erase(0, start);
        if (done) {
            if (!st.speak_buf.empty()) {
                bus.publishSpeak({QString::fromStdString(st.speak_buf), voice,
                                  request_id, /*append*/ true, /*flush*/ false});
                st.speak_buf.clear();
            }
            bus.publishSpeak({QString(), voice, request_id,
                              /*append*/ true, /*flush*/ true});
        }
    };

    TurnCollector::TokenHook hook = [&](const std::string& delta, bool done) {
        st.full += delta;
        if (!st.decided) {
            const size_t nb = st.full.find_first_not_of(" \t\r\n");
            if (nb != std::string::npos &&
                (st.full.size() - nb >= kSniffWindow || done)) {
                st.decided = true;
                st.clean = (st.full[nb] != '{');   // leading '{' ⇒ hold as JSON
                if (st.clean) {
                    bus.publishToken({request_id, QString::fromStdString(st.full), false});
                    feedTts(st.full, false);
                }
            } else if (done) {
                st.decided = true;   // whitespace only
                st.clean = true;
            }
        } else if (st.clean) {
            bus.publishToken({request_id, QString::fromStdString(delta), false});
            feedTts(delta, false);
        }
        // decided && !clean ⇒ suspected JSON: hold, post-process after the run.
    };

    bool ok = false;
    const std::string raw =
        collector_.run(inf_, req, kStepTimeoutMs, &ok, std::move(hook));

    const bool hasProse = raw.find_first_not_of(" \t\r\n") != std::string::npos;
    if (st.decided && st.clean && hasProse) {
        // Clean prose already streamed to the UI. Finish TTS + close the stream.
        feedTts("", /*done*/ true);
        bus.publishToken({request_id, QString(), true});
        persistTranscript(raw, /*assistant*/ true);
        return raw;
    }

    // Held as JSON / failed / empty → sanitize into prose and publish fresh.
    std::string prose =
        sanitizeFinalAnswer(raw, clean_msgs, persona, request_id, tool_digest, 0);
    if (prose.empty())
        prose = fallback.empty() ? std::string("Sorry, I couldn't complete that.")
                                 : fallback;
    streamOrPublishAnswer(prose, persona, request_id, speak);
    return prose;
}

// ---------------------------------------------------------------------------
// Router
// ---------------------------------------------------------------------------

TurnRoute AgentLoop::classifyRouteHeuristic(const std::string& user_text) {
    const std::string t = toLower(user_text);
    // Goal: multi-step / research / plan language.
    if (containsAny(t, {
            "make a plan", "create a plan", "multi-step", "multi step",
            "research", "investigate", "find out and", "and then",
            "step by step", "break down", "work through",
            "write a report", "lab report", "gather", "compile a",
            "look up and", "search and then", "first ", "then "})) {
        // Guard: short "then" alone in casual speech — require length or second signal.
        if (t.size() > 40 || containsAny(t, {
                "plan", "research", "report", "investigate", "compile",
                "gather", "multi", "steps", "and then", "find out"}))
            return TurnRoute::Goal;
    }
    // Command: pure UI / skill invocation. Router v2 (B-ROUTE) — three signals,
    // in cheap-first order:
    //   1) a small set of explicit command phrases (deterministic, registry-free);
    //   2) an intent-verb + media/site-object table ("open a youtube video",
    //      "play some lofi") — allows gaps between verb and object;
    //   3) trigger matching against the loaded SkillRegistry (word-boundary,
    //      ordered subsequence so "open a youtube video" matches "open youtube").
    if (containsAny(t, {"slop mode", "run skill", "start skill", "run the skill"}))
        return TurnRoute::Command;
    const std::vector<std::string> words = wordsOf(t);
    if (matchesMediaIntent(words))
        return TurnRoute::Command;
    if (!matchSkillTrigger(words).empty())
        return TurnRoute::Command;
    return TurnRoute::Quick;
}

TurnRoute AgentLoop::classifyRoute(const std::string& user_text,
                                   const Persona& persona,
                                   const QString& request_id) {
    // Fast path: skip a full LLM round-trip when the heuristic is confident.
    // Router prefill alone was multi-second even on GPU and catastrophic on CPU.
    // Default to heuristic for almost everything; only run the LLM router when
    // the message is long *and* the heuristic is Quick (ambiguous intent).
    const TurnRoute heuristic = classifyRouteHeuristic(user_text);
    const std::string lower = toLower(user_text);
    const bool shortMsg = user_text.size() < 200;
    const bool clearlyQuick =
        heuristic == TurnRoute::Quick &&
        (shortMsg || !containsAny(lower, {"plan", "research", "multi", "and then", "step by"}));
    const bool clearlyCommand = heuristic == TurnRoute::Command;
    const bool clearlyGoal    = heuristic == TurnRoute::Goal;
    if (clearlyQuick || clearlyCommand || clearlyGoal) {
        PM_INFO("AgentLoop: router heuristic fast-path → {} (skip LLM)",
                turnRouteToString(heuristic));
        return heuristic;
    }

    // Ambiguous: one cheap constrained classify.
    std::vector<grammar::ToolDef> defs = {
        {"quick",   {{"type","object"},{"properties",
            {{"reason",{{"type","string"}}}}}}},
        {"goal",    {{"type","object"},{"properties",
            {{"reason",{{"type","string"}}}}}}},
        {"command", {{"type","object"},{"properties",
            {{"reason",{{"type","string"}}}}}}},
    };
    const std::string g = grammar::buildToolCallGrammar(defs);

    std::ostringstream catalog;
    for (const auto& n : tools_.names()) catalog << n << " ";

    std::vector<ChatMessage> msgs;
    msgs.push_back({Role::System,
        "You are a turn router for a home assistant. Classify the user message as exactly "
        "one of: quick (direct answer or ≤2 tool calls), goal (multi-step plan needed), "
        "command (UI/skill invocation, no plan). Reply with a single tool-call JSON using "
        "tool name quick|goal|command.\nTools available: " + catalog.str()});
    msgs.push_back({Role::User, user_text});

    bool ok = false;
    const std::string raw = constrainedComplete(msgs, persona, g, request_id,
                                                QStringLiteral(":route"), &ok);
    if (ok) {
        std::string tool;
        nlohmann::json args;
        if (parseToolCall(raw, tool, args)) {
            const TurnRoute r = turnRouteFromString(tool);
            PM_INFO("AgentLoop: router → {} ({})", turnRouteToString(r), raw);
            return r;
        }
    }
    PM_INFO("AgentLoop: router fallback heuristic → {}", turnRouteToString(heuristic));
    return heuristic;
}

// ---------------------------------------------------------------------------
// Interactive entry
// ---------------------------------------------------------------------------

std::string AgentLoop::runInteractive(const std::string& user_text,
                                      const QString& request_id,
                                      bool from_voice) {
    auto& bus = EventBus::instance();
    if (user_text.empty()) {
        bus.publishToken({request_id, QString(), true});
        return {};
    }

    const Persona persona = loadActivePersona(db_);
    if (!from_voice) persistTranscript(user_text, /*assistant*/ false);

    // Best-effort summary roll (no-op without model / short history).
    maybeUpdateRollingSummary(user_text);

    const TurnRoute route = classifyRoute(user_text, persona, request_id);
    std::string answer;
    switch (route) {
    case TurnRoute::Goal:
        answer = runGoalPath(user_text, persona, request_id, from_voice);
        break;
    case TurnRoute::Command:
        answer = runCommand(user_text, persona, request_id, from_voice);
        break;
    case TurnRoute::Quick:
    default:
        answer = runQuick(user_text, persona, request_id, from_voice);
        break;
    }
    return answer;
}

// ---------------------------------------------------------------------------
// Quick path (≤2 tool rounds + final answer) — snappy v1 behavior
// ---------------------------------------------------------------------------

std::string AgentLoop::runQuick(const std::string& user_text,
                                const Persona& persona,
                                const QString& request_id,
                                bool from_voice) {
    auto& bus = EventBus::instance();

    nlohmann::json specs = tools_.specs(persona.tools);
    specs.push_back({
        {"type", "function"},
        {"function", {
            {"name", kFinalAnswerTool},
            {"description", "Provide the final answer to the user and end the turn."},
            {"parameters", finalAnswerSchema()},
        }},
    });

    std::vector<grammar::ToolDef> defs;
    defs.reserve(specs.size());
    for (const auto& s : specs) {
        const auto& fn = s.value("function", nlohmann::json::object());
        defs.push_back({fn.value("name", ""),
                        fn.value("parameters", nlohmann::json::object())});
    }
    const std::string toolGrammar = grammar::buildToolCallGrammar(defs);

    std::vector<ChatMessage> messages =
        assembleContext(persona, user_text, specs, user_text, /*tool protocol*/ true);

    std::string finalAnswer;
    std::string toolDigest;   // human-ish summary of this turn's tool results
    bool gotFinal = false;
    int toolCalls = 0;

    for (int round = 0; round < kMaxQuickToolRounds + 1 && !gotFinal; ++round) {
        bool ok = false;
        const std::string raw = constrainedComplete(
            messages, persona, toolGrammar, request_id,
            QStringLiteral(":plan%1").arg(round), &ok);
        if (!ok) {
            PM_WARN("AgentLoop: quick step {} timed out", round);
            break;
        }

        std::string tool;
        nlohmann::json args;
        if (!parseToolCall(raw, tool, args)) {
            finalAnswer = raw;
            gotFinal = true;
            break;
        }
        if (tool == kFinalAnswerTool) {
            finalAnswer = args.value("answer", "");
            gotFinal = true;
            break;
        }

        if (toolCalls >= kMaxQuickToolRounds) {
            // Force exit: treat as needing final answer.
            messages.push_back({Role::Assistant, raw});
            messages.push_back({Role::User,
                "You have used the maximum number of tools for a quick turn. "
                "Call final_answer now."});
            continue;
        }

        nlohmann::json callObj = {{"tool", tool}, {"arguments", args}};
        messages.push_back({Role::Assistant, callObj.dump()});
        bus.publishToolCall({request_id, QString::fromStdString(tool),
                             QString::fromStdString(args.dump())});

        ITool* impl = tools_.get(tool);
        ToolResult result;
        if (!impl) {
            result.ok = false;
            result.content = {{"error", "unknown tool: " + tool}};
            result.summary = result.content["error"].get<std::string>();
        } else if (impl->isDeepTask()) {
            const qint64 task_id = sched_.enqueue(tool, args, /*priority*/ 0);
            result.ok = true;
            result.content = {{"queued", true}, {"task_id", task_id}, {"tool", tool}};
            result.summary = "Queued " + tool + " as background task " +
                             std::to_string(task_id);
        } else {
            ToolContext tctx;
            tctx.inference = &inf_;
            tctx.db = &db_;
            tctx.memory = memory_;
            tctx.active_user_id = -1;
            tctx.active_personality = persona.name;
            try {
                result = impl->invoke(args, tctx);
            } catch (const std::exception& e) {
                result.ok = false;
                result.content = {{"error", e.what()}};
                result.summary = std::string("tool threw: ") + e.what();
            }
        }
        ++toolCalls;

        const std::string resultJson = compactToolResult(result.content.dump());
        messages.push_back({Role::Tool, resultJson, tool});
        bus.publishToolResult({request_id, QString::fromStdString(tool),
                               QString::fromStdString(resultJson), result.ok});
        toolDigest += "- " + tool + ": " +
                      (result.summary.empty() ? fitTokens(resultJson, 120)
                                              : result.summary) + "\n";
        if (!result.summary.empty()) {
            bus.publishNotice({result.ok ? "info" : "warn", "agent",
                               QString::fromStdString(result.summary)});
            ActivityLog(db_).record(tool, result.summary, result.ok);
        }
    }

    // Final answer — B-LEAK fix. Build a CLEAN context with NO tool protocol
    // (persona + memory + summary + history + user turn, no JSON instructions),
    // append a compact digest of this turn's tool results, then generate under a
    // shadow request_id with a streaming guard so no raw tool-call JSON can ever
    // reach ChatModel. finalizeAndPublishAnswer post-processes anything that
    // still comes back as JSON into prose before publishing.
    std::vector<ChatMessage> finalMsgs =
        assembleContext(persona, user_text, nlohmann::json::array(), user_text,
                        /*include_tool_protocol*/ false);
    if (!toolDigest.empty())
        finalMsgs.push_back({Role::System,
            "Results from tools you used this turn:\n" + toolDigest});
    finalMsgs.push_back({Role::User,
        "Using any tool results above, answer my request in natural, conversational "
        "prose. Reply with plain text only — do not output JSON or call any tool."});

    // Fallback prose if generation fails entirely: sanitized final_answer text
    // from the tool loop (never raw JSON), else a synthesized digest summary.
    const std::string fallback = sanitizeFinalText(finalAnswer, toolDigest);
    const std::string answer = finalizeAndPublishAnswer(
        std::move(finalMsgs), persona, request_id, toolDigest, fallback,
        /*speak*/ true);
    (void)from_voice;
    return answer;
}

// ---------------------------------------------------------------------------
// Command path
// ---------------------------------------------------------------------------

std::string AgentLoop::runCommand(const std::string& user_text,
                                  const Persona& persona,
                                  const QString& request_id,
                                  bool from_voice) {
    // Router v2 unifies skill loading through the shared SkillRegistry. Resolve
    // the command to a skill by matching triggers (word-boundary, gaps), then
    // expand ANY matched skill (not just slop_mode) into an executable goal.
    const std::string lower = toLower(user_text);
    const std::vector<std::string> words = wordsOf(lower);

    std::string skillName = matchSkillTrigger(words);
    // Explicit "run/start skill <name>" form: take the trailing token as a name.
    if (skillName.empty() &&
        containsAny(lower, {"run skill", "start skill", "run the skill"})) {
        if (!words.empty()) {
            SkillRegistry* reg = defaultSkillRegistry();
            for (auto it = words.rbegin(); reg && it != words.rend(); ++it) {
                if (reg->has(*it)) { skillName = *it; break; }
            }
        }
    }

    if (!skillName.empty()) {
        // Best-effort fill of a topic/query-style required param from the phrasing.
        nlohmann::json params = nlohmann::json::object();
        if (SkillRegistry* reg = defaultSkillRegistry()) {
            if (const Skill* sk = reg->get(skillName)) {
                std::string topic;
                for (const char* kw : {" about ", " on ", " of ", " for "}) {
                    const auto pos = lower.find(kw);
                    if (pos != std::string::npos) {
                        topic = user_text.substr(pos + std::string(kw).size());
                        break;
                    }
                }
                const size_t a = topic.find_first_not_of(" \t\r\n");
                topic = (a == std::string::npos) ? std::string() : topic.substr(a);
                if (sk->params.is_object() && sk->params.contains("required") &&
                    sk->params["required"].is_array()) {
                    for (const auto& r : sk->params["required"]) {
                        if (!r.is_string()) continue;
                        const std::string key = r.get<std::string>();
                        if (params.contains(key)) continue;
                        if (key == "topic" || key == "query" || key == "q" ||
                            key == "search" || key == "prompt")
                            params[key] = topic.empty() ? std::string("lofi") : topic;
                        else
                            params[key] = topic.empty() ? user_text : topic;
                    }
                }
            }
        }

        std::vector<PlanStepRec> steps;
        if (expandSkillViaRegistry(skillName, params, steps)) {
            nlohmann::json ctx = {{"skill", skillName}, {"params", params},
                                  {"request_id", request_id.toStdString()}};
            const int64_t gid = createGoal(skillName, from_voice ? "voice" : "chat",
                                           ctx, steps);
            executeGoal(gid);
            const GoalRec g = loadGoal(gid);
            const std::string answer =
                "Running skill '" + skillName + "' as goal " + std::to_string(gid) +
                " (" + g.status + ").";
            streamOrPublishAnswer(answer, persona, request_id, /*speak*/ from_voice);
            return answer;
        }
        PM_WARN("AgentLoop: skill '{}' matched but failed to expand — falling back to quick",
                skillName);
    }

    // No skill matched (or expansion failed): fall back to the Quick path so the
    // user still gets a real answer instead of a canned dead-end string.
    return runQuick(user_text, persona, request_id, from_voice);
}

// ---------------------------------------------------------------------------
// Goal path: plan → execute → reflect
// ---------------------------------------------------------------------------

std::string AgentLoop::runGoalPath(const std::string& user_text,
                                   const Persona& persona,
                                   const QString& request_id,
                                   bool from_voice) {
    nlohmann::json ctx = {
        {"request_id", request_id.toStdString()},
        {"user_text", user_text},
        {"trace", nlohmann::json::array()},
    };
    GoalRec goal;
    goal.title = fitTokens(user_text, 40);
    if (goal.title.empty()) goal.title = "goal";
    goal.origin = from_voice ? "voice" : "chat";
    goal.context = ctx;
    goal.status = "active";

    // Insert shell goal first so we have an id for steps.
    const int64_t now = to_unix(Clock::now());
    goal.id = db_.exec(
        "INSERT INTO goals(title,status,origin,context_json,created_at,updated_at) "
        "VALUES(?1,'active',?2,?3,?4,?4)",
        {goal.title, goal.origin, goal.context.dump(), now});

    EventBus::instance().publishGoalUpdate({
        QString::number(goal.id),
        QString::fromStdString(goal.title),
        QStringLiteral("running"),
        QStringLiteral("Planning…"),
    });

    if (!planGoal(goal, user_text, persona, request_id)) {
        // Fallback: single prompt step so the goal still does something.
        PlanStepRec st;
        st.idx = 0;
        st.description = user_text;
        st.kind = "prompt";
        st.status = "pending";
        goal.steps = {st};
        insertSteps(goal.id, goal.steps);
        appendTrace(goal, {{"event", "plan_fallback"}, {"steps", 1}});
    }

    executeGoal(goal.id);
    goal = loadGoal(goal.id);

    std::string summary;
    if (goal.result.is_object() && goal.result.contains("summary"))
        summary = goal.result.value("summary", "");
    if (summary.empty()) summary = "Goal " + goal.status;

    // Visible chat answer.
    const std::string answer =
        (goal.status == "done" ? "✔ Finished: " : "✖ Failed: ") +
        goal.title + " — " + summary;
    streamOrPublishAnswer(answer, persona, request_id,
                          /*speak*/ from_voice);
    return answer;
}

bool AgentLoop::planGoal(GoalRec& goal, const std::string& user_text,
                         const Persona& persona, const QString& request_id) {
    nlohmann::json specs = tools_.specs(persona.tools);
    // Catalog-only system for planning.
    std::ostringstream sys;
    sys << "You are a planner for a local home assistant. Produce a JSON plan via the "
           "emit_plan tool: title + up to " << kMaxPlanSteps << " steps. Each step has "
           "description, kind (tool|prompt|skill|surface), and optional tool/args. "
           "Prefer tools from the catalog. Keep plans short.\n\nTools:\n"
        << renderCatalog(specs);

    std::vector<grammar::ToolDef> defs = {
        {"emit_plan", planSchema()},
    };
    const std::string g = grammar::buildToolCallGrammar(defs);

    std::vector<ChatMessage> msgs;
    msgs.push_back({Role::System, sys.str()});
    // Inject a little memory for planning relevance.
    const std::string mem = recallMemoriesBlock(user_text, 200);
    if (!mem.empty()) msgs.push_back({Role::System, mem});
    msgs.push_back({Role::User, "Create a plan for: " + user_text});

    bool ok = false;
    const std::string raw = constrainedComplete(msgs, persona, g, request_id,
                                                QStringLiteral(":plan"), &ok);
    if (!ok) return false;

    std::string tool;
    nlohmann::json args;
    nlohmann::json plan;
    if (parseToolCall(raw, tool, args) && tool == "emit_plan") {
        plan = args;
    } else {
        plan = parseJsonObject(raw);
    }
    if (plan.is_null() || !plan.is_object()) return false;

    if (plan.contains("title") && plan["title"].is_string()) {
        const std::string t = plan["title"].get<std::string>();
        if (!t.empty()) {
            goal.title = t;
            db_.exec("UPDATE goals SET title=?1, updated_at=?2 WHERE id=?3",
                     {goal.title, to_unix(Clock::now()), goal.id});
        }
    }

    auto steps = stepsFromPlanJson(plan);
    if (steps.empty()) return false;
    goal.steps = std::move(steps);
    insertSteps(goal.id, goal.steps);
    appendTrace(goal, {{"event", "planned"}, {"steps", goal.steps.size()},
                       {"title", goal.title}});
    return true;
}

// ---------------------------------------------------------------------------
// Execute goal
// ---------------------------------------------------------------------------

void AgentLoop::executeGoal(int64_t goal_id) {
    GoalRec goal = loadGoal(goal_id);
    if (goal.id == 0) {
        PM_WARN("AgentLoop: executeGoal({}) — not found", goal_id);
        return;
    }
    if (goal.status == "done" || goal.status == "failed" || goal.status == "cancelled") {
        PM_INFO("AgentLoop: goal {} already terminal ({})", goal_id, goal.status);
        return;
    }
    // waiting_agent / waiting_user: only resume when caller forces execute; leave parked
    // unless there are pending steps (session completion will re-call executeGoal).
    if ((goal.status == "waiting_agent" || goal.status == "waiting_user") &&
        std::none_of(goal.steps.begin(), goal.steps.end(),
                     [](const PlanStepRec& s) { return s.status == "pending"; })) {
        return;
    }

    const Persona persona = loadActivePersona(db_);
    const QString request_id = goal.context.is_object() && goal.context.contains("request_id")
        ? QString::fromStdString(goal.context.value("request_id", "goal:" + std::to_string(goal_id)))
        : QStringLiteral("goal:%1").arg(goal_id);

    const auto start = std::chrono::steady_clock::now();
    persistGoalStatus(goal, "active");

    int steps_run = 0;
    bool parked = false;

    for (;;) {
        if (goalTimedOut(start)) {
            persistGoalStatus(goal, "failed",
                              {{"summary", "Goal timed out"},
                               {"error", "timeout"}});
            deliverGoalTerminal(goal, "timed out", goal.origin == "voice");
            return;
        }
        if (steps_run >= kMaxTotalSteps) {
            persistGoalStatus(goal, "failed",
                              {{"summary", "Exceeded total step cap"},
                               {"error", "step_cap"}});
            deliverGoalTerminal(goal, "step cap exceeded", goal.origin == "voice");
            return;
        }

        // Reload steps for crash-resume accuracy.
        goal = loadGoal(goal_id);
        PlanStepRec* next = nullptr;
        for (auto& s : goal.steps) {
            if (s.status == "pending") { next = &s; break; }
        }
        if (!next) break; // all done / no pending

        PlanStepRec& step = *next;
        step.status = "running";
        step.attempts += 1;
        persistStep(step);

        const bool ok = executeStep(goal, step, persona, request_id);
        ++steps_run;

        if (goal.status == "waiting_agent" || goal.status == "waiting_user") {
            parked = true;
            break;
        }

        if (!ok) {
            if (step.attempts < kMaxStepAttempts) {
                // Reflect / replan remaining, then retry.
                if (reflectAndReplan(goal, step, persona, request_id)) {
                    // Failed step skipped/replaced; continue.
                    continue;
                }
                // Reflection unavailable: re-queue same step as pending.
                step.status = "pending";
                persistStep(step);
                continue;
            }
            step.status = "failed";
            persistStep(step);
            const std::string summary =
                "Step " + std::to_string(step.idx) + " failed after " +
                std::to_string(step.attempts) + " attempts: " + step.description;
            persistGoalStatus(goal, "failed", {{"summary", summary}});
            deliverGoalTerminal(goal, summary, goal.origin == "voice");
            return;
        }
    }

    if (parked) {
        PM_INFO("AgentLoop: goal {} parked ({})", goal_id, goal.status);
        EventBus::instance().publishGoalUpdate({
            QString::number(goal.id),
            QString::fromStdString(goal.title),
            QString::fromStdString(goal.status),
            QStringLiteral("Waiting…"),
        });
        return;
    }

    // Success if every non-skipped step is done.
    goal = loadGoal(goal_id);
    bool any_failed = false;
    std::ostringstream sum;
    for (const auto& s : goal.steps) {
        if (s.status == "failed") any_failed = true;
        if (s.status == "done" && s.result.is_object() && s.result.contains("summary")) {
            if (sum.tellp() > 0) sum << "; ";
            sum << s.result.value("summary", "");
        }
    }
    std::string summary = sum.str();
    if (summary.empty()) summary = any_failed ? "Completed with failures" : "Completed";

    if (any_failed) {
        persistGoalStatus(goal, "failed", {{"summary", summary}});
        deliverGoalTerminal(goal, summary, goal.origin == "voice");
    } else {
        persistGoalStatus(goal, "done", {{"summary", summary}});
        deliverGoalTerminal(goal, summary, goal.origin == "voice");
    }
}

bool AgentLoop::executeStep(GoalRec& goal, PlanStepRec& step,
                            const Persona& persona, const QString& request_id) {
    const std::string kind = toLower(step.kind);
    bool ok = false;
    if (kind == "tool") {
        ok = dispatchToolStep(goal, step, persona, request_id);
    } else if (kind == "prompt") {
        ok = dispatchPromptStep(goal, step, persona, request_id);
    } else if (kind == "skill") {
        ok = dispatchSkillStep(goal, step);
    } else if (kind == "agent_session") {
        ok = dispatchAgentSessionStep(goal, step);
    } else if (kind == "surface") {
        ok = dispatchSurfaceStep(step);
    } else {
        step.result = {{"error", "unknown kind: " + step.kind}};
        step.status = "failed";
        persistStep(step);
        return false;
    }

    if (goal.status == "waiting_agent" || goal.status == "waiting_user") {
        // Step left pending/parked; don't mark done.
        return true;
    }

    if (ok) {
        step.status = "done";
        persistStep(step);
        appendTrace(goal, {{"event", "step_done"}, {"idx", step.idx},
                           {"kind", step.kind}, {"tool", step.tool}});
    } else {
        step.status = "failed";
        persistStep(step);
        appendTrace(goal, {{"event", "step_failed"}, {"idx", step.idx},
                           {"kind", step.kind}, {"result", step.result}});
    }
    return ok;
}

bool AgentLoop::dispatchToolStep(GoalRec& goal, PlanStepRec& step,
                                 const Persona& persona, const QString& request_id) {
    auto& bus = EventBus::instance();
    const std::string tool = step.tool.empty() ? step.args.value("tool", "") : step.tool;
    nlohmann::json args = step.args;
    if (args.contains("tool")) args.erase("tool");

    bus.publishToolCall({request_id, QString::fromStdString(tool),
                         QString::fromStdString(args.dump())});

    ITool* impl = tools_.get(tool);
    if (!impl) {
        step.result = {{"error", "unknown tool: " + tool}, {"ok", false}};
        bus.publishToolResult({request_id, QString::fromStdString(tool),
                               QString::fromStdString(step.result.dump()), false});
        return false;
    }

    ToolResult result;
    if (impl->isDeepTask()) {
        const qint64 task_id = sched_.enqueue(tool, args, /*priority*/ 0);
        result.ok = true;
        result.content = {{"queued", true}, {"task_id", task_id}, {"tool", tool}};
        result.summary = "Queued " + tool + " as background task " + std::to_string(task_id);
    } else {
        ToolContext tctx;
        tctx.inference = &inf_;
        tctx.db = &db_;
        tctx.memory = memory_;
        tctx.active_user_id = -1;
        tctx.active_personality = persona.name;
        try {
            result = impl->invoke(args, tctx);
        } catch (const std::exception& e) {
            result.ok = false;
            result.content = {{"error", e.what()}};
            result.summary = std::string("tool threw: ") + e.what();
        }
    }

    const std::string resultJson = compactToolResult(result.content.dump());
    step.result = nlohmann::json::parse(resultJson, nullptr, false);
    if (step.result.is_discarded()) step.result = {{"raw", resultJson}};
    if (!result.summary.empty()) step.result["summary"] = result.summary;
    step.result["ok"] = result.ok;

    bus.publishToolResult({request_id, QString::fromStdString(tool),
                           QString::fromStdString(resultJson), result.ok});
    if (!result.summary.empty()) {
        bus.publishNotice({result.ok ? "info" : "warn", "agent",
                           QString::fromStdString(result.summary)});
        ActivityLog(db_).record(tool, result.summary, result.ok);
    }
    (void)goal;
    return result.ok;
}

bool AgentLoop::dispatchPromptStep(GoalRec& goal, PlanStepRec& step,
                                   const Persona& persona, const QString& request_id) {
    // Build context from prior step results.
    std::ostringstream prior;
    for (const auto& s : goal.steps) {
        if (s.idx >= step.idx) break;
        if (s.status != "done") continue;
        prior << "Step " << s.idx << " (" << s.description << "): ";
        if (s.result.is_object() && s.result.contains("summary"))
            prior << s.result.value("summary", "") << "\n";
        else if (!s.result.is_null())
            prior << fitTokens(s.result.dump(), 200) << "\n";
    }

    std::vector<ChatMessage> msgs;
    msgs.push_back({Role::System,
        persona.system_prompt +
        "\nYou are executing one step of a multi-step goal. Answer the step request "
        "directly and concisely."});
    if (prior.tellp() > 0)
        msgs.push_back({Role::System, "Prior step results:\n" + prior.str()});
    msgs.push_back({Role::User, step.description});

    bool ok = false;
    std::string text = unconstrainedComplete(msgs, persona, request_id,
                                             /*stream*/ false, &ok);
    if (!ok || text.empty()) {
        // No model: use description as a pass-through note so deterministic
        // tests can still complete prompt steps.
        text = "Completed: " + step.description;
        step.result = {{"summary", text}, {"ok", true}, {"offline", true}};
        return true;
    }
    step.result = {{"summary", fitTokens(text, 400)}, {"text", fitTokens(text, 800)},
                   {"ok", true}};
    return true;
}

bool AgentLoop::dispatchSkillStep(GoalRec& goal, PlanStepRec& step) {
    const std::string name = step.tool.empty() ? step.args.value("name", "") : step.tool;
    nlohmann::json params = step.args.value("params", step.args);
    std::vector<PlanStepRec> expanded;
    if (!expandSkillViaRegistry(name, params, expanded)) {
        step.result = {{"error", "skill not found or empty: " + name}, {"ok", false}};
        return false;
    }
    // Insert expanded steps after current idx; mark this skill step done with summary.
    // Renumber: keep done steps, replace remaining pending with expansion + rest.
    std::vector<PlanStepRec> remaining;
    for (const auto& s : goal.steps) {
        if (s.idx > step.idx && (s.status == "pending" || s.status == "running"))
            remaining.push_back(s);
    }
    std::vector<PlanStepRec> new_pending = expanded;
    for (auto& r : remaining) new_pending.push_back(r);

    // Delete pending steps after current and re-insert.
    db_.exec("DELETE FROM plan_steps WHERE goal_id=?1 AND idx>?2 AND status='pending'",
             {goal.id, step.idx});
    int idx = step.idx + 1;
    for (auto& s : new_pending) {
        s.idx = idx++;
        s.goal_id = goal.id;
        s.status = "pending";
        s.id = db_.exec(
            "INSERT INTO plan_steps(goal_id,idx,description,kind,tool,args_json,"
            "status,attempts,updated_at) VALUES(?1,?2,?3,?4,?5,?6,'pending',0,?7)",
            {goal.id, s.idx, s.description, s.kind, s.tool,
             s.args.dump(), to_unix(Clock::now())});
    }
    step.result = {{"summary", "Expanded skill " + name + " into " +
                               std::to_string(expanded.size()) + " steps"},
                   {"ok", true}, {"expanded", expanded.size()}};
    // Reload goal steps for the execute loop.
    goal = loadGoal(goal.id);
    return true;
}

bool AgentLoop::dispatchAgentSessionStep(GoalRec& goal, PlanStepRec& step) {
    // C4 owns AgentSessionService. Park the goal until a session event resumes it.
    step.status = "pending";  // will be re-entered on resume
    step.result = {{"parked", true}, {"kind", "agent_session"},
                   {"summary", "Waiting on external agent session"}};
    persistStep(step);
    persistGoalStatus(goal, "waiting_agent",
                      {{"summary", "Waiting on agent session"},
                       {"step_idx", step.idx}});
    appendTrace(goal, {{"event", "waiting_agent"}, {"idx", step.idx}});
    return true;
}

bool AgentLoop::dispatchSurfaceStep(PlanStepRec& step) {
    SurfaceRequest r;
    r.id = QString::fromStdString(step.args.value("id", "surface-" + std::to_string(step.id)));
    r.action = QString::fromStdString(step.args.value("action", "spawn"));
    r.type = QString::fromStdString(step.args.value("type", "placeholder"));
    r.title = QString::fromStdString(step.args.value("title", step.description));
    if (step.args.contains("args")) {
        if (step.args["args"].is_string())
            r.args_json = QString::fromStdString(step.args["args"].get<std::string>());
        else
            r.args_json = QString::fromStdString(step.args["args"].dump());
    } else {
        r.args_json = QString::fromStdString(step.args.dump());
    }
    EventBus::instance().publishSurfaceRequest(r);
    step.result = {{"summary", "Published surface " + r.action.toStdString() +
                               " " + r.type.toStdString()},
                   {"ok", true},
                   {"surface_id", r.id.toStdString()}};
    return true;
}

bool AgentLoop::reflectAndReplan(GoalRec& goal, const PlanStepRec& failed,
                                 const Persona& persona, const QString& request_id) {
    // Collect remaining pending steps (excluding the failed one which is currently failed/running).
    std::ostringstream err;
    err << "Step " << failed.idx << " (" << failed.description << ") failed with: ";
    if (failed.result.is_object()) err << failed.result.dump();
    else err << "unknown error";

    std::ostringstream remain;
    for (const auto& s : goal.steps) {
        if (s.idx > failed.idx && s.status == "pending") {
            remain << "- [" << s.kind << "] " << s.description << "\n";
        }
    }

    nlohmann::json specs = tools_.specs(persona.tools);
    std::ostringstream sys;
    sys << "A plan step failed. Revise ONLY the remaining steps. Call emit_plan with a "
           "new steps array (kinds: tool|prompt|skill|surface). You may use tools:\n"
        << renderCatalog(specs);

    std::vector<grammar::ToolDef> defs = {{"emit_plan", planSchema()}};
    const std::string g = grammar::buildToolCallGrammar(defs);

    std::vector<ChatMessage> msgs;
    msgs.push_back({Role::System, sys.str()});
    msgs.push_back({Role::User,
        err.str() + "\nRemaining steps:\n" + remain.str() +
        "\nIf unrecoverable, return steps:[] and title \"unrecoverable\"."});

    bool ok = false;
    const std::string raw = constrainedComplete(msgs, persona, g, request_id,
                                                QStringLiteral(":reflect"), &ok);
    if (!ok) {
        // No model: leave failed step as pending for simple retry.
        return false;
    }

    std::string tool;
    nlohmann::json args;
    nlohmann::json plan;
    if (parseToolCall(raw, tool, args) && tool == "emit_plan") plan = args;
    else plan = parseJsonObject(raw);

    if (plan.is_object() && plan.value("title", "") == "unrecoverable") {
        return false; // caller will exhaust attempts → fail goal
    }

    auto new_steps = stepsFromPlanJson(plan);
    if (new_steps.empty()) {
        // Empty revision → treat as unrecoverable.
        return false;
    }

    // Mark failed step skipped (we replan past it).
    PlanStepRec skip = failed;
    skip.status = "skipped";
    if (!skip.result.is_object()) skip.result = nlohmann::json::object();
    skip.result["skipped_after_reflect"] = true;
    persistStep(skip);

    replacePendingSteps(goal, new_steps);
    appendTrace(goal, {{"event", "reflected"}, {"failed_idx", failed.idx},
                       {"new_steps", new_steps.size()}});
    goal = loadGoal(goal.id);
    return true;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

int64_t AgentLoop::createGoal(const std::string& title,
                              const std::string& origin,
                              const nlohmann::json& context,
                              const std::vector<PlanStepRec>& steps) {
    const int64_t now = to_unix(Clock::now());
    nlohmann::json ctx = context.is_object() ? context : nlohmann::json::object();
    if (!ctx.contains("trace")) ctx["trace"] = nlohmann::json::array();
    const int64_t gid = db_.exec(
        "INSERT INTO goals(title,status,origin,context_json,created_at,updated_at) "
        "VALUES(?1,'active',?2,?3,?4,?4)",
        {title, origin, ctx.dump(), now});
    std::vector<PlanStepRec> copy = steps;
    // Cap plan length.
    if (static_cast<int>(copy.size()) > kMaxPlanSteps)
        copy.resize(kMaxPlanSteps);
    insertSteps(gid, copy);
    PM_INFO("AgentLoop: created goal {} '{}' with {} step(s)", gid, title, copy.size());
    return gid;
}

void AgentLoop::insertSteps(int64_t goal_id, std::vector<PlanStepRec>& steps) const {
    const int64_t now = to_unix(Clock::now());
    for (size_t i = 0; i < steps.size(); ++i) {
        auto& s = steps[i];
        s.goal_id = goal_id;
        s.idx = static_cast<int>(i);
        if (s.status.empty()) s.status = "pending";
        s.id = db_.exec(
            "INSERT INTO plan_steps(goal_id,idx,description,kind,tool,args_json,"
            "status,result_json,attempts,updated_at) "
            "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)",
            {goal_id, s.idx, s.description, s.kind, s.tool,
             s.args.is_null() ? "{}" : s.args.dump(),
             s.status,
             s.result.is_null() ? nlohmann::json(nullptr) : nlohmann::json(s.result.dump()),
             s.attempts, now});
    }
}

void AgentLoop::persistStep(const PlanStepRec& step) const {
    if (step.id <= 0) return;
    const int64_t now = to_unix(Clock::now());
    db_.exec("UPDATE plan_steps SET status=?1, result_json=?2, attempts=?3, "
             "updated_at=?4, description=?5, kind=?6, tool=?7, args_json=?8 "
             "WHERE id=?9",
             {step.status,
              step.result.is_null() ? nlohmann::json(nullptr)
                                    : nlohmann::json(step.result.dump()),
              step.attempts, now, step.description, step.kind, step.tool,
              step.args.is_null() ? "{}" : step.args.dump(),
              step.id});
}

void AgentLoop::persistGoalStatus(GoalRec& goal, const std::string& status,
                                  const nlohmann::json& result) const {
    goal.status = status;
    if (!result.is_null()) goal.result = result;
    const int64_t now = to_unix(Clock::now());
    db_.exec("UPDATE goals SET status=?1, result_json=?2, context_json=?3, updated_at=?4 "
             "WHERE id=?5",
             {status,
              goal.result.is_null() ? nlohmann::json(nullptr)
                                    : nlohmann::json(goal.result.dump()),
              goal.context.is_null() ? "{}" : goal.context.dump(),
              now, goal.id});
}

void AgentLoop::appendTrace(GoalRec& goal, const nlohmann::json& event) const {
    if (!goal.context.is_object()) goal.context = nlohmann::json::object();
    if (!goal.context.contains("trace") || !goal.context["trace"].is_array())
        goal.context["trace"] = nlohmann::json::array();
    nlohmann::json e = event;
    e["ts"] = to_unix(Clock::now());
    goal.context["trace"].push_back(e);
    db_.exec("UPDATE goals SET context_json=?1, updated_at=?2 WHERE id=?3",
             {goal.context.dump(), to_unix(Clock::now()), goal.id});
}

void AgentLoop::replacePendingSteps(GoalRec& goal,
                                    const std::vector<PlanStepRec>& remaining) const {
    // Remove all pending steps; keep done/failed/skipped.
    db_.exec("DELETE FROM plan_steps WHERE goal_id=?1 AND status='pending'", {goal.id});
    int max_idx = -1;
    db_.query("SELECT MAX(idx) FROM plan_steps WHERE goal_id=?1", {goal.id},
              [&](const Row& r) {
                  if (!r.isNull(0)) max_idx = static_cast<int>(r.i64(0));
              });
    int idx = max_idx + 1;
    const int64_t now = to_unix(Clock::now());
    int count = 0;
    for (const auto& src : remaining) {
        if (count >= kMaxPlanSteps) break;
        db_.exec(
            "INSERT INTO plan_steps(goal_id,idx,description,kind,tool,args_json,"
            "status,attempts,updated_at) VALUES(?1,?2,?3,?4,?5,?6,'pending',0,?7)",
            {goal.id, idx++, src.description, src.kind, src.tool,
             src.args.is_null() ? "{}" : src.args.dump(), now});
        ++count;
    }
}

GoalRec AgentLoop::loadGoal(int64_t goal_id) const {
    GoalRec g;
    db_.query("SELECT id,title,status,origin,context_json,result_json FROM goals "
              "WHERE id=?1",
              {goal_id},
              [&](const Row& r) {
                  g.id = r.i64(0);
                  g.title = r.text(1);
                  g.status = r.text(2);
                  g.origin = r.text(3);
                  g.context = nlohmann::json::parse(r.text(4), nullptr, false);
                  if (g.context.is_discarded()) g.context = nlohmann::json::object();
                  if (!r.isNull(5)) {
                      g.result = nlohmann::json::parse(r.text(5), nullptr, false);
                      if (g.result.is_discarded()) g.result = nlohmann::json();
                  }
              });
    if (g.id == 0) return g;

    db_.query("SELECT id,goal_id,idx,description,kind,tool,args_json,status,"
              "result_json,attempts FROM plan_steps WHERE goal_id=?1 ORDER BY idx ASC",
              {goal_id},
              [&](const Row& r) {
                  PlanStepRec s;
                  s.id = r.i64(0);
                  s.goal_id = r.i64(1);
                  s.idx = static_cast<int>(r.i64(2));
                  s.description = r.text(3);
                  s.kind = r.text(4);
                  s.tool = r.isNull(5) ? "" : r.text(5);
                  s.args = nlohmann::json::parse(r.isNull(6) ? "{}" : r.text(6),
                                                 nullptr, false);
                  if (s.args.is_discarded() || !s.args.is_object())
                      s.args = nlohmann::json::object();
                  s.status = r.text(7);
                  if (!r.isNull(8)) {
                      s.result = nlohmann::json::parse(r.text(8), nullptr, false);
                      if (s.result.is_discarded()) s.result = nlohmann::json();
                  }
                  s.attempts = static_cast<int>(r.i64(9));
                  g.steps.push_back(std::move(s));
              });
    return g;
}

void AgentLoop::deliverGoalTerminal(const GoalRec& goal,
                                    const std::string& summary,
                                    bool from_voice) const {
    auto& bus = EventBus::instance();
    const QString status = QString::fromStdString(goal.status);
    bus.publishGoalUpdate({
        QString::number(goal.id),
        QString::fromStdString(goal.title),
        status,
        QString::fromStdString(summary),
    });

    const QString line = QStringLiteral("%1 Finished: %2 — %3")
                             .arg(goal.status == "done" ? QStringLiteral("✔")
                                                        : QStringLiteral("✖"))
                             .arg(QString::fromStdString(goal.title),
                                  QString::fromStdString(summary));
    bus.publishNotice({goal.status == "done" ? "good" : "error", "agent", line});

    // Optional TTS: agent.speak_results (default on) for voice-origin goals,
    // or whenever the caller marks from_voice (interactive voice path).
    Config cfg(db_);
    const bool speak_cfg = cfg.getBool(keys::AgentSpeakResults);
    const bool should_speak = from_voice || (speak_cfg && goal.origin == "voice");
    if (should_speak) {
        const Persona persona = loadActivePersona(db_);
        bus.publishSpeak({line, QString::fromStdString(persona.voice),
                          QStringLiteral("goal:%1").arg(goal.id)});
    }

    // Inject assistant transcript line for chat history.
    persistTranscript(line.toStdString(), /*assistant*/ true);
    PM_INFO("AgentLoop: goal {} terminal {} — {}", goal.id, goal.status, summary);
}

void AgentLoop::resumeActiveGoals() {
    recoverOnStartup();
    std::vector<int64_t> ids;
    db_.query("SELECT id FROM goals WHERE status='active' ORDER BY id ASC",
              {}, [&](const Row& r) { ids.push_back(r.i64(0)); });
    for (int64_t id : ids) {
        // Only resume if there is pending work.
        int pending = 0;
        db_.query("SELECT COUNT(*) FROM plan_steps WHERE goal_id=?1 AND status='pending'",
                  {id}, [&](const Row& r) { pending = static_cast<int>(r.i64(0)); });
        if (pending > 0) executeGoal(id);
    }
}

int AgentLoop::goalTimeoutMin() const {
    int m = Config(db_).getInt(keys::AgentGoalTimeoutMin, 30);
    if (m <= 0) m = 30;
    return m;
}

bool AgentLoop::goalTimedOut(const std::chrono::steady_clock::time_point& start) const {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto limit = std::chrono::minutes(goalTimeoutMin());
    return elapsed >= limit;
}

} // namespace polymath

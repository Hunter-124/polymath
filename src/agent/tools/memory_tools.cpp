#include "memory_tools.h"
#include "tool_support.h"
#include "database.h"
#include "memory_service.h"
#include "logging.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <vector>

// remember / recall / search_memory — long-term memory over the `memories`
// table (the persisted source of truth, per schema.h).
//
// When ToolContext.memory is set, remember/recall go through MemoryService
// (embed + hnswlib). When the embedder is unavailable (or memory is null),
// recall/search fall back to keyword-scored SQL so tools still work offline.

namespace polymath {

namespace {

// Split a query into lowercase word tokens for LIKE-based scoring.
std::vector<std::string> tokenize(const std::string& q) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : q) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!cur.empty()) {
            out.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

// Keyword-scored recall: rank recent memories by how many query tokens they
// contain (fallback when MemoryService / embedder is unavailable).
nlohmann::json keywordRecall(Database& db, const std::string& query, int k,
                             const std::string& kindFilter,
                             int64_t userId = -1) {
    const auto tokens = tokenize(query);

    // Pull a bounded candidate window (most recent first), then score in-memory.
    // Wave Z: when userId >= 0 include that user's rows + shared (NULL/-1).
    std::string sql = "SELECT id,kind,text,source,ts,user_id FROM memories";
    std::vector<nlohmann::json> params;
    if (!kindFilter.empty() && userId >= 0) {
        sql += " WHERE kind=?1 AND (user_id IS NULL OR user_id=-1 OR user_id=?2)";
        params.push_back(kindFilter);
        params.push_back(userId);
    } else if (!kindFilter.empty()) {
        sql += " WHERE kind=?1";
        params.push_back(kindFilter);
    } else if (userId >= 0) {
        sql += " WHERE (user_id IS NULL OR user_id=-1 OR user_id=?1)";
        params.push_back(userId);
    }
    sql += " ORDER BY ts DESC LIMIT 500";

    struct Cand { int64_t id; std::string kind, text, source; int64_t ts; int score; };
    std::vector<Cand> cands;
    db.query(sql, params, [&](const Row& r) {
        Cand c{r.i64(0), r.text(1), r.text(2), r.text(3), r.i64(4), 0};
        std::string lower = c.text;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
        for (const auto& t : tokens)
            if (lower.find(t) != std::string::npos) ++c.score;
        cands.push_back(std::move(c));
    });

    // Prefer higher token-overlap; break ties by recency.
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.ts > b.ts;
    });

    nlohmann::json hits = nlohmann::json::array();
    for (const auto& c : cands) {
        if (static_cast<int>(hits.size()) >= k) break;
        if (!tokens.empty() && c.score == 0) break;   // no overlap left; stop
        hits.push_back({
            {"id", c.id}, {"kind", c.kind}, {"text", c.text},
            {"source", c.source}, {"ts", c.ts}, {"score", c.score},
        });
    }
    return hits;
}

// Convert MemoryService::recall hits into the tool JSON shape, optionally
// filtering by kind (search_memory). Looks up kind/source/ts from the table.
nlohmann::json semanticHitsToJson(Database& db,
                                  const std::vector<MemoryHit>& hits,
                                  const std::string& kindFilter) {
    nlohmann::json out = nlohmann::json::array();
    if (hits.empty()) return out;

    std::string in_list;
    for (size_t i = 0; i < hits.size(); ++i) {
        if (i) in_list += ',';
        in_list += std::to_string(hits[i].id);
    }

    struct Meta { std::string kind, source; int64_t ts; };
    std::unordered_map<int64_t, Meta> meta;
    db.query("SELECT id,kind,source,ts FROM memories WHERE id IN (" + in_list + ")",
             {}, [&](const Row& r) {
                 meta.emplace(r.i64(0), Meta{r.text(1), r.text(2), r.i64(3)});
             });

    for (const auto& h : hits) {
        auto it = meta.find(h.id);
        const std::string kind = it != meta.end() ? it->second.kind : std::string{};
        if (!kindFilter.empty() && kind != kindFilter) continue;
        nlohmann::json row = {
            {"id", h.id},
            {"text", h.text},
            {"score", h.score},
            {"kind", kind},
        };
        if (it != meta.end()) {
            row["source"] = it->second.source;
            row["ts"] = it->second.ts;
        }
        out.push_back(std::move(row));
    }
    return out;
}

} // namespace

// --- remember ---------------------------------------------------------------

std::string RememberTool::name() const { return "remember"; }
std::string RememberTool::description() const {
    return "Store a durable fact, note, or preference in long-term memory so it can be "
           "recalled in future conversations.";
}

nlohmann::json RememberTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"text", {{"type", "string"}, {"description", "The information to remember"}}},
            {"kind", {{"type", "string"}, {"description", "note|fact|summary|preference (default note)"}}},
        }},
        {"required", {"text"}},
    };
}

ToolResult RememberTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const std::string text = args.value("text", "");
    const std::string kind = args.value("kind", "note");
    if (text.empty())
        return {false, {{"error", "text required"}}, "remember: missing text"};

    const int64_t uid = ctx.active_user_id;

    // Prefer MemoryService so the row is embedded + indexed for semantic recall.
    if (ctx.memory) {
        const int64_t id = ctx.memory->remember(text, kind, uid);
        if (id < 0)
            return {false, {{"error", "remember failed"}}, "remember: service rejected text"};
        PM_INFO("remember: id={} kind={} (via MemoryService)", id, kind);
        return {true, {{"memory_id", id}, {"kind", kind}}, "Remembered: " + text};
    }

    if (!ctx.db)
        return {false, {{"error", "no database"}}, "remember: no db"};

    nlohmann::json uidParam = (uid >= 0) ? nlohmann::json(uid) : nlohmann::json(nullptr);
    // Durable row; vector_id stays NULL for a later MemoryService backfill.
    const int64_t id = ctx.db->exec(
        "INSERT INTO memories(kind,text,vector_id,source,user_id,ts) "
        "VALUES(?1,?2,NULL,?3,?4,?5)",
        {kind, text, std::string("agent"), uidParam, tool_support::nowUnix()});

    PM_INFO("remember: id={} kind={} (db-only)", id, kind);
    return {true, {{"memory_id", id}, {"kind", kind}}, "Remembered: " + text};
}

// --- recall -----------------------------------------------------------------

std::string RecallTool::name() const { return "recall"; }
std::string RecallTool::description() const {
    return "Recall information from long-term memory relevant to a query.";
}

nlohmann::json RecallTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"},  {"description", "What to recall about"}}},
            {"k",     {{"type", "integer"}, {"description", "Max results (default 5)"}}},
        }},
        {"required", {"query"}},
    };
}

ToolResult RecallTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const std::string query = args.value("query", "");
    int k = args.value("k", 5);
    if (k <= 0 || k > 25) k = 5;
    if (query.empty())
        return {false, {{"error", "query required"}}, "recall: missing query"};

    nlohmann::json hits = nlohmann::json::array();

    // Semantic path first (MemoryService::recall → embed → hnsw).
    if (ctx.memory && ctx.db) {
        const auto sem = ctx.memory->recall(query, k);
        hits = semanticHitsToJson(*ctx.db, sem, /*kindFilter*/ "");
    }

    // Keyword fallback when embedder/index is unavailable or returned nothing.
    if (hits.empty() && ctx.db)
        hits = keywordRecall(*ctx.db, query, k, /*kindFilter*/ "", ctx.active_user_id);

    const size_t n = hits.size();
    nlohmann::json content = {{"query", query}, {"memories", std::move(hits)}};
    if (n == 0)
        return {true, std::move(content), "recall: nothing relevant to \"" + query + "\""};
    return {true, std::move(content), "Recalled " + std::to_string(n) + " memory item(s)"};
}

// --- search_memory ----------------------------------------------------------

std::string SearchMemoryTool::name() const { return "search_memory"; }
std::string SearchMemoryTool::description() const {
    return "Search long-term memory, optionally filtered by kind (note|fact|summary|caption), "
           "returning matching entries.";
}

nlohmann::json SearchMemoryTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"},  {"description", "Search text"}}},
            {"kind",  {{"type", "string"},  {"description", "Restrict to a memory kind"}}},
            {"k",     {{"type", "integer"}, {"description", "Max results (default 10)"}}},
        }},
        {"required", {"query"}},
    };
}

ToolResult SearchMemoryTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const std::string query = args.value("query", "");
    const std::string kind  = args.value("kind", "");
    int k = args.value("k", 10);
    if (k <= 0 || k > 50) k = 10;
    if (query.empty())
        return {false, {{"error", "query required"}}, "search_memory: missing query"};

    nlohmann::json hits = nlohmann::json::array();

    if (ctx.memory && ctx.db) {
        // Over-fetch when kind-filtering so we still fill k after the filter.
        const int fetch = kind.empty() ? k : std::max(k * 3, 15);
        const auto sem = ctx.memory->recall(query, fetch);
        hits = semanticHitsToJson(*ctx.db, sem, kind);
        if (static_cast<int>(hits.size()) > k)
            hits = nlohmann::json(hits.begin(), hits.begin() + k);
    }

    if (hits.empty() && ctx.db)
        hits = keywordRecall(*ctx.db, query, k, kind, ctx.active_user_id);

    const size_t n = hits.size();
    nlohmann::json content = {{"query", query}, {"kind", kind}, {"results", std::move(hits)}};
    return {true, std::move(content),
            "search_memory: " + std::to_string(n) + " match(es)"};
}

} // namespace polymath

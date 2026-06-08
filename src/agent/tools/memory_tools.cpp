#include "memory_tools.h"
#include "tool_support.h"
#include "database.h"
#include "logging.h"

#include <algorithm>
#include <cctype>
#include <sstream>

// remember / recall / search_memory — long-term memory over the `memories`
// table (the persisted source of truth, per schema.h).
//
// Vector upgrade: MemoryService owns the hnswlib index and is the canonical
// semantic indexer (MemoryService::remember embeds + inserts; recall does k-NN).
// The ToolContext exposes only Database + InferenceManager (not MemoryService),
// so these tools write the durable row here and the row is left with a NULL
// vector_id for MemoryService to embed and index on its own pass. Recall here
// uses a keyword-scored SQL query — a dependency-free fallback that always works
// even before the vector index has caught up. (See TODO about threading a
// MemoryService handle through ToolContext to enable inline semantic recall.)

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
// contain (a cheap proxy for relevance until the vector index is consulted).
nlohmann::json keywordRecall(Database& db, const std::string& query, int k,
                             const std::string& kindFilter) {
    const auto tokens = tokenize(query);

    // Pull a bounded candidate window (most recent first), then score in-memory.
    std::string sql = "SELECT id,kind,text,source,ts FROM memories";
    std::vector<nlohmann::json> params;
    if (!kindFilter.empty()) { sql += " WHERE kind=?1"; params.push_back(kindFilter); }
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
    nlohmann::json uidParam = (uid >= 0) ? nlohmann::json(uid) : nlohmann::json(nullptr);

    // Durable row; vector_id stays NULL for MemoryService to embed + index.
    const int64_t id = ctx.db->exec(
        "INSERT INTO memories(kind,text,vector_id,source,user_id,ts) "
        "VALUES(?1,?2,NULL,?3,?4,?5)",
        {kind, text, std::string("agent"), uidParam, tool_support::nowUnix()});

    PM_INFO("remember: id={} kind={}", id, kind);
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

    nlohmann::json hits = keywordRecall(*ctx.db, query, k, /*kindFilter*/ "");
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

    nlohmann::json hits = keywordRecall(*ctx.db, query, k, kind);
    const size_t n = hits.size();
    nlohmann::json content = {{"query", query}, {"kind", kind}, {"results", std::move(hits)}};
    return {true, std::move(content),
            "search_memory: " + std::to_string(n) + " match(es)"};
}

} // namespace polymath

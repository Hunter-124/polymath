#include "doc_rag.h"
#include "tool_support.h"

#include "database.h"
#include "inference_manager.h"
#include "paths.h"
#include "logging.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace polymath {

namespace fs = std::filesystem;

namespace {

// --- base64 over raw bytes (embeddings are stored as text, no BLOB binding) ---
constexpr const char* kB64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const unsigned char* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        unsigned n = (unsigned(data[i]) << 16) | (unsigned(data[i + 1]) << 8) | data[i + 2];
        out += kB64[(n >> 18) & 63]; out += kB64[(n >> 12) & 63];
        out += kB64[(n >> 6) & 63];  out += kB64[n & 63];
    }
    if (i < len) {
        unsigned n = unsigned(data[i]) << 16;
        if (i + 1 < len) n |= unsigned(data[i + 1]) << 8;
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += (i + 1 < len) ? kB64[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

bool base64Decode(const std::string& in, std::string& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = val(c);
        if (v < 0) return false;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out += char((buf >> bits) & 0xFF); }
    }
    return true;
}

// Encode an embedding as a base64 unit vector (so a stored dot product == cosine).
std::string encodeVec(Embedding v) {
    double norm = 0;
    for (float x : v) norm += double(x) * x;
    norm = std::sqrt(norm);
    if (norm > 0) for (float& x : v) x = float(x / norm);
    return base64Encode(reinterpret_cast<const unsigned char*>(v.data()),
                        v.size() * sizeof(float));
}

bool decodeVec(const std::string& b64, std::vector<float>& out) {
    std::string raw;
    if (!base64Decode(b64, raw) || raw.empty() || raw.size() % sizeof(float) != 0)
        return false;
    out.resize(raw.size() / sizeof(float));
    std::memcpy(out.data(), raw.data(), raw.size());
    return true;
}

// Text formats we index. (PDF/.docx need extraction we don't vendor yet.)
bool isTextFile(const fs::path& p) {
    static const std::set<std::string> ok = {
        ".txt", ".md", ".markdown", ".mkd", ".text", ".rst", ".csv",
        ".log", ".json", ".yaml", ".yml", ".tsv", ".ini", ".cfg", ".org"};
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return ok.count(ext) > 0;
}

// Split text into ~1.1k-char passages on paragraph boundaries, hard-wrapping any
// oversized paragraph on whitespace. Good-enough retrieval granularity for notes.
std::vector<std::string> chunkText(const std::string& raw) {
    constexpr size_t kMax = 1100;
    std::vector<std::string> chunks, paras;

    {   // paragraphs = runs of non-blank lines
        std::istringstream ss(raw);
        std::string line, para;
        auto pushPara = [&] { if (!para.empty()) { paras.push_back(para); para.clear(); } };
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find_first_not_of(" \t") == std::string::npos) pushPara();
            else { if (!para.empty()) para += '\n'; para += line; }
        }
        pushPara();
    }

    std::string cur;
    auto flush = [&] {
        size_t b = cur.find_first_not_of(" \t\r\n");
        size_t e = cur.find_last_not_of(" \t\r\n");
        if (b != std::string::npos) chunks.push_back(cur.substr(b, e - b + 1));
        cur.clear();
    };
    for (const auto& p : paras) {
        if (p.size() > kMax) {
            flush();
            size_t i = 0;
            while (i < p.size()) {
                size_t len = std::min(kMax, p.size() - i);
                if (i + len < p.size()) {
                    size_t sp = p.rfind(' ', i + len);
                    if (sp != std::string::npos && sp > i + kMax / 2) len = sp - i;
                }
                chunks.push_back(p.substr(i, len));
                i += len;
                while (i < p.size() && (p[i] == ' ' || p[i] == '\n')) ++i;
            }
            continue;
        }
        if (!cur.empty() && cur.size() + p.size() + 2 > kMax) flush();
        if (!cur.empty()) cur += "\n\n";
        cur += p;
    }
    flush();
    return chunks;
}

}  // namespace

// ---------------------------------------------------------------------------
//  DocRag engine
// ---------------------------------------------------------------------------
int64_t DocRag::indexedChunks(Database& db) {
    int64_t n = 0;
    db.query("SELECT COUNT(*) FROM knowledge_chunks", {}, [&](const Row& r) { n = r.i64(0); });
    return n;
}

DocRag::IngestStats DocRag::ingest(Database& db, InferenceManager& inf) {
    IngestStats st;

    // Probe the embedding model once: no model -> nothing to do (report honestly).
    if (inf.embed("ok").empty()) {
        st.no_model = true;
        PM_WARN("docrag: no embedding model loaded; skipping document ingest");
        return st;
    }

    const auto dir = Paths::instance().knowledge();
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::set<std::string> onDisk;
    int total = 0;

    for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        const fs::path path = it->path();
        if (!isTextFile(path)) continue;

        const std::string spath = path.string();
        onDisk.insert(spath);

        // Opaque change token (file_time count; stable per machine for comparison).
        int64_t mtime = 0;
        { std::error_code e2; auto t = fs::last_write_time(path, e2);
          if (!e2) mtime = int64_t(t.time_since_epoch().count()); }

        int64_t fileId = -1, storedMtime = -1;
        db.query("SELECT id,mtime FROM knowledge_files WHERE path=?1", {spath},
                 [&](const Row& r) { fileId = r.i64(0); storedMtime = r.i64(1); });
        if (fileId >= 0 && storedMtime == mtime) { st.skipped++; continue; }

        std::error_code e3;
        auto sz = fs::file_size(path, e3);
        if (!e3 && sz > 4ull * 1024 * 1024) {
            PM_WARN("docrag: skipping large file {} ({} bytes)", spath, uint64_t(sz));
            st.skipped++;
            continue;
        }

        std::ifstream in(path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        const auto chunks = chunkText(content);
        const std::string title = path.filename().string();
        const int64_t now = tool_support::nowUnix();

        if (fileId < 0) {
            fileId = db.exec(
                "INSERT INTO knowledge_files(path,title,mtime,chunk_count,indexed_at) "
                "VALUES(?1,?2,?3,0,?4)", {spath, title, mtime, now});
        } else {
            db.exec("DELETE FROM knowledge_chunks WHERE file_id=?1", {fileId});
            db.exec("UPDATE knowledge_files SET title=?1,mtime=?2,indexed_at=?3 WHERE id=?4",
                    {title, mtime, now, fileId});
        }

        int n = 0;
        for (size_t i = 0; i < chunks.size(); ++i) {
            if (total >= kMaxChunks) {
                PM_WARN("docrag: chunk cap {} reached; remaining content not indexed", kMaxChunks);
                break;
            }
            Embedding e = inf.embed(chunks[i]);
            if (e.empty()) { st.no_model = true; break; }
            db.exec("INSERT INTO knowledge_chunks(file_id,chunk_no,text,embedding) "
                    "VALUES(?1,?2,?3,?4)",
                    {fileId, int64_t(i), chunks[i], encodeVec(std::move(e))});
            ++n; ++st.chunks; ++total;
        }
        db.exec("UPDATE knowledge_files SET chunk_count=?1 WHERE id=?2", {int64_t(n), fileId});
        st.files++;
        PM_INFO("docrag: indexed '{}' ({} chunks)", title, n);
        if (st.no_model) break;
    }

    // Prune files that disappeared from disk.
    std::vector<std::pair<int64_t, std::string>> known;
    db.query("SELECT id,path FROM knowledge_files", {},
             [&](const Row& r) { known.emplace_back(r.i64(0), r.text(1)); });
    for (const auto& [id, p] : known) {
        if (!onDisk.count(p)) {
            db.exec("DELETE FROM knowledge_chunks WHERE file_id=?1", {id});
            db.exec("DELETE FROM knowledge_files WHERE id=?1", {id});
            st.removed++;
        }
    }

    PM_INFO("docrag: ingest done — {} file(s), {} chunk(s), {} unchanged, {} removed",
            st.files, st.chunks, st.skipped, st.removed);
    return st;
}

std::vector<DocRag::DocHit> DocRag::search(Database& db, InferenceManager& inf,
                                           const std::string& query, int k) {
    std::vector<DocHit> hits;
    if (query.empty() || k <= 0) return hits;

    // Lazy first-time ingest so it "just works" after dropping files in.
    if (indexedChunks(db) == 0) ingest(db, inf);

    Embedding q = inf.embed(query);
    if (q.empty()) return hits;   // no embedding model

    double norm = 0;
    for (float x : q) norm += double(x) * x;
    norm = std::sqrt(norm);
    if (norm > 0) for (float& x : q) x = float(x / norm);

    db.query("SELECT c.chunk_no,c.text,c.embedding,f.title "
             "FROM knowledge_chunks c JOIN knowledge_files f ON f.id=c.file_id", {},
        [&](const Row& r) {
            std::vector<float> v;
            if (!decodeVec(r.text(2), v) || v.size() != q.size()) return;
            float dot = 0;
            for (size_t i = 0; i < v.size(); ++i) dot += v[i] * q[i];
            DocHit h;
            h.chunk_no = int(r.i64(0));
            h.text     = r.text(1);
            h.file     = r.text(3);
            h.score    = dot;
            hits.push_back(std::move(h));
        });

    const auto byScore = [](const DocHit& a, const DocHit& b) { return a.score > b.score; };
    if (int(hits.size()) > k) {
        std::partial_sort(hits.begin(), hits.begin() + k, hits.end(), byScore);
        hits.resize(k);
    } else {
        std::sort(hits.begin(), hits.end(), byScore);
    }
    // Trim weak matches so we don't feed the model irrelevant noise.
    constexpr float kMinScore = 0.25f;
    while (!hits.empty() && hits.back().score < kMinScore) hits.pop_back();
    return hits;
}

// ---------------------------------------------------------------------------
//  Tools
// ---------------------------------------------------------------------------
std::string SearchDocumentsTool::name() const { return "search_documents"; }
std::string SearchDocumentsTool::description() const {
    return "Search the user's own documents and notes — the files they placed in Hearth's local "
           "knowledge folder — and return the most relevant passages with their source filename. "
           "Use this to answer questions about the user's personal files, notes, manuals, or papers. "
           "Everything stays on this machine.";
}
nlohmann::json SearchDocumentsTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"},  {"description", "What to look for in the documents"}}},
            {"k",     {{"type", "integer"}, {"description", "Max passages to return (default 5)"}}},
        }},
        {"required", {"query"}},
    };
}
ToolResult SearchDocumentsTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    if (!ctx.db || !ctx.inference)
        return {false, {{"error", "unavailable"}}, "search_documents: backend unavailable"};
    const std::string query = args.is_object() ? args.value("query", std::string{}) : std::string{};
    int k = args.is_object() ? args.value("k", 5) : 5;
    if (k <= 0 || k > 12) k = 5;
    if (query.empty())
        return {false, {{"error", "query required"}}, "search_documents: missing query"};

    auto hits = DocRag::search(*ctx.db, *ctx.inference, query, k);

    nlohmann::json passages = nlohmann::json::array();
    std::set<std::string> files;
    for (const auto& h : hits) {
        passages.push_back({{"file", h.file}, {"text", h.text}, {"score", h.score}});
        files.insert(h.file);
    }
    nlohmann::json content = {{"query", query}, {"passages", std::move(passages)}};
    if (hits.empty()) {
        const int64_t n = DocRag::indexedChunks(*ctx.db);
        const std::string msg = n == 0
            ? "search_documents: no documents indexed yet — drop files in the knowledge folder, "
              "then call reindex_documents"
            : "search_documents: nothing relevant to \"" + query + "\"";
        return {true, std::move(content), msg};
    }
    return {true, std::move(content),
            "Found " + std::to_string(hits.size()) + " passage(s) across " +
            std::to_string(files.size()) + " file(s)"};
}

std::string ReindexDocumentsTool::name() const { return "reindex_documents"; }
std::string ReindexDocumentsTool::description() const {
    return "Rescan the user's local knowledge folder and (re)index any new or changed documents so "
           "they become searchable with search_documents. Call this after the user says they added "
           "or changed files in their documents/knowledge folder.";
}
nlohmann::json ReindexDocumentsTool::parametersSchema() const {
    return {{"type", "object"}, {"properties", nlohmann::json::object()}};
}
ToolResult ReindexDocumentsTool::invoke(const nlohmann::json&, ToolContext& ctx) {
    if (!ctx.db || !ctx.inference)
        return {false, {{"error", "unavailable"}}, "reindex_documents: backend unavailable"};
    auto st = DocRag::ingest(*ctx.db, *ctx.inference);
    nlohmann::json content = {{"files", st.files}, {"chunks", st.chunks},
                              {"unchanged", st.skipped}, {"removed", st.removed}};
    if (st.no_model)
        return {false, std::move(content),
                "reindex_documents: no embedding model is loaded — add an 'embedding' model in Settings first"};
    return {true, std::move(content),
            "Indexed " + std::to_string(st.files) + " file(s), " + std::to_string(st.chunks) +
            " passage(s) (" + std::to_string(st.skipped) + " unchanged, " +
            std::to_string(st.removed) + " removed)"};
}

}  // namespace polymath

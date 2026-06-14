#pragma once
//
// DocRag — local "ask your documents" (RAG). Indexes plain-text / markdown files
// the user drops into Paths::knowledge() into embedded passages (knowledge_files
// / knowledge_chunks), and answers queries by brute-force cosine over those
// embeddings using the local EmbeddingGemma model — fully offline, no cloud.
//
// Brute force is deliberate: a personal knowledge base is hundreds-to-thousands
// of chunks, where an O(N) scan is sub-millisecond and avoids a second on-disk
// vector index to keep in sync. kMaxChunks bounds the corpus.
//
#include "i_tool.h"
#include <string>
#include <vector>

namespace polymath {

class Database;
class InferenceManager;

class DocRag {
public:
    struct IngestStats {
        int  files = 0;       // files (re)indexed this pass
        int  chunks = 0;      // passages embedded this pass
        int  skipped = 0;     // unchanged files left as-is
        int  removed = 0;     // files deleted from disk, pruned from the index
        bool no_model = false;// no embedding model loaded -> nothing indexed
    };
    // Scan Paths::knowledge() and (re)index new/changed files, prune deleted ones.
    static IngestStats ingest(Database& db, InferenceManager& inf);

    struct DocHit {
        std::string file;     // display filename
        int         chunk_no = 0;
        std::string text;     // the passage
        float       score = 0.0f;   // cosine similarity in [-1, 1]
    };
    // Up to `k` most relevant passages. Lazily ingests once if nothing is indexed
    // yet. Empty if there is no embedding model or no relevant content.
    static std::vector<DocHit> search(Database& db, InferenceManager& inf,
                                      const std::string& query, int k);

    // Number of indexed passages (cheap COUNT) — lets callers craft good messages.
    static int64_t indexedChunks(Database& db);

    static constexpr int kMaxChunks = 6000;   // corpus ceiling (brute-force scan)
};

// search_documents — retrieve passages from the user's own files to answer from.
class SearchDocumentsTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

// reindex_documents — rescan the knowledge folder and (re)embed changed files.
class ReindexDocumentsTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath

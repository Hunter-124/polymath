// v3 — local document RAG end-to-end.
//
// Drives the real DocRag engine against a temp DB + the on-disk EmbeddingGemma
// model. Writes a few clearly-themed text files into Paths::knowledge(), ingests
// them, and proves that a semantic query surfaces the RIGHT file's passage as the
// top hit — and that re-ingest is idempotent (unchanged files are skipped).
//
// Skips cleanly when no embedding .gguf is present (CI without the ~28 GB models),
// exactly like test_memory_e2e; set POLYMATH_E2E_FULL=1 to require it.

#include "config.h"
#include "database.h"
#include "inference_manager.h"
#include "paths.h"
#include "doc_rag.h"

#include <QCoreApplication>
#include <QDir>

#undef NDEBUG   // keep assert() live in Release
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace polymath;
namespace fs = std::filesystem;

static void writeFile(const fs::path& p, const std::string& content) {
    std::ofstream out(p, std::ios::binary);
    out << content;
}

static bool embeddingModelOnDisk(const fs::path& modelsDir) {
    const auto dir = modelsDir / "embeddings";
    std::error_code ec;
    if (!fs::exists(dir, ec)) return false;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".gguf") return true;
    return false;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const fs::path work = fs::temp_directory_path() / "polymath_test_docrag";
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work / "knowledge", ec);
    Paths::instance().setRoot(work);

    const fs::path dataModels =
        fs::path(QDir(QCoreApplication::applicationDirPath())
                     .absoluteFilePath("data/models").toStdWString());

    const fs::path dbPath = work / "polymath.db";

    // Register the embedding model by absolute path (skip auto-discovery).
    {
        Database db;
        assert(db.open(dbPath.string()));
        Config(db).seedDefaults();
        if (fs::exists(dataModels / "embeddings", ec))
            for (auto& e : fs::directory_iterator(dataModels / "embeddings", ec))
                if (e.path().extension() == ".gguf")
                    db.exec("INSERT OR IGNORE INTO models(id,display_name,path,role,n_ctx,"
                            "n_gpu_layers,chat_template,mmproj_path,is_active) "
                            "VALUES(?1,?1,?2,'embedding',2048,999,'','',1)",
                            {e.path().stem().string(), e.path().string()});
        db.close();
    }

    if (!embeddingModelOnDisk(dataModels)) {
        if (std::getenv("POLYMATH_E2E_FULL")) {
            assert(false && "EmbeddingGemma .gguf not found (POLYMATH_E2E_FULL=1)");
        }
        std::printf("test_doc_rag: SKIP — no EmbeddingGemma .gguf under data/models/embeddings; "
                    "set POLYMATH_E2E_FULL=1 to require it.\n");
        return 0;
    }

    // Three clearly-distinct documents so a topical query must pick its own file.
    writeFile(work / "knowledge" / "garden.md",
        "# Backyard Garden Notes\n\n"
        "I planted tomato and basil seedlings in the raised bed along the south fence. "
        "Tomatoes need full sun and deep watering every two or three days; let the top inch "
        "of soil dry between waterings to avoid root rot.\n\n"
        "Basil likes to be pinched back often to stay bushy. Add compost in spring and mulch "
        "to keep the soil moist through the hottest weeks of summer.\n");
    writeFile(work / "knowledge" / "home_network.md",
        "# Home Network Setup\n\n"
        "The Wi-Fi router lives in the hallway closet. To change the admin password, browse to "
        "192.168.1.1, sign in, and open the Administration tab. The guest network SSID is HearthGuest.\n\n"
        "Port forwarding for the game server is under Advanced then NAT. Reboot the router after "
        "changing any wireless settings for them to take effect.\n");
    writeFile(work / "knowledge" / "pasta_recipe.md",
        "# Weeknight Garlic Pasta\n\n"
        "Bring a large pot of salted water to a boil and cook the spaghetti until al dente. "
        "Meanwhile warm plenty of olive oil with thinly sliced garlic over low heat until "
        "fragrant and just golden.\n\n"
        "Toss the drained pasta with the garlic oil, a ladle of pasta water, chili flakes, and "
        "grated parmesan. Finish with chopped parsley and black pepper.\n");

    Database db;
    assert(db.open(dbPath.string()));
    InferenceManager inf(db);
    inf.reloadRegistry();        // embed() loads the embedding model on demand

    // Sanity: the embedding model really loads.
    assert(!inf.embed("hello").empty() &&
           "embedding model failed to produce a vector — check data/models/embeddings");

    // --- ingest ------------------------------------------------------------
    auto st = DocRag::ingest(db, inf);
    std::printf("test_doc_rag: ingest -> files=%d chunks=%d skipped=%d removed=%d\n",
                st.files, st.chunks, st.skipped, st.removed);
    assert(!st.no_model && "ingest reported no embedding model");
    assert(st.files == 3 && "expected 3 files indexed");
    assert(st.chunks >= 3 && "expected at least one passage per file");
    assert(DocRag::indexedChunks(db) == st.chunks && "indexed chunk count mismatch");

    // --- semantic routing: each query must surface its own file as the top hit -
    struct Case { const char* query; const char* expectFile; };
    const Case cases[] = {
        {"how often should I water my tomato plants",        "garden.md"},
        {"reset the wireless router administrator password", "home_network.md"},
        {"cook spaghetti with garlic and olive oil",         "pasta_recipe.md"},
    };
    for (const auto& c : cases) {
        auto hits = DocRag::search(db, inf, c.query, 3);
        assert(!hits.empty() && "query returned no passages");
        std::printf("test_doc_rag: \"%s\" -> top=%s (%.3f)\n",
                    c.query, hits[0].file.c_str(), hits[0].score);
        assert(hits[0].file == c.expectFile &&
               "semantic search routed the query to the wrong document");
    }

    // --- idempotent re-ingest: nothing changed -> all skipped, none re-indexed -
    auto st2 = DocRag::ingest(db, inf);
    std::printf("test_doc_rag: re-ingest -> files=%d skipped=%d\n", st2.files, st2.skipped);
    assert(st2.files == 0 && st2.skipped == 3 &&
           "re-ingest should skip unchanged files");

    // --- deleting a file prunes it from the index --------------------------
    fs::remove(work / "knowledge" / "pasta_recipe.md", ec);
    auto st3 = DocRag::ingest(db, inf);
    assert(st3.removed == 1 && "deleted file should be pruned");
    {
        auto hits = DocRag::search(db, inf, "cook spaghetti with garlic and olive oil", 3);
        for (const auto& h : hits)
            assert(h.file != "pasta_recipe.md" && "pruned file still returned in search");
    }

    db.close();
    fs::remove_all(work, ec);
    std::puts("test_doc_rag: OK");
    return 0;
}

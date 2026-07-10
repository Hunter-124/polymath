// Wave 2 · Card G — privacy / persistence / at-rest security, end-to-end over
// the real core data layer (Database + Config + Retention + ActivityLog).
//
// Drives the four pillars holistically on an on-disk temp DB:
//   1. Gating       — master kill-switch ANDs every per-feature sense toggle, so
//                     the value each service reads before capturing goes false.
//   2. Retention    — the sweeper purges expired ambient transcripts / events
//                     (explicit TTL + per-category window) and leaves fresh rows.
//   3. Encryption   — a keyed open round-trips; we assert the key path is wired
//                     and report whether a real cipher engaged (toolchain-gated).
//   4. Activity log — a recorded web/tool action lands in the surfaced feed and
//                     is aged out by retention like any other event.
//
#include "database.h"
#include "config.h"
#include "retention.h"
#include "activity_log.h"
#include "types.h"

#undef NDEBUG   // keep assert() live in Release, or the test is a no-op
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace polymath;

namespace {

int64_t countEvents(Database& db, const std::string& kind) {
    int64_t n = 0;
    db.query("SELECT COUNT(*) FROM events WHERE kind=?1", {kind},
             [&](const Row& r) { n = r.i64(0); });
    return n;
}

int64_t countTranscripts(Database& db) {
    int64_t n = 0;
    db.query("SELECT COUNT(*) FROM transcripts", {}, [&](const Row& r) { n = r.i64(0); });
    return n;
}

// Insert a transcript directly (mirrors the audio pipeline's write).
void insertTranscript(Database& db, const std::string& text, bool ambient,
                      int64_t ts, int64_t ttl_at /*0 => NULL*/) {
    db.exec("INSERT INTO transcripts(text,speaker,is_ambient,ttl_at,ts) "
            "VALUES(?1,NULL,?2,?3,?4)",
            {text, ambient ? 1 : 0,
             ttl_at ? nlohmann::json(ttl_at) : nlohmann::json(nullptr), ts});
}

void insertEvent(Database& db, const std::string& kind, int64_t ts) {
    db.exec("INSERT INTO events(kind,label,ts) VALUES(?1,'',?2)", {kind, ts});
}

} // namespace

// --- 1. Master kill-switch gates every sense toggle -------------------------
static void test_gating() {
    auto tmp = std::filesystem::temp_directory_path() / "pm_privacy_gating.db";
    std::filesystem::remove(tmp);

    Database db;
    assert(db.open(tmp.string()));
    Config cfg(db);
    cfg.seedDefaults();

    // Defaults: master on, every sense on.
    assert(cfg.masterEnabled());
    assert(cfg.getBool(keys::MicEnabled));
    assert(cfg.getBool(keys::AmbientTranscription));
    assert(cfg.getBool(keys::FaceRecognition));
    assert(cfg.getBool(keys::CamerasEnabled));

    // Flip the master kill-switch OFF: every sense the services consult reads
    // false, even though each feature's own value is still "1".
    cfg.set(keys::MasterEnabled, "0");
    assert(!cfg.masterEnabled());
    assert(!cfg.getBool(keys::MicEnabled));
    assert(!cfg.getBool(keys::AmbientTranscription));
    assert(!cfg.getBool(keys::FaceRecognition));
    assert(!cfg.getBool(keys::CamerasEnabled));

    // The raw stored value is unchanged (so the UI can still show the toggle on)
    // and non-sense keys are NOT gated by the master switch.
    assert(cfg.getBool(keys::MicEnabled, /*respectMaster=*/false));
    assert(db.getBool(keys::MicEnabled, false));   // raw still "1"

    // A single per-feature toggle still works independently with master back on.
    cfg.set(keys::MasterEnabled, "1");
    cfg.set(keys::MicEnabled, "0");
    assert(!cfg.getBool(keys::MicEnabled));
    assert(cfg.getBool(keys::CamerasEnabled));     // unrelated sense unaffected

    // Overhaul A2: new UI/audio/agent keys seed and round-trip via Config.
    assert(cfg.getStr(keys::UiAccent, "") == "#33E1FF");
    assert(cfg.getStr(keys::LlmKvQuant, "") == "q8_0");
    assert(cfg.getInt(keys::AgentGoalTimeoutMin, 0) == 30);
    cfg.set(keys::UiAccent, "#FF00AA");
    assert(cfg.getStr(keys::UiAccent, "") == "#FF00AA");
    cfg.set(keys::UiFontScale, "1.25");
    assert(cfg.getStr(keys::UiFontScale, "") == "1.25");

    db.close();
    std::filesystem::remove(tmp);
    std::puts("  [1] gating: master kill-switch + per-feature toggles OK");
    std::puts("  [1b] A2 settings keys seed + get/set round-trip OK");
}

// --- 2. Retention sweeper purges expired, keeps fresh -----------------------
static void test_retention() {
    auto tmp = std::filesystem::temp_directory_path() / "pm_privacy_retention.db";
    std::filesystem::remove(tmp);

    Database db;
    assert(db.open(tmp.string()));
    Config cfg(db);
    cfg.seedDefaults();
    cfg.set(keys::RetainAmbientDays, "7");
    cfg.set(keys::RetainEventsDays, "30");

    const int64_t now = to_unix(Clock::now());
    const int64_t day = 86'400;

    // Transcripts:
    insertTranscript(db, "expired-by-explicit-ttl", true, now - 2 * day, now - day);  // TTL passed
    insertTranscript(db, "fresh-explicit-ttl",      true, now,           now + day);  // TTL future
    insertTranscript(db, "ambient-old-no-ttl",      true, now - 30 * day, 0);         // > 7d window
    insertTranscript(db, "ambient-fresh-no-ttl",    true, now - 1 * day,  0);         // within window
    insertTranscript(db, "command-old-no-ttl",      false, now - 365 * day, 0);       // command kept

    // Events:
    insertEvent(db, "person", now - 60 * day);   // > 30d -> swept
    insertEvent(db, "motion", now - 5 * day);    // within window -> kept

    assert(countTranscripts(db) == 5);
    assert(countEvents(db, "person") == 1);

    Retention ret(db);
    SweepResult r = ret.sweep(now);

    // Two transcripts gone (explicit-ttl-expired + ambient-old); three remain.
    assert(r.transcripts_removed == 2);
    assert(countTranscripts(db) == 3);
    // The shortest-lived category (ambient) is gone where stale; fresh/command stay.
    {
        bool has_fresh_ttl = false, has_fresh_ambient = false, has_command = false, has_old = false;
        db.query("SELECT text FROM transcripts", {}, [&](const Row& row) {
            const std::string t = row.text(0);
            if (t == "fresh-explicit-ttl")   has_fresh_ttl = true;
            if (t == "ambient-fresh-no-ttl") has_fresh_ambient = true;
            if (t == "command-old-no-ttl")   has_command = true;
            if (t == "ambient-old-no-ttl" || t == "expired-by-explicit-ttl") has_old = true;
        });
        assert(has_fresh_ttl && has_fresh_ambient && has_command && !has_old);
    }

    // One event swept (old person), one kept (recent motion).
    assert(r.events_removed == 1);
    assert(countEvents(db, "person") == 0);
    assert(countEvents(db, "motion") == 1);

    // A second sweep with nothing new past TTL is a no-op (idempotent).
    SweepResult r2 = ret.sweep(now);
    assert(r2.total() == 0);

    // retention.*_days == 0 means keep forever (only explicit-ttl rows age out).
    cfg.set(keys::RetainAmbientDays, "0");
    cfg.set(keys::RetainEventsDays, "0");
    insertTranscript(db, "ambient-ancient", true, now - 999 * day, 0);
    insertEvent(db, "motion", now - 999 * day);
    SweepResult r3 = ret.sweep(now);
    assert(r3.total() == 0);   // nothing removed when windows are disabled

    db.close();
    std::filesystem::remove(tmp);
    std::puts("  [2] retention: per-category TTL purge + keep-fresh OK");
}

// --- 3. At-rest encryption is wired (key path round-trips) ------------------
static void test_encryption() {
    auto tmp = std::filesystem::temp_directory_path() / "pm_privacy_enc.db";
    std::filesystem::remove(tmp);

    const std::string key = Database::deriveKey("polymath-test-install-secret");
    assert(key.size() == 64);                         // 256-bit hex
    assert(Database::deriveKey("a") != Database::deriveKey("b"));   // input-sensitive

    // Open WITH the key, write a secret row, close.
    {
        Database db;
        assert(db.open(tmp.string(), key));           // key path runs, db readable
        Config cfg(db);
        cfg.seedDefaults();
        db.setSetting("secret", "top-secret-value");
        std::printf("  [3] encryption: encryptionActive=%s cipher='%s'\n",
                    db.encryptionActive() ? "true" : "false",
                    db.cipherVersion().c_str());
        db.close();
    }

    // Re-open WITH the correct key -> the value round-trips regardless of build.
    {
        Database db;
        assert(db.open(tmp.string(), key));
        assert(db.getSetting("secret") == "top-secret-value");
        const bool encrypted = db.encryptionActive();
        db.close();

        // The production build links a real SQLCipher codec, so encryption MUST
        // be active. We require it: a wrong-key open is rejected and the bytes on
        // disk are ciphertext (no plaintext, no SQLite header). If the codec is
        // somehow absent (a fallback build without OpenSSL), we do NOT silently
        // pass — we assert the documented plaintext fallback AND fail the test so
        // the toolchain gap is loud, since at-rest encryption is now a contract.
        const bool codec = Database::hasCodec();
        assert(codec && "SQLCipher codec missing — at-rest encryption is REQUIRED. "
                        "Build with OpenSSL so the vendored SQLCipher amalgamation "
                        "links (see third_party/CMakeLists.txt).");
        assert(encrypted && "encryptionActive() must be true with the SQLCipher build");

        // SQLCipher present: opening WITHOUT the key must fail (NOTADB), and
        // the raw bytes must not contain our plaintext or the SQLite header.
        Database d2;
        assert(!d2.open(tmp.string(), ""));            // no key rejected
        Database d3;
        assert(!d3.open(tmp.string(), Database::deriveKey("WRONG-secret")));  // wrong key rejected
        std::FILE* f = std::fopen(tmp.string().c_str(), "rb");
        assert(f);
        std::string blob; char buf[4096]; size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) blob.append(buf, n);
        std::fclose(f);
        assert(blob.find("top-secret-value") == std::string::npos);
        assert(blob.find("SQLite format 3") == std::string::npos);
        std::puts("  [3] encryption: SQLCipher ACTIVE — file ciphered, wrong key rejected");
    }

    std::filesystem::remove(tmp);
}

// --- 3b. Plaintext -> encrypted migration on first run ----------------------
static void test_migration() {
    if (!Database::hasCodec()) {
        std::puts("  [3b] migration: SKIP (no SQLCipher codec in this build)");
        return;
    }
    auto tmp = std::filesystem::temp_directory_path() / "pm_privacy_migrate.db";
    auto rm_all = [&] {
        for (const char* suf : {"", "-wal", "-shm", ".plaintext.bak"})
            std::filesystem::remove(std::filesystem::path(tmp.string() + suf));
    };
    rm_all();   // start clean (a stale encrypted -wal beside a fresh DB would confuse step 1)

    const std::string key = Database::deriveKey("migration-install-secret");

    // 1) Create a PLAINTEXT db (no key) with a recognizable secret row.
    {
        Database db;
        assert(db.open(tmp.string()));                 // no key => plaintext on disk
        Config cfg(db);
        cfg.seedDefaults();
        db.setSetting("legacy", "plaintext-payload");
        assert(!db.encryptionActive());
        db.close();
    }
    // Sanity: the file really is a plaintext SQLite header on disk.
    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "rb");
        assert(f);
        char hdr[16] = {0};
        assert(std::fread(hdr, 1, sizeof hdr, f) == sizeof hdr);
        std::fclose(f);
        assert(std::string(hdr, 15) == "SQLite format 3");
    }

    // 2) Re-open WITH the key: open() should detect the plaintext file and
    //    migrate it to encrypted in place, preserving the row.
    {
        Database db;
        assert(db.open(tmp.string(), key));
        assert(db.encryptionActive());                 // now ciphered
        assert(db.getSetting("legacy") == "plaintext-payload");  // data survived
        db.close();
    }
    // 3) On disk: ciphertext (no header), and a plaintext backup was kept.
    {
        std::FILE* f = std::fopen(tmp.string().c_str(), "rb");
        assert(f);
        std::string blob; char buf[4096]; size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) blob.append(buf, n);
        std::fclose(f);
        assert(blob.find("SQLite format 3") == std::string::npos);
        assert(blob.find("plaintext-payload") == std::string::npos);
        assert(std::filesystem::exists(std::filesystem::path(tmp.string() + ".plaintext.bak")));
    }
    // 4) Wrong key no longer opens the now-encrypted db.
    {
        Database db;
        assert(!db.open(tmp.string(), Database::deriveKey("not-the-key")));
    }

    rm_all();
    std::puts("  [3b] migration: plaintext DB upgraded to encrypted, data preserved OK");
}

// --- 4. Activity log records + surfaces web/tool actions --------------------
static void test_activity_log() {
    auto tmp = std::filesystem::temp_directory_path() / "pm_privacy_activity.db";
    std::filesystem::remove(tmp);

    Database db;
    assert(db.open(tmp.string()));
    Config cfg(db);
    cfg.seedDefaults();

    ActivityLog log(db);
    const int64_t id1 = log.record("web_search", "Found 5 result(s) for \"weather\"", true);
    const int64_t id2 = log.record("fetch_page", "fetched https://example.com", true);
    const int64_t id3 = log.record("print_document", "printer offline", false);
    assert(id1 > 0 && id2 > 0 && id3 > 0);

    // Surfaced through the same feed the Timeline/Privacy view reads (events).
    assert(countEvents(db, "tool") == 3);
    bool sawSearch = false, sawFailed = false;
    db.query("SELECT label FROM events WHERE kind='tool' ORDER BY ts ASC", {},
             [&](const Row& r) {
                 const std::string l = r.text(0);
                 if (l.find("web_search") != std::string::npos) sawSearch = true;
                 if (l.find("(failed)") != std::string::npos)   sawFailed = true;
             });
    assert(sawSearch && sawFailed);

    // Activity entries age out under the events retention window like anything
    // else in the table (privacy contract: no eternal logs).
    const int64_t now = to_unix(Clock::now());
    db.exec("UPDATE events SET ts=?1 WHERE kind='tool'", {now - 400 * 86'400});
    cfg.set(keys::RetainEventsDays, "30");
    Retention(db).sweep(now);
    assert(countEvents(db, "tool") == 0);

    db.close();
    std::filesystem::remove(tmp);
    std::puts("  [4] activity log: tool actions recorded, surfaced, and aged OK");
}

int main() {
    std::puts("test_privacy_e2e:");
    test_gating();
    test_retention();
    test_encryption();
    test_migration();
    test_activity_log();
    std::puts("test_privacy_e2e: OK");
    return 0;
}

#include "inference_manager.h"
#include "llama_backend.h"
#include "vram_budget.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"
#include "paths.h"

#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <cctype>
#include <filesystem>

// InferenceManager — owns the tiered model pool and arbitrates the ~8 GB VRAM
// budget. Loads the registry from the `models` table, keeps the active Fast
// model resident, streams generate() tokens onto EventBus::tokenStreamed, and
// loads/unloads the Heavy model under the budget (evicting Fast/Vision).

namespace polymath {

namespace {
// Map the schema's role string <-> ModelRole enum.
ModelRole roleFromString(const std::string& s) {
    if (s == "heavy")     return ModelRole::Heavy;
    if (s == "vision")    return ModelRole::Vision;
    if (s == "embedding") return ModelRole::Embedding;
    return ModelRole::Fast;
}
} // namespace

const char* InferenceManager::roleName(ModelRole role) {
    switch (role) {
        case ModelRole::Fast:      return "fast";
        case ModelRole::Heavy:     return "heavy";
        case ModelRole::Vision:    return "vision";
        case ModelRole::Embedding: return "embedding";
    }
    return "fast";
}

InferenceManager::InferenceManager(Database& db, QObject* parent)
    : QObject(parent), db_(db) {
    vram_ = std::make_unique<VramBudget>(/*budgetMiB*/ 8192);
}

InferenceManager::~InferenceManager() = default;

// ---------------------------------------------------------------------------
//  lifecycle
// ---------------------------------------------------------------------------
void InferenceManager::start() {
    PM_INFO("InferenceManager starting (CUDA={})", vram_->cudaAvailable());
    reloadRegistry();

    // Load the active Fast model so the assistant can answer immediately.
    {
        std::lock_guard lk(pool_mtx_);
        if (ensureLoaded(ModelRole::Fast))
            PM_INFO("InferenceManager: Fast model resident");
        else
            PM_WARN("InferenceManager: no Fast model available at startup");
    }
    emitVram();
}

void InferenceManager::stop() {
    std::lock_guard lk(pool_mtx_);
    backends_.clear();
    {
        std::lock_guard rlk(registry_mtx_);
        active_id_.clear();
    }
    heavy_loaded_ = false;
}

// ---------------------------------------------------------------------------
//  registry (mirror of the `models` table)
// ---------------------------------------------------------------------------
void InferenceManager::autoDiscoverModels() {
    namespace fs = std::filesystem;
    const auto root = Paths::instance().models();
    std::error_code ec;
    if (!fs::exists(root, ec)) return;

    auto lc = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    };

    int inserted = 0;
    // models/llm -> fast|heavy (by size hint), models/vlm -> vision (+mmproj),
    // models/embeddings -> embedding.
    for (const char* sub : {"llm", "vlm", "embeddings"}) {
        const auto dir = root / sub;
        if (!fs::exists(dir, ec)) continue;

        std::string mmproj;   // the projector in this dir, if any (for vision)
        for (auto& e : fs::directory_iterator(dir, ec))
            if (lc(e.path().filename().string()).find("mmproj") != std::string::npos)
                mmproj = e.path().string();

        for (auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() != ".gguf") continue;
            const std::string fname = lc(e.path().filename().string());
            if (fname.find("mmproj") != std::string::npos) continue;   // projector

            const std::string subS = sub;
            std::string role = "fast";
            int n_ctx = 8192;
            if (subS == "vlm")              role = "vision";
            else if (subS == "embeddings")  { role = "embedding"; n_ctx = 2048; }
            else if (fname.find("27b") != std::string::npos ||
                     fname.find("32b") != std::string::npos ||
                     fname.find("70b") != std::string::npos)
                role = "heavy";

            const std::string path = e.path().string();
            bool exists = false;
            db_.query("SELECT 1 FROM models WHERE path=?1", {path},
                      [&](const Row&) { exists = true; });
            if (exists) continue;

            const std::string id = e.path().stem().string();
            // is_active=1 only if no other model of this role exists yet.
            db_.exec(
                "INSERT INTO models(id,display_name,path,role,n_ctx,n_gpu_layers,"
                "chat_template,mmproj_path,is_active) VALUES(?1,?1,?2,?3,?4,999,'',?5,"
                "(SELECT CASE WHEN EXISTS(SELECT 1 FROM models WHERE role=?3) THEN 0 ELSE 1 END))",
                {id, path, role, n_ctx, role == "vision" ? mmproj : std::string()});
            ++inserted;
        }
    }
    if (inserted)
        PM_INFO("InferenceManager: auto-registered {} new model(s) from {}",
                inserted, root.string());
}

void InferenceManager::reloadRegistry() {
    autoDiscoverModels();   // pick up any newly-downloaded models on disk

    std::vector<ModelSpec> specs;
    std::unordered_map<int, std::string> active;

    db_.query(
        "SELECT id,display_name,path,role,n_ctx,n_gpu_layers,chat_template,"
        "mmproj_path,is_active FROM models",
        {}, [&](const Row& r) {
            ModelSpec s;
            s.id            = r.text(0);
            s.display_name  = r.text(1);
            s.path          = r.text(2);
            s.role          = roleFromString(r.text(3));
            s.n_ctx         = static_cast<int>(r.i64(4));
            s.n_gpu_layers  = static_cast<int>(r.i64(5));
            s.chat_template = r.text(6);
            s.mmproj_path   = r.text(7);
            const bool is_active = r.i64(8) != 0;
            if (is_active) active[static_cast<int>(s.role)] = s.id;
            specs.push_back(std::move(s));
        });

    size_t count = 0;
    {
        std::lock_guard lk(registry_mtx_);
        registry_ = std::move(specs);
        // Preserve any already-chosen active ids the table didn't mark, but let
        // explicit is_active rows win.
        for (auto& [role, id] : active) active_id_[role] = id;
        // Default each role's active model to the first of that role if unset.
        for (const auto& s : registry_) {
            int rk = static_cast<int>(s.role);
            if (active_id_.find(rk) == active_id_.end())
                active_id_[rk] = s.id;
        }
        count = registry_.size();
    }
    PM_INFO("InferenceManager: registry has {} model(s)", count);
}

std::vector<ModelSpec> InferenceManager::registry() const {
    std::lock_guard lk(registry_mtx_);
    return registry_;
}

const ModelSpec* InferenceManager::specById(const std::string& id) const {
    // caller holds registry_mtx_
    auto it = std::find_if(registry_.begin(), registry_.end(),
                           [&](const ModelSpec& s) { return s.id == id; });
    return it == registry_.end() ? nullptr : &*it;
}

const ModelSpec* InferenceManager::specForRole(ModelRole role) const {
    // caller holds registry_mtx_
    auto ai = active_id_.find(static_cast<int>(role));
    if (ai != active_id_.end()) {
        if (const ModelSpec* s = specById(ai->second)) return s;
    }
    auto it = std::find_if(registry_.begin(), registry_.end(),
                           [&](const ModelSpec& s) { return s.role == role; });
    return it == registry_.end() ? nullptr : &*it;
}

void InferenceManager::setActiveModel(ModelRole role, const std::string& id) {
    {
        std::lock_guard lk(registry_mtx_);
        active_id_[static_cast<int>(role)] = id;
    }
    // Persist the choice so it survives restarts (one active per role).
    db_.exec("UPDATE models SET is_active=0 WHERE role=?1", {std::string(roleName(role))});
    db_.exec("UPDATE models SET is_active=1 WHERE id=?1", {id});

    // If that role is currently resident, hot-swap to the newly-selected model.
    bool changed = false;
    {
        std::lock_guard lk(pool_mtx_);
        if (backendFor(role)) {
            unloadRole(role);
            ensureLoaded(role);
            changed = true;
        }
    }
    if (changed) emitVram();
}

// ---------------------------------------------------------------------------
//  backend pool helpers
// ---------------------------------------------------------------------------
LlamaBackend* InferenceManager::backendFor(ModelRole role) {
    auto it = backends_.find(static_cast<int>(role));
    if (it == backends_.end() || !it->second) return nullptr;
    return static_cast<LlamaBackend*>(it->second.get());
}

bool InferenceManager::loadModel(const ModelSpec& spec_in) {
    ModelSpec spec = spec_in;

    // Decide offload depth under the live budget. n_gpu_layers in the registry
    // is the *desired* cap (999 = all); the VRAM budget may lower it.
    const size_t modelMiB = VramBudget::estimateModelMiB(spec.path, spec.n_ctx);
    // Probe the model's layer count lazily: we don't have it until load, so use
    // the registry cap as the ceiling and let the planner scale it.
    const int desired = spec.n_gpu_layers > 0 ? spec.n_gpu_layers : 999;
    const int planned = vram_->planGpuLayers(modelMiB, desired);
    spec.n_gpu_layers = std::min(desired, planned);

    auto backend = std::make_unique<LlamaBackend>();
    if (!backend->load(spec)) {
        PM_ERROR("InferenceManager: failed to load '{}' ({})", spec.id, roleName(spec.role));
        return false;
    }

    const size_t footprint = backend->vramFootprintMiB();
    vram_->reserve(spec.id, footprint);
    backends_[static_cast<int>(spec.role)] = std::move(backend);

    emit modelStateChanged(QString::fromUtf8(roleName(spec.role)),
                           QString::fromStdString(spec.id), true);
    PM_INFO("InferenceManager: loaded '{}' as {} (~{} MiB, {} layers)",
            spec.id, roleName(spec.role), footprint, spec.n_gpu_layers);
    return true;
}

LlamaBackend* InferenceManager::ensureLoaded(ModelRole role) {
    if (auto* b = backendFor(role)) return b;

    ModelSpec spec;
    {
        std::lock_guard lk(registry_mtx_);
        const ModelSpec* s = specForRole(role);
        if (!s) return nullptr;
        spec = *s;
    }
    if (!loadModel(spec)) return nullptr;
    return backendFor(role);
}

void InferenceManager::unloadRole(ModelRole role) {
    auto it = backends_.find(static_cast<int>(role));
    if (it == backends_.end()) return;

    std::string id;
    if (it->second) {
        id = it->second->spec().id;
        it->second->unload();
    }
    backends_.erase(it);
    if (!id.empty()) vram_->release(id);

    emit modelStateChanged(QString::fromUtf8(roleName(role)),
                           QString::fromStdString(id), false);
    PM_INFO("InferenceManager: unloaded {} ('{}')", roleName(role), id);
}

void InferenceManager::emitVram() {
    const auto mem = vram_->query();
    const int used = static_cast<int>(vram_->reservedMiB());
    const int total = static_cast<int>(mem.totalMiB);
    emit vramChanged(used, total);
    EventBus::instance().publishNotice(
        {"info", "inference",
         QStringLiteral("VRAM %1/%2 MiB").arg(used).arg(total)});
}

// ---------------------------------------------------------------------------
//  generate (async streaming)
// ---------------------------------------------------------------------------
void InferenceManager::generate(const ChatRequest& req) {
    // Hop onto our own thread if called from elsewhere (the agent worker calls
    // this directly). Capture by value; the functor form needs no metatype.
    if (QThread::currentThread() != this->thread()) {
        ChatRequest copy = req;
        QMetaObject::invokeMethod(this, [this, copy]() { runGenerate(copy); },
                                  Qt::QueuedConnection);
        return;
    }
    runGenerate(req);
}

void InferenceManager::runGenerate(const ChatRequest& req) {
    auto& bus = EventBus::instance();
    const QString rid = QString::fromStdString(req.request_id);

    std::lock_guard lk(pool_mtx_);

    // Resolve which backend handles this turn.
    LlamaBackend* backend = nullptr;
    if (!req.model_id.empty()) {
        ModelRole role = ModelRole::Fast;
        {
            std::lock_guard rlk(registry_mtx_);
            if (const ModelSpec* s = specById(req.model_id)) role = s->role;
        }
        // Ensure the *specific* model is the one loaded for its role.
        if (auto* b = backendFor(role); b && b->spec().id == req.model_id) {
            backend = b;
        } else {
            unloadRole(role);
            ModelSpec spec;
            {
                std::lock_guard rlk(registry_mtx_);
                if (const ModelSpec* s = specById(req.model_id)) spec = *s;
            }
            if (!spec.id.empty() && loadModel(spec)) backend = backendFor(role);
        }
    } else {
        // Default: active Fast model (load on demand).
        backend = ensureLoaded(ModelRole::Fast);
    }

    if (!backend) {
        PM_ERROR("InferenceManager::generate: no usable model for req {}", req.request_id);
        bus.publishToken({rid, QStringLiteral("[no model loaded]"), true});
        return;
    }

    // Stream tokens onto the bus. The final (done=true) chunk carries no text.
    backend->generate(req, [&](std::string_view piece, bool done) {
        bus.publishToken({rid, QString::fromUtf8(piece.data(),
                                                 static_cast<int>(piece.size())), done});
    });
}

// ---------------------------------------------------------------------------
//  embed / describeImage (synchronous; call from worker threads)
// ---------------------------------------------------------------------------
Embedding InferenceManager::embed(const std::string& text) {
    std::lock_guard lk(pool_mtx_);
    LlamaBackend* backend = ensureLoaded(ModelRole::Embedding);
    if (!backend) {
        PM_WARN("InferenceManager::embed: no embedding model registered");
        return {};
    }
    return backend->embed(text);
}

std::string InferenceManager::describeImage(const Frame& frame, const std::string& prompt) {
    std::string out;
    {
        std::lock_guard lk(pool_mtx_);
        // Vision needs VRAM; if Heavy is resident it may have evicted Vision, so
        // we load on demand here (budget-aware).
        LlamaBackend* backend = ensureLoaded(ModelRole::Vision);
        if (!backend) {
            PM_WARN("InferenceManager::describeImage: no vision model registered");
            return {};
        }
        out = backend->describeImage(frame, prompt);
    }
    emitVram();
    return out;
}

// ---------------------------------------------------------------------------
//  tiered Heavy control
// ---------------------------------------------------------------------------
void InferenceManager::requestHeavy(bool on) {
    if (on == heavy_loaded_) return;

    std::lock_guard lk(pool_mtx_);
    if (on) {
        // Make room: evict the lighter resident models, then load Heavy with as
        // many layers as the freed budget allows.
        ModelSpec heavy;
        {
            std::lock_guard rlk(registry_mtx_);
            const ModelSpec* s = specForRole(ModelRole::Heavy);
            if (!s) { PM_WARN("requestHeavy: no Heavy model registered"); return; }
            heavy = *s;
        }
        PM_INFO("requestHeavy(on): evicting resident set for '{}'", heavy.id);
        unloadRole(ModelRole::Vision);
        unloadRole(ModelRole::Fast);
        unloadRole(ModelRole::Embedding);

        if (loadModel(heavy)) {
            heavy_loaded_ = true;
        } else {
            PM_ERROR("requestHeavy: Heavy load failed; restoring Fast");
            ensureLoaded(ModelRole::Fast);
        }
    } else {
        PM_INFO("requestHeavy(off): unloading Heavy, restoring Fast");
        unloadRole(ModelRole::Heavy);
        heavy_loaded_ = false;
        ensureLoaded(ModelRole::Fast);
    }
    emitVram();
}

bool InferenceManager::heavyLoaded() const { return heavy_loaded_; }

} // namespace polymath

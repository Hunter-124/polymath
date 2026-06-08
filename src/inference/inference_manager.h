#pragma once
//
// InferenceManager — owns the tiered model pool (Fast resident, Heavy/Vision/
// Embedding on demand) and arbitrates the ~8 GB VRAM budget between them.
//
// CONTRACT (implemented by Wave-1 agent):
//   * Wraps llama.cpp via core::IModelBackend (LlamaBackend).
//   * generate() runs on a worker; streams tokens onto EventBus::tokenStreamed.
//   * requestHeavy(true) evicts/【de-offloads】the resident set to fit a big model,
//     runs queued deep work, then restores Fast on requestHeavy(false).
//
#include "service.h"
#include "i_model_backend.h"
#include "types.h"
#include <QObject>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace polymath {

class Database;
class VramBudget;
class LlamaBackend;

class InferenceManager : public QObject, public IService {
    Q_OBJECT
public:
    explicit InferenceManager(Database& db, QObject* parent = nullptr);
    ~InferenceManager() override;

    void start() override;
    void stop() override;
    const char* serviceName() const override { return "inference"; }

    // Registry (backed by `models` table; editable from the Model Manager UI).
    void reloadRegistry();
    std::vector<ModelSpec> registry() const;
    void setActiveModel(ModelRole role, const std::string& id);

    // Async streaming completion: tokens published on EventBus with req.request_id.
    void generate(const ChatRequest& req);

    // Synchronous helpers (call from worker threads only).
    Embedding   embed(const std::string& text);
    std::string describeImage(const Frame& frame, const std::string& prompt);

    // Tiered control. Loading Heavy may unload Fast/Vision per the VRAM budget.
    void requestHeavy(bool on);
    bool heavyLoaded() const;

signals:
    void modelStateChanged(QString role, QString id, bool loaded);
    void vramChanged(int usedMiB, int totalMiB);

private:
    // Runs the blocking decode loop on the InferenceManager's own thread so the
    // caller (agent worker) is never blocked. Dispatched as a queued functor by
    // generate() — no metatype registration required.
    void runGenerate(const ChatRequest& req);

    // --- internals (all touch the backend pool; call on this thread) ---
    LlamaBackend* backendFor(ModelRole role);     // nullptr if not loaded
    LlamaBackend* ensureLoaded(ModelRole role);   // load-on-demand, budgeted
    bool          loadModel(const ModelSpec& spec);
    void          unloadRole(ModelRole role);
    const ModelSpec* specForRole(ModelRole role) const;   // active spec, or null
    const ModelSpec* specById(const std::string& id) const;
    void          emitVram();
    static const char* roleName(ModelRole role);

    Database& db_;
    std::unique_ptr<VramBudget> vram_;

    // Pool mutation/use is serialized by pool_mtx_: generate() runs on this
    // thread but embed()/describeImage() are called from other service workers.
    std::mutex                                             pool_mtx_;
    std::unordered_map<int /*ModelRole*/, ModelBackendPtr> backends_;

    mutable std::mutex             registry_mtx_;
    std::vector<ModelSpec>         registry_;             // mirror of `models`
    std::unordered_map<int, std::string> active_id_;      // role -> active model id
    bool                           heavy_loaded_ = false;
};

} // namespace polymath

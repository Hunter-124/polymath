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
#include <unordered_map>

namespace polymath {

class Database;
class VramBudget;

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
    Database& db_;
    std::unique_ptr<VramBudget> vram_;
    std::unordered_map<int /*ModelRole*/, ModelBackendPtr> backends_;
};

} // namespace polymath

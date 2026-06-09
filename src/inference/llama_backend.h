#pragma once
//
// LlamaBackend — IModelBackend over llama.cpp for one loaded model.
//
// Pinned API: llama.cpp release ~b4400+ (the post-`llama_sampler` / unified
// `llama_model` + `llama_context` API, vocab accessed via `llama_model_get_vocab`,
// and the `mtmd` multimodal helper for VLM/CLIP projectors). The exact symbols
// most likely to drift between releases are flagged with `// API:` comments.
//
// Responsibilities:
//   * load(): llama_model_load_from_file + llama_init_from_model, honoring
//     spec.n_gpu_layers (already trimmed to the VRAM budget by the manager) and
//     spec.n_ctx; for Embedding role, set ctx params embeddings + pooling.
//   * generate(): apply the model chat template, tokenize the prompt, run a
//     batched decode loop with a sampler chain (top-k/top-p/temp/repeat penalty
//     + optional GBNF grammar), streaming detokenized pieces to the callback.
//   * embed(): mean/last-token pooled embedding via llama_get_embeddings_seq.
//   * describeImage(): mtmd projector + clip to fold a JPEG into the prompt.
//
// One LlamaBackend owns exactly one model+context and is NOT thread-safe; the
// InferenceManager serializes calls into it from a single worker thread.
//
#include "i_model_backend.h"
#include "types.h"

#include <atomic>
#include <memory>
#include <string>

namespace polymath {

class LlamaBackend final : public IModelBackend {
public:
    LlamaBackend();
    ~LlamaBackend() override;

    bool load(const ModelSpec& spec) override;
    void unload() override;
    bool isLoaded() const override;
    const ModelSpec& spec() const override { return spec_; }

    void        generate(const ChatRequest& request, const TokenCallback& on_token) override;
    Embedding   embed(std::string_view text) override;
    std::string describeImage(const Frame& frame, std::string_view prompt) override;

    size_t vramFootprintMiB() const override { return footprint_mib_; }

    // Cooperative cancellation: aborts an in-flight generate() at the next token
    // boundary. Safe to call from another thread.
    void requestStop() { stop_requested_.store(true); }

    // Cheaply read the transformer block (layer) count from a .gguf's metadata
    // header WITHOUT loading the weights, by scanning for the `*.block_count`
    // key. Returns 0 if it can't be determined (caller should fall back). Used by
    // the VRAM budget so partial offload is planned against the real layer count
    // (not a placeholder), which is what makes "Heavy fits the budget" correct.
    static int probeLayerCount(const std::string& path);

private:
    struct Impl;                       // hides all llama.h types from this header
    std::unique_ptr<Impl> d_;

    ModelSpec         spec_;
    bool              loaded_ = false;
    size_t            footprint_mib_ = 0;
    std::atomic<bool> stop_requested_{false};

    // Internal helpers (implemented in llama_backend.cpp).
    std::string applyChatTemplate(const std::vector<ChatMessage>& messages,
                                  bool add_assistant) const;
    bool        loadMultimodal();      // lazy mtmd/clip projector init (VLM)
};

} // namespace polymath

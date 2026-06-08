#pragma once
//
// IModelBackend — abstraction over an inference engine for one loaded model.
// Implemented by inference/LlamaBackend (llama.cpp).  The InferenceManager owns
// a pool of these (one per loaded role) and arbitrates VRAM between them.
//
#include "types.h"
#include <functional>
#include <memory>
#include <string>

namespace polymath {

// Streaming token sink. `done` marks the final call for a request.
using TokenCallback = std::function<void(std::string_view token, bool done)>;

struct IModelBackend {
    virtual ~IModelBackend() = default;

    virtual bool load(const ModelSpec& spec) = 0;
    virtual void unload() = 0;
    virtual bool isLoaded() const = 0;
    virtual const ModelSpec& spec() const = 0;

    // Streaming chat completion. Honors request.sampling (incl. optional GBNF
    // grammar for constrained tool-call JSON). Blocks the calling worker thread.
    virtual void generate(const ChatRequest& request, const TokenCallback& on_token) = 0;

    // Embedding (Embedding-role models). Returns empty on unsupported role.
    virtual Embedding embed(std::string_view text) = 0;

    // Vision-language description (Vision-role models w/ mmproj). prompt guides
    // the answer (e.g. "Is there a set of keys in this frame? Where?").
    virtual std::string describeImage(const Frame& frame, std::string_view prompt) = 0;

    // Approx. VRAM cost of the currently-loaded weights+kv, in MiB (for budgeting).
    virtual size_t vramFootprintMiB() const = 0;
};

using ModelBackendPtr = std::unique_ptr<IModelBackend>;

} // namespace polymath

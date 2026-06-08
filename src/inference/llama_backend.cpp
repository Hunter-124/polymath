#include "llama_backend.h"
#include "logging.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

// llama.cpp is an optional vendored dependency (third_party/llama.cpp). When it
// is not present the scaffold still compiles: every public method degrades to a
// logged no-op so the rest of the pipeline can be exercised. The real engine is
// compiled in when POLYMATH_HAVE_LLAMA is defined by the module CMakeLists.
#ifdef POLYMATH_HAVE_LLAMA
#  include <llama.h>
// Multimodal (VLM) projector helper shipped with recent llama.cpp under tools/
// mtmd. Header availability varies by release/packaging, so it is independently
// guarded; define POLYMATH_HAVE_MTMD to enable describeImage().
#  ifdef POLYMATH_HAVE_MTMD
#    include <mtmd.h>
#  endif
#endif

namespace polymath {

namespace {
constexpr size_t kMiB = 1024ull * 1024ull;
} // namespace

// ===========================================================================
//  Impl — owns the raw llama.cpp handles.
// ===========================================================================
struct LlamaBackend::Impl {
#ifdef POLYMATH_HAVE_LLAMA
    llama_model*   model = nullptr;
    llama_context* ctx   = nullptr;
    const llama_vocab* vocab = nullptr;
#  ifdef POLYMATH_HAVE_MTMD
    mtmd_context*  mtmd = nullptr;
#  endif
#endif
    bool embeddings_mode = false;
};

LlamaBackend::LlamaBackend() : d_(std::make_unique<Impl>()) {}

LlamaBackend::~LlamaBackend() { unload(); }

bool LlamaBackend::isLoaded() const { return loaded_; }

// ---------------------------------------------------------------------------
//  load / unload
// ---------------------------------------------------------------------------
bool LlamaBackend::load(const ModelSpec& spec) {
    unload();
    spec_ = spec;
    d_->embeddings_mode = (spec.role == ModelRole::Embedding);

#ifndef POLYMATH_HAVE_LLAMA
    PM_WARN("LlamaBackend::load('{}'): built without llama.cpp — stub load",
            spec.id);
    loaded_ = true;
    footprint_mib_ = 0;
    return true;
#else
    if (!std::filesystem::exists(spec.path)) {
        PM_ERROR("LlamaBackend: model file not found: {}", spec.path);
        return false;
    }

    // --- model weights ---
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = spec.n_gpu_layers;       // already VRAM-budgeted
    // API: split-mode / main_gpu left at defaults (single-GPU desktop target).

    d_->model = llama_model_load_from_file(spec.path.c_str(), mparams);
    if (!d_->model) {
        PM_ERROR("LlamaBackend: failed to load model {}", spec.path);
        return false;
    }
    d_->vocab = llama_model_get_vocab(d_->model);

    // --- context ---
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx   = static_cast<uint32_t>(std::max(spec.n_ctx, 0));
    cparams.n_batch = std::min<uint32_t>(cparams.n_ctx ? cparams.n_ctx : 2048, 2048);
    cparams.n_threads       = 0;   // 0 => llama picks a sane default
    cparams.n_threads_batch = 0;
    if (d_->embeddings_mode) {
        cparams.embeddings = true;
        // API: pooling type enum name is LLAMA_POOLING_TYPE_MEAN in recent tags.
        cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;
    }

    d_->ctx = llama_init_from_model(d_->model, cparams);
    if (!d_->ctx) {
        PM_ERROR("LlamaBackend: failed to create context for {}", spec.id);
        llama_model_free(d_->model);
        d_->model = nullptr;
        return false;
    }

    // Footprint estimate for the VRAM ledger: weights file size + KV cache.
    std::error_code ec;
    size_t weights = 0;
    if (auto sz = std::filesystem::file_size(spec.path, ec); !ec) weights = sz / kMiB;
    footprint_mib_ = weights + (static_cast<size_t>(cparams.n_ctx) * 128) / 1024;

    loaded_ = true;
    stop_requested_.store(false);
    PM_INFO("LlamaBackend loaded '{}' role={} n_ctx={} ngl={} (~{} MiB)",
            spec.id, static_cast<int>(spec.role), cparams.n_ctx,
            spec.n_gpu_layers, footprint_mib_);
    return true;
#endif
}

void LlamaBackend::unload() {
    if (!loaded_) return;
#ifdef POLYMATH_HAVE_LLAMA
#  ifdef POLYMATH_HAVE_MTMD
    if (d_->mtmd) { mtmd_free(d_->mtmd); d_->mtmd = nullptr; }
#  endif
    if (d_->ctx)   { llama_free(d_->ctx);       d_->ctx = nullptr; }
    if (d_->model) { llama_model_free(d_->model); d_->model = nullptr; }
    d_->vocab = nullptr;
#endif
    loaded_ = false;
    footprint_mib_ = 0;
    PM_DEBUG("LlamaBackend unloaded '{}'", spec_.id);
}

// ---------------------------------------------------------------------------
//  chat template
// ---------------------------------------------------------------------------
std::string LlamaBackend::applyChatTemplate(const std::vector<ChatMessage>& messages,
                                            bool add_assistant) const {
#ifndef POLYMATH_HAVE_LLAMA
    // Minimal fallback formatting when the native template engine is absent.
    std::string out;
    for (const auto& m : messages) {
        const char* tag = m.role == Role::System    ? "system"
                        : m.role == Role::Assistant  ? "assistant"
                        : m.role == Role::Tool       ? "tool" : "user";
        out += tag; out += ": "; out += m.content; out += "\n";
    }
    if (add_assistant) out += "assistant: ";
    return out;
#else
    // Translate to llama_chat_message[] (role strings llama.cpp understands).
    auto roleStr = [](Role r) -> const char* {
        switch (r) {
            case Role::System:    return "system";
            case Role::Assistant: return "assistant";
            case Role::Tool:      return "tool";
            case Role::User:
            default:              return "user";
        }
    };
    std::vector<llama_chat_message> lm;
    lm.reserve(messages.size());
    for (const auto& m : messages)
        lm.push_back({roleStr(m.role), m.content.c_str()});

    // Prefer the spec-provided template, else the model's built-in one.
    const char* tmpl = spec_.chat_template.empty()
                           ? llama_model_chat_template(d_->model, /*name*/ nullptr)
                           : spec_.chat_template.c_str();

    std::vector<char> buf(8192);
    int32_t n = llama_chat_apply_template(tmpl, lm.data(), lm.size(),
                                          add_assistant, buf.data(),
                                          static_cast<int32_t>(buf.size()));
    if (n > static_cast<int32_t>(buf.size())) {
        buf.resize(n);
        n = llama_chat_apply_template(tmpl, lm.data(), lm.size(), add_assistant,
                                      buf.data(), static_cast<int32_t>(buf.size()));
    }
    if (n < 0) {
        PM_WARN("LlamaBackend: chat template apply failed; using raw concat");
        std::string out;
        for (const auto& m : messages) { out += m.content; out += "\n"; }
        return out;
    }
    return std::string(buf.data(), buf.data() + n);
#endif
}

#ifdef POLYMATH_HAVE_LLAMA
namespace {

// Tokenize `text` against the model vocab. add_special adds BOS/EOS as needed.
std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text,
                                  bool add_special) {
    int n = -llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.size()),
                            nullptr, 0, add_special, /*parse_special*/ true);
    std::vector<llama_token> out(n);
    int got = llama_tokenize(vocab, text.c_str(), static_cast<int32_t>(text.size()),
                             out.data(), n, add_special, true);
    out.resize(std::max(got, 0));
    return out;
}

// Detokenize a single token to its UTF-8 piece (handles partial bytes).
std::string tokenToPiece(const llama_vocab* vocab, llama_token tok) {
    char buf[256];
    int n = llama_token_to_piece(vocab, tok, buf, sizeof(buf), 0, /*special*/ false);
    if (n < 0) return {};
    return std::string(buf, buf + n);
}

// Build the sampler chain from SamplingParams (+ optional GBNF grammar).
llama_sampler* buildSampler(const llama_vocab* vocab, const SamplingParams& sp) {
    auto* chain = llama_sampler_chain_init(llama_sampler_chain_default_params());

    if (!sp.grammar.empty()) {
        // Grammar-constrained sampling for tool-call JSON. API: signature is
        // (vocab, grammar_str, grammar_root) in recent releases.
        llama_sampler* g = llama_sampler_init_grammar(vocab, sp.grammar.c_str(), "root");
        if (g) llama_sampler_chain_add(chain, g);
        else   PM_WARN("LlamaBackend: grammar init failed; sampling unconstrained");
    }

    llama_sampler_chain_add(chain,
        llama_sampler_init_penalties(/*last_n*/ 64, sp.repeat_penalty,
                                     /*freq*/ 0.0f, /*present*/ 0.0f));
    if (sp.top_k > 0)
        llama_sampler_chain_add(chain, llama_sampler_init_top_k(sp.top_k));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(sp.top_p, /*min_keep*/ 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(sp.temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    return chain;
}

} // namespace
#endif // POLYMATH_HAVE_LLAMA

// ---------------------------------------------------------------------------
//  generate (streaming)
// ---------------------------------------------------------------------------
void LlamaBackend::generate(const ChatRequest& request, const TokenCallback& on_token) {
    stop_requested_.store(false);

#ifndef POLYMATH_HAVE_LLAMA
    PM_WARN("LlamaBackend::generate: built without llama.cpp — emitting placeholder");
    on_token("[llama.cpp not compiled in]", true);
    return;
#else
    if (!loaded_ || !d_->ctx) { on_token("", true); return; }

    const std::string prompt = applyChatTemplate(request.messages, /*add_assistant*/ true);
    std::vector<llama_token> tokens = tokenize(d_->vocab, prompt, /*add_special*/ true);
    if (tokens.empty()) { on_token("", true); return; }

    const int n_ctx = static_cast<int>(llama_n_ctx(d_->ctx));
    if (static_cast<int>(tokens.size()) >= n_ctx) {
        // Keep the tail that fits — leave room for the response.
        const int keep = std::max(1, n_ctx - 256);
        tokens.erase(tokens.begin(), tokens.end() - keep);
    }

    llama_memory_clear(llama_get_memory(d_->ctx), /*data*/ true);  // fresh KV

    // Decode the prompt in one batch.
    llama_batch batch = llama_batch_get_one(tokens.data(),
                                            static_cast<int32_t>(tokens.size()));
    if (llama_decode(d_->ctx, batch) != 0) {
        PM_ERROR("LlamaBackend: prompt decode failed");
        on_token("", true);
        return;
    }

    llama_sampler* sampler = buildSampler(d_->vocab, request.sampling);
    const int max_tokens = request.sampling.max_tokens > 0
                               ? request.sampling.max_tokens : 1024;
    int n_decoded = 0;

    while (n_decoded < max_tokens && !stop_requested_.load()) {
        llama_token tok = llama_sampler_sample(sampler, d_->ctx, /*idx*/ -1);
        if (llama_vocab_is_eog(d_->vocab, tok)) break;   // end-of-generation

        std::string piece = tokenToPiece(d_->vocab, tok);
        if (!piece.empty()) on_token(piece, /*done*/ false);

        llama_sampler_accept(sampler, tok);

        // Feed the sampled token back in for the next step.
        llama_batch next = llama_batch_get_one(&tok, 1);
        if (llama_decode(d_->ctx, next) != 0) {
            PM_WARN("LlamaBackend: decode step failed at {}", n_decoded);
            break;
        }
        ++n_decoded;
    }

    llama_sampler_free(sampler);
    on_token("", /*done*/ true);
    PM_DEBUG("LlamaBackend: generated {} tokens for req {}", n_decoded,
             request.request_id);
#endif
}

// ---------------------------------------------------------------------------
//  embed
// ---------------------------------------------------------------------------
Embedding LlamaBackend::embed(std::string_view text) {
#ifndef POLYMATH_HAVE_LLAMA
    (void)text;
    return {};
#else
    if (!loaded_ || !d_->ctx || !d_->embeddings_mode) {
        PM_WARN("LlamaBackend::embed called on non-embedding backend '{}'", spec_.id);
        return {};
    }
    std::vector<llama_token> tokens =
        tokenize(d_->vocab, std::string(text), /*add_special*/ true);
    if (tokens.empty()) return {};

    llama_memory_clear(llama_get_memory(d_->ctx), true);
    llama_batch batch = llama_batch_get_one(tokens.data(),
                                            static_cast<int32_t>(tokens.size()));
    if (llama_decode(d_->ctx, batch) != 0) {
        PM_ERROR("LlamaBackend::embed decode failed");
        return {};
    }

    const int n_embd = llama_model_n_embd(d_->model);
    // Pooled (sequence) embedding when pooling is enabled; else last-token.
    const float* emb = llama_get_embeddings_seq(d_->ctx, /*seq_id*/ 0);
    if (!emb) emb = llama_get_embeddings(d_->ctx);
    if (!emb) return {};

    Embedding out(emb, emb + n_embd);
    // L2-normalize so cosine == dot in the HNSW index downstream.
    float norm = 0.0f;
    for (float v : out) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 0.0f) for (float& v : out) v /= norm;
    return out;
#endif
}

// ---------------------------------------------------------------------------
//  describeImage (VLM via mtmd/clip)
// ---------------------------------------------------------------------------
bool LlamaBackend::loadMultimodal() {
#if defined(POLYMATH_HAVE_LLAMA) && defined(POLYMATH_HAVE_MTMD)
    if (d_->mtmd) return true;
    if (spec_.mmproj_path.empty()) {
        PM_WARN("LlamaBackend: vision role '{}' has no mmproj_path", spec_.id);
        return false;
    }
    mtmd_context_params mp = mtmd_context_params_default();
    mp.use_gpu = (spec_.n_gpu_layers != 0);
    d_->mtmd = mtmd_init_from_file(spec_.mmproj_path.c_str(), d_->model, mp);
    if (!d_->mtmd) {
        PM_ERROR("LlamaBackend: failed to init mtmd projector {}", spec_.mmproj_path);
        return false;
    }
    return true;
#else
    return false;
#endif
}

std::string LlamaBackend::describeImage(const Frame& frame, std::string_view prompt) {
#if !defined(POLYMATH_HAVE_LLAMA) || !defined(POLYMATH_HAVE_MTMD)
    (void)frame; (void)prompt;
    PM_WARN("LlamaBackend::describeImage: VLM support not compiled in");
    return {};
#else
    if (!loaded_ || frame.jpeg.empty()) return {};
    if (!loadMultimodal()) return {};

    // Decode the JPEG bytes into an mtmd bitmap (RGB). API: helper name is
    // mtmd_helper_bitmap_init_from_buf in recent tags.
    mtmd_bitmap* bmp = mtmd_helper_bitmap_init_from_buf(
        d_->mtmd, frame.jpeg.data(), frame.jpeg.size());
    if (!bmp) {
        PM_ERROR("LlamaBackend::describeImage: failed to decode frame JPEG");
        return {};
    }

    // The prompt must carry the media marker so mtmd knows where to splice the
    // image embeddings. We wrap it through the chat template.
    std::string user = std::string(mtmd_default_marker()) + "\n" + std::string(prompt);
    std::vector<ChatMessage> msgs = {{Role::User, user}};
    std::string templated = applyChatTemplate(msgs, /*add_assistant*/ true);

    mtmd_input_text it;
    it.text          = templated.c_str();
    it.add_special   = true;
    it.parse_special = true;

    mtmd_input_chunks* chunks = mtmd_input_chunks_init();
    const mtmd_bitmap* bitmaps[] = {bmp};
    if (mtmd_tokenize(d_->mtmd, chunks, &it, bitmaps, 1) != 0) {
        PM_ERROR("LlamaBackend::describeImage: mtmd_tokenize failed");
        mtmd_input_chunks_free(chunks);
        mtmd_bitmap_free(bmp);
        return {};
    }

    llama_memory_clear(llama_get_memory(d_->ctx), true);
    llama_pos n_past = 0;
    if (mtmd_helper_eval_chunks(d_->mtmd, d_->ctx, chunks, /*n_past*/ 0,
                                /*seq_id*/ 0, llama_n_batch(d_->ctx),
                                /*logits_last*/ true, &n_past) != 0) {
        PM_ERROR("LlamaBackend::describeImage: chunk eval failed");
        mtmd_input_chunks_free(chunks);
        mtmd_bitmap_free(bmp);
        return {};
    }
    mtmd_input_chunks_free(chunks);
    mtmd_bitmap_free(bmp);

    // Greedy text decode of the answer (no grammar; short captions).
    SamplingParams sp;
    sp.temperature = 0.2f;
    sp.max_tokens  = 256;
    llama_sampler* sampler = buildSampler(d_->vocab, sp);

    std::string answer;
    for (int i = 0; i < sp.max_tokens && !stop_requested_.load(); ++i) {
        llama_token tok = llama_sampler_sample(sampler, d_->ctx, -1);
        if (llama_vocab_is_eog(d_->vocab, tok)) break;
        answer += tokenToPiece(d_->vocab, tok);
        llama_sampler_accept(sampler, tok);
        llama_batch nb = llama_batch_get_one(&tok, 1);
        if (llama_decode(d_->ctx, nb) != 0) break;
    }
    llama_sampler_free(sampler);
    return answer;
#endif
}

} // namespace polymath

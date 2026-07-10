#include "llama_backend.h"
#include "logging.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

// llama.cpp is an optional vendored dependency (third_party/llama.cpp). When it
// is not present the scaffold still compiles: every public method degrades to a
// logged no-op so the rest of the pipeline can be exercised. The real engine is
// compiled in when POLYMATH_HAVE_LLAMA is defined by the module CMakeLists.
#ifdef POLYMATH_HAVE_LLAMA
#  include <llama.h>
#  include <gguf.h>            // cheap metadata read for probeLayerCount()
// Multimodal (VLM) projector helper shipped with recent llama.cpp under tools/
// mtmd. Header availability varies by release/packaging, so it is independently
// guarded; define POLYMATH_HAVE_MTMD to enable describeImage().
#  ifdef POLYMATH_HAVE_MTMD
#    include <mtmd.h>
#    include <mtmd-helper.h>   // mtmd_helper_bitmap_init_from_buf / eval_chunks
#  endif
#endif

#include <string_view>

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
//  probeLayerCount — read "<arch>.block_count" from the gguf header (no weights)
// ---------------------------------------------------------------------------
int LlamaBackend::probeLayerCount(const std::string& path) {
#ifndef POLYMATH_HAVE_LLAMA
    (void)path;
    return 0;
#else
    gguf_init_params gp{};
    gp.no_alloc = true;          // metadata only — do NOT map the tensor data
    gp.ctx      = nullptr;
    gguf_context* gc = gguf_init_from_file(path.c_str(), gp);
    if (!gc) return 0;

    int layers = 0;
    const int64_t n_kv = gguf_get_n_kv(gc);
    for (int64_t i = 0; i < n_kv; ++i) {
        std::string_view key = gguf_get_key(gc, i);
        // Keys look like "gemma3.block_count", "llama.block_count", etc.
        if (key.size() >= 12 && key.substr(key.size() - 12) == ".block_count") {
            // block_count is stored as an unsigned 32-bit int in practice.
            const gguf_type t = gguf_get_kv_type(gc, i);
            if (t == GGUF_TYPE_UINT32)      layers = static_cast<int>(gguf_get_val_u32(gc, i));
            else if (t == GGUF_TYPE_INT32)  layers = gguf_get_val_i32(gc, i);
            else if (t == GGUF_TYPE_UINT64) layers = static_cast<int>(gguf_get_val_u64(gc, i));
            else if (t == GGUF_TYPE_INT64)  layers = static_cast<int>(gguf_get_val_i64(gc, i));
            break;
        }
    }
    gguf_free(gc);
    return layers;
#endif
}

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
    // Larger batch helps prompt eval; cap so we don't over-allocate on 8 GB machines.
    cparams.n_batch = std::min<uint32_t>(cparams.n_ctx ? cparams.n_ctx : 2048, 512);
    // Pin thread counts: llama's "0 = default" is often too low on Windows. Leave
    // one logical core for the UI / audio pump. 6C/12T → 11 worker threads.
    {
        unsigned hc = std::thread::hardware_concurrency();
        if (hc == 0) hc = 8;
        const int n = static_cast<int>(hc > 2 ? hc - 1 : hc);
        cparams.n_threads       = n;
        cparams.n_threads_batch = n;
        PM_INFO("LlamaBackend: n_threads={} n_batch={}", n, cparams.n_batch);
    }
    // KV-cache quantization (04 §1): type_k/type_v behind llm.kv_quant.
    // Default q8_0 ≈ 50 % KV memory vs fp16 with negligible quality loss here.
    {
        auto kvType = [](const std::string& q) -> ggml_type {
            if (q == "f16" || q == "fp16") return GGML_TYPE_F16;
            if (q == "f32" || q == "fp32") return GGML_TYPE_F32;
            return GGML_TYPE_Q8_0;   // "q8_0" and anything unknown
        };
        const ggml_type kt = kvType(kv_quant_);
        cparams.type_k = kt;
        cparams.type_v = kt;
        PM_INFO("LlamaBackend: KV quant={} (type_k/type_v)", kv_quant_);
    }
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
//  countTokens — llama_tokenize only (no generation)
// ---------------------------------------------------------------------------
int LlamaBackend::countTokens(std::string_view text) const {
    if (!loaded_ || text.empty()) return 0;
#ifndef POLYMATH_HAVE_LLAMA
    return std::max(1, static_cast<int>(text.size()) / 4);
#else
    if (!d_->vocab) return std::max(1, static_cast<int>(text.size()) / 4);
    // First call with null buffer returns the required token count (negated).
    const int n = -llama_tokenize(d_->vocab, text.data(),
                                  static_cast<int32_t>(text.size()),
                                  nullptr, 0,
                                  /*add_special*/ false,
                                  /*parse_special*/ false);
    return std::max(n, 0);
#endif
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

// Build the main sampler chain (penalties/top-k/top-p/temp/dist). The grammar is
// deliberately NOT part of this chain — see Sampler below for why.
llama_sampler* buildMainChain(const SamplingParams& sp) {
    auto* chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
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

// Sampler — owns an optional GBNF grammar sampler alongside the main chain and
// implements the "grammar-checked resample" loop used by llama.cpp's own
// common_sampler. We do NOT splice the grammar into the main chain because that
// path is what crashes on some models (notably gemma-3n): applying the grammar
// over the *full* vocab first, then letting dist pick, can hand back a token the
// grammar does not actually accept (e.g. an EOG token while the grammar is not
// in an accepting state, or — after top-k/top-p reshape — a token whose only
// surviving mass came from a candidate the grammar would reject). Accepting such
// a token empties the grammar stacks, and the *next* llama_grammar apply hits
// `GGML_ASSERT(!stacks.empty())` / `GGML_ABORT("fatal error")`, which on Windows
// surfaces as the 0xC0000409 fast-fail that card B observed.
//
// The safe pattern: sample from the main chain, then *verify* the candidate
// against the grammar on a single-element array. If it survives, accept it into
// both samplers. If the grammar rejects it, re-run the grammar over the full
// distribution and resample with a fresh greedy pick — guaranteeing the accepted
// token is always grammar-legal, so the stacks can never empty unexpectedly.
class Sampler {
public:
    Sampler(const llama_vocab* vocab, const SamplingParams& sp) : vocab_(vocab) {
        main_ = buildMainChain(sp);
        if (!sp.grammar.empty()) {
            grammar_ = llama_sampler_init_grammar(vocab, sp.grammar.c_str(), "root");
            if (!grammar_)
                PM_WARN("LlamaBackend: grammar init failed; sampling unconstrained");
        }
    }
    ~Sampler() {
        if (grammar_) llama_sampler_free(grammar_);
        if (main_)    llama_sampler_free(main_);
    }
    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    // Pick the next token for context `ctx`, honoring the grammar when present.
    llama_token sample(llama_context* ctx) {
        if (!grammar_)
            return llama_sampler_sample(main_, ctx, /*idx*/ -1);

        // Snapshot the logits for this position so we can rebuild the candidate
        // array if the grammar rejects the main chain's first pick.
        const int   n_vocab = llama_vocab_n_tokens(vocab_);
        const float* logits = llama_get_logits_ith(ctx, -1);
        if (!logits) return llama_sampler_sample(main_, ctx, -1);

        // 1) Sample from the main chain (penalties/top-k/top-p/temp/dist).
        llama_token tok = llama_sampler_sample(main_, ctx, -1);

        // 2) Verify it against the grammar in isolation. If the grammar keeps it
        //    (logit not forced to -inf), it is legal — accept and return.
        {
            llama_token_data one{tok, logits[tok], 0.0f};
            llama_token_data_array arr{&one, 1, -1, false};
            llama_sampler_apply(grammar_, &arr);
            if (one.logit > -INFINITY) return tok;
        }

        // 3) The main pick was grammar-illegal (the crash trigger if blindly
        //    accepted). Re-run the grammar over the FULL distribution, then pick
        //    the most-probable surviving token. This always yields a legal token
        //    when the grammar still has any continuation.
        cur_.resize(n_vocab);
        for (int i = 0; i < n_vocab; ++i)
            cur_[i] = llama_token_data{i, logits[i], 0.0f};
        llama_token_data_array arr{cur_.data(), cur_.size(), -1, false};
        llama_sampler_apply(grammar_, &arr);

        llama_token best = tok;
        float best_logit = -INFINITY;
        for (size_t i = 0; i < arr.size; ++i)
            if (arr.data[i].logit > best_logit) {
                best_logit = arr.data[i].logit;
                best = arr.data[i].id;
            }

        // Grammar dead-end: the grammar rejected the ENTIRE vocabulary, so there
        // is no legal continuation. `best` is still the illegal main-chain pick;
        // accepting it would feed an illegal token to the grammar, empty its
        // stacks, and trip GGML_ASSERT(!stacks.empty()) -> GGML_ABORT on the next
        // apply (the 0xC0000409 fast-fail — observed on gemma-3n). Signal
        // end-of-generation instead so the caller breaks BEFORE accepting.
        if (best_logit == -INFINITY)
            return llama_vocab_eos(vocab_);
        return best;
    }

    // Advance both samplers with the chosen token. Never call with an EOG token
    // (the caller breaks on EOG first) — feeding EOG to an unaccepting grammar is
    // exactly the GGML_ABORT path.
    void accept(llama_token tok) {
        if (grammar_) llama_sampler_accept(grammar_, tok);
        llama_sampler_accept(main_, tok);
    }

private:
    const llama_vocab*           vocab_   = nullptr;
    llama_sampler*               main_    = nullptr;
    llama_sampler*               grammar_ = nullptr;
    std::vector<llama_token_data> cur_;
};

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

    Sampler sampler(d_->vocab, request.sampling);
    const int max_tokens = request.sampling.max_tokens > 0
                               ? request.sampling.max_tokens : 1024;
    int n_decoded = 0;

    while (n_decoded < max_tokens && !stop_requested_.load()) {
        llama_token tok = sampler.sample(d_->ctx);
        // Break BEFORE accepting an EOG token: accepting end-of-generation while a
        // grammar is not in an accepting state is the GGML_ABORT crash path.
        if (llama_vocab_is_eog(d_->vocab, tok)) break;   // end-of-generation

        std::string piece = tokenToPiece(d_->vocab, tok);
        if (!piece.empty()) on_token(piece, /*done*/ false);

        sampler.accept(tok);

        // Feed the sampled token back in for the next step.
        llama_batch next = llama_batch_get_one(&tok, 1);
        if (llama_decode(d_->ctx, next) != 0) {
            PM_WARN("LlamaBackend: decode step failed at {}", n_decoded);
            break;
        }
        ++n_decoded;
    }

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
    // mtmd-helper now returns a wrapper {bitmap, video_ctx} and takes a
    // `placeholder` flag. For a still image video_ctx is null; stb_image decodes
    // the JPEG internally.
    mtmd_helper_bitmap_wrapper bmp_wrap = mtmd_helper_bitmap_init_from_buf(
        d_->mtmd, frame.jpeg.data(), frame.jpeg.size(), /*placeholder*/ false);
    mtmd_bitmap* bmp = bmp_wrap.bitmap;
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
    Sampler sampler(d_->vocab, sp);

    std::string answer;
    for (int i = 0; i < sp.max_tokens && !stop_requested_.load(); ++i) {
        llama_token tok = sampler.sample(d_->ctx);
        if (llama_vocab_is_eog(d_->vocab, tok)) break;
        answer += tokenToPiece(d_->vocab, tok);
        sampler.accept(tok);
        llama_batch nb = llama_batch_get_one(&tok, 1);
        if (llama_decode(d_->ctx, nb) != 0) break;
    }
    return answer;
#endif
}

} // namespace polymath

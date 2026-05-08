// grobid_utils.cpp
#include "grobid_utils.h"
#include "vault.h"
#include "markdown.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <tinyxml2.h>
#include <nlohmann/json.hpp>

#include <QProcess>
#include <QThread>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

using json = nlohmann::json;

/* ============================================================
   Globals
   ============================================================ */

static QProcess* g_grobid_process = nullptr;

/* ============================================================
   GROBID Server Management
   ============================================================ */

bool ensure_grobid_running() {
    httplib::Client cli("http://localhost:8070");
    cli.set_read_timeout(2, 0);

    // Check if GROBID is alive
    if (auto res = cli.Get("/api/isalive"); res && res->status == 200) {
        return true;
    }

    if (!g_grobid_process) {
        g_grobid_process = new QProcess();
        g_grobid_process->setProcessChannelMode(QProcess::MergedChannels);

        QStringList args;
        args << "run";
        g_grobid_process->setWorkingDirectory("/Users/tadghk/grobid");
        g_grobid_process->start("./gradlew", args);

        if (!g_grobid_process->waitForStarted(10000)) {
            std::cerr << "Failed to start GROBID via Gradle\n";
            return false;
        }

        QObject::connect(g_grobid_process, &QProcess::readyRead, []() {
            std::cout << g_grobid_process->readAll().toStdString();
        });
    }

    // Wait for server to be alive
    for (int i = 0; i < 60; ++i) {
        if (auto res = cli.Get("/api/isalive"); res && res->status == 200) {
            std::cout << "GROBID is alive!\n";
            return true;
        }
        QThread::sleep(1);
    }

    std::cerr << "GROBID did not respond in time\n";
    return false;
}



std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == ':') {
            escaped << c;
        } else if (c == ' ') {
            escaped << '+';
        } else {
            escaped << '%' << std::uppercase << std::setw(2)
                    << int((unsigned char)c) << std::nouppercase;
        }
    }
    return escaped.str();
}

/* ============================================================
   Paper title normalisation
   ============================================================ */

static std::string norm_unicode_punct(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (size_t i = 0; i < s.size(); ) {
        auto b = [&](size_t j) -> unsigned char { return j < s.size() ? (unsigned char)s[j] : 0; };
        if (b(i) == 0xE2 && b(i+1) == 0x80) {
            switch (b(i+2)) {
                case 0x93: out += '-';    i += 3; continue;  // en dash → -
                case 0x94: out += " - "; i += 3; continue;  // em dash → " - "
                case 0x98: case 0x99: out += '\''; i += 3; continue;  // smart single quotes
                case 0x9C: case 0x9D: out += '"';  i += 3; continue;  // smart double quotes
                case 0xA6: out += "..."; i += 3; continue;  // ellipsis
                case 0xA2: out += '-';   i += 3; continue;  // bullet
            }
        }
        if (b(i) == 0xC2 && b(i+1) == 0xA0) { out += ' '; i += 2; continue; }  // NBSP
        out += s[i++];
    }
    return out;
}

static std::string title_case_word(const std::string& w) {
    if (w.empty()) return w;
    std::string r = w;
    r[0] = (char)std::toupper((unsigned char)r[0]);
    for (size_t i = 1; i < r.size(); ++i)
        r[i] = (char)std::tolower((unsigned char)r[i]);
    return r;
}

static std::string str_to_upper(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = (char)std::toupper((unsigned char)c);
    return r;
}

static const std::unordered_map<std::string, std::string>& get_acronym_map() {
    // Keys are the fully-uppercased, hyphen-stripped form of the word.
    // Values are the canonical display casing.
    static const std::unordered_map<std::string, std::string> m = {
        // ── Transformer / language models ────────────────────────────────────
        {"BERT",          "BERT"},
        {"ROBERTA",       "RoBERTa"},
        {"ALBERT",        "ALBERT"},
        {"XLNET",         "XLNet"},
        {"T5",            "T5"},
        {"BART",          "BART"},
        {"ELECTRA",       "ELECTRA"},
        {"DEBERTA",       "DeBERTa"},
        {"DISTILBERT",    "DistilBERT"},
        {"SPANBERT",      "SpanBERT"},
        {"BIOBERT",       "BioBERT"},
        {"SCIBERT",       "SciBERT"},
        {"CLINICALBERT",  "ClinicalBERT"},
        {"LEGALBERT",     "LegalBERT"},
        {"FINBERT",       "FinBERT"},
        {"SBERT",         "SBERT"},
        {"MPNET",         "MPNet"},
        {"GPT",           "GPT"},
        {"CHATGPT",       "ChatGPT"},
        {"INSTRUCTGPT",   "InstructGPT"},
        {"LLAMA",         "LLaMA"},
        {"LLAMA2",        "LLaMA-2"},
        {"LLAMA3",        "LLaMA-3"},
        {"LLAVA",         "LLaVA"},
        {"MINIGPT4",      "MiniGPT-4"},
        {"ALPACA",        "Alpaca"},
        {"VICUNA",        "Vicuna"},
        {"MISTRAL",       "Mistral"},
        {"MIXTRAL",       "Mixtral"},
        {"FALCON",        "Falcon"},
        {"PALM",          "PaLM"},
        {"PALM2",         "PaLM-2"},
        {"GEMMA",         "Gemma"},
        {"GEMINI",        "Gemini"},
        {"CLAUDE",        "Claude"},
        {"CHINCHILLA",    "Chinchilla"},
        {"GOPHER",        "Gopher"},
        {"BLOOM",         "BLOOM"},
        {"OPT",           "OPT"},
        {"FLAN",          "FLAN"},
        {"ELMO",          "ELMo"},
        {"PHI",           "Phi"},
        {"QWEN",          "Qwen"},
        {"RWKV",          "RWKV"},
        {"MAMBA",         "Mamba"},
        {"RETNET",        "RetNet"},
        {"HYENA",         "Hyena"},
        {"OPENLLAMA",     "OpenLLaMA"},
        {"WORD2VEC",      "Word2Vec"},
        {"FASTTEXT",      "fastText"},
        {"GLOVE",         "GloVe"},
        // ── Vision / multimodal ───────────────────────────────────────────────
        {"VIT",           "ViT"},
        {"DEIT",          "DeiT"},
        {"BEIT",          "BEiT"},
        {"MAE",           "MAE"},
        {"DINO",          "DINO"},
        {"DINOV2",        "DINOv2"},
        {"IJEPA",         "I-JEPA"},
        {"CLIP",          "CLIP"},
        {"BLIP",          "BLIP"},
        {"BLIP2",         "BLIP-2"},
        {"ALIGN",         "ALIGN"},
        {"DALLE",         "DALL-E"},
        {"DALLE2",        "DALL-E 2"},
        {"DALLE3",        "DALL-E 3"},
        {"FLAMINGO",      "Flamingo"},
        {"IMAGEBIND",     "ImageBind"},
        {"IDEFICS",       "IDEFICS"},
        // ── Diffusion / generative models ─────────────────────────────────────
        {"DDPM",          "DDPM"},
        {"DDIM",          "DDIM"},
        {"SDXL",          "SDXL"},
        {"VAE",           "VAE"},
        {"CVAE",          "CVAE"},
        {"VQVAE",         "VQ-VAE"},
        {"GAN",           "GAN"},
        {"DCGAN",         "DCGAN"},
        {"WGAN",          "WGAN"},
        {"CYCLEGAN",      "CycleGAN"},
        {"STYLEGAN",      "StyleGAN"},
        {"BIGGAN",        "BigGAN"},
        {"VQGAN",         "VQGAN"},
        {"STABLEDIFFUSION","Stable Diffusion"},
        // ── Recurrent / CNN architectures ─────────────────────────────────────
        {"LSTM",          "LSTM"},
        {"GRU",           "GRU"},
        {"RNN",           "RNN"},
        {"BILSTM",        "BiLSTM"},
        {"BIGRU",         "BiGRU"},
        {"BRNN",          "BRNN"},
        {"CNN",           "CNN"},
        {"DCNN",          "DCNN"},
        {"RESNET",        "ResNet"},
        {"VGG",           "VGG"},
        {"ALEXNET",       "AlexNet"},
        {"EFFICIENTNET",  "EfficientNet"},
        {"MOBILENET",     "MobileNet"},
        {"DENSENET",      "DenseNet"},
        {"SHUFFLENET",    "ShuffleNet"},
        {"SQUEEZENET",    "SqueezeNet"},
        {"UNET",          "U-Net"},
        // ── Self-supervised / contrastive ─────────────────────────────────────
        {"BYOL",          "BYOL"},
        {"SIMCLR",        "SimCLR"},
        {"MOCO",          "MoCo"},
        {"SIMSIAM",       "SimSiam"},
        {"SWAV",          "SwAV"},
        // ── Efficiency / fine-tuning methods ──────────────────────────────────
        {"LORA",          "LoRA"},
        {"QLORA",         "QLoRA"},
        {"PEFT",          "PEFT"},
        {"BITFIT",        "BitFit"},
        {"IA3",           "IA3"},
        {"DAPT",          "DAPT"},
        {"RLHF",          "RLHF"},
        // ── RL / alignment ────────────────────────────────────────────────────
        {"PPO",           "PPO"},
        {"DPO",           "DPO"},
        {"SFT",           "SFT"},
        {"GRPO",          "GRPO"},
        {"TRPO",          "TRPO"},
        {"A3C",           "A3C"},
        {"A2C",           "A2C"},
        {"DQN",           "DQN"},
        {"SAC",           "SAC"},
        {"TD3",           "TD3"},
        {"DDPG",          "DDPG"},
        {"RAG",           "RAG"},
        {"MOE",           "MoE"},
        {"MOA",           "MoA"},
        {"MDP",           "MDP"},
        {"POMDP",         "POMDP"},
        {"CMDP",          "CMDP"},
        // ── Attention / architecture components ───────────────────────────────
        {"MHA",           "MHA"},
        {"GQA",           "GQA"},
        {"MQA",           "MQA"},
        {"MLA",           "MLA"},
        {"FFN",           "FFN"},
        {"MLP",           "MLP"},
        {"COT",           "CoT"},
        {"ICL",           "ICL"},
        {"KV",            "KV"},
        // ── Activation functions ──────────────────────────────────────────────
        {"RELU",          "ReLU"},
        {"GELU",          "GeLU"},
        {"SILU",          "SiLU"},
        {"ELU",           "ELU"},
        {"PRELU",         "PReLU"},
        {"LEAKYRELU",     "LeakyReLU"},
        {"SWIGLU",        "SwiGLU"},
        {"GEGLU",         "GeGLU"},
        {"GLU",           "GLU"},
        {"MISH",          "Mish"},
        {"SWISH",         "Swish"},
        // ── Normalisation ─────────────────────────────────────────────────────
        {"RMSNORM",       "RMSNorm"},
        {"LAYERNORM",     "LayerNorm"},
        {"GROUPNORM",     "GroupNorm"},
        {"BATCHNORM",     "BatchNorm"},
        // ── Optimisers ────────────────────────────────────────────────────────
        {"SGD",           "SGD"},
        {"ADAM",          "Adam"},
        {"ADAMW",         "AdamW"},
        {"ADAGRAD",       "AdaGrad"},
        {"ADADELTA",      "AdaDelta"},
        {"RMSPROP",       "RMSProp"},
        {"LAMB",          "LAMB"},
        {"LARS",          "LARS"},
        // ── Tokenisation ──────────────────────────────────────────────────────
        {"BPE",           "BPE"},
        {"WORDPIECE",     "WordPiece"},
        {"SENTENCEPIECE", "SentencePiece"},
        // ── NLP tasks / sub-fields ────────────────────────────────────────────
        {"NLP",           "NLP"},
        {"NLU",           "NLU"},
        {"NLG",           "NLG"},
        {"NER",           "NER"},
        {"QA",            "QA"},
        {"MRC",           "MRC"},
        {"MT",            "MT"},
        {"NMT",           "NMT"},
        {"SMT",           "SMT"},
        {"ASR",           "ASR"},
        {"TTS",           "TTS"},
        {"OCR",           "OCR"},
        {"STT",           "STT"},
        {"NLI",           "NLI"},
        {"STS",           "STS"},
        {"SNLI",          "SNLI"},
        {"MNLI",          "MNLI"},
        {"SRL",           "SRL"},
        {"AMR",           "AMR"},
        {"WSD",           "WSD"},
        // ── CV tasks ──────────────────────────────────────────────────────────
        {"VQA",           "VQA"},
        {"DOCVQA",        "DocVQA"},
        {"TEXTVQA",       "TextVQA"},
        {"SCIENCEQA",     "ScienceQA"},
        // ── XAI ───────────────────────────────────────────────────────────────
        {"SHAP",          "SHAP"},
        {"LIME",          "LIME"},
        {"GRADCAM",       "Grad-CAM"},
        // ── Metrics ───────────────────────────────────────────────────────────
        {"BLEU",          "BLEU"},
        {"ROUGE",         "ROUGE"},
        {"METEOR",        "METEOR"},
        {"CIDER",         "CIDEr"},
        {"SPICE",         "SPICE"},
        {"WER",           "WER"},
        {"CER",           "CER"},
        {"MRR",           "MRR"},
        {"NDCG",          "NDCG"},
        {"MAUVE",         "MAUVE"},
        {"BERTSCORE",     "BERTScore"},
        {"BLEURT",        "BLEURT"},
        {"COMET",         "COMET"},
        // ── Datasets / benchmarks ─────────────────────────────────────────────
        {"IMAGENET",      "ImageNet"},
        {"CIFAR",         "CIFAR"},
        {"MNIST",         "MNIST"},
        {"COCO",          "COCO"},
        {"PASCAL",        "PASCAL"},
        {"OPENIMAGES",    "OpenImages"},
        {"SQUAD",         "SQuAD"},
        {"COQA",          "CoQA"},
        {"QUAC",          "QuAC"},
        {"TRIVIAQA",      "TriviaQA"},
        {"HOTPOTQA",      "HotpotQA"},
        {"NATURALQA",     "NaturalQA"},
        {"GLUE",          "GLUE"},
        {"SUPERGLUE",     "SuperGLUE"},
        {"MMLU",          "MMLU"},
        {"HELLASWAG",     "HellaSwag"},
        {"WINOGRANDE",    "WinoGrande"},
        {"ARC",           "ARC"},
        {"TRUTHFULQA",    "TruthfulQA"},
        {"GSM8K",         "GSM8K"},
        {"HUMANEVAL",     "HumanEval"},
        {"MBPP",          "MBPP"},
        {"WIKITEXT",      "WikiText"},
        {"COMMONCRAWL",   "CommonCrawl"},
        {"OPENWEBTEXT",   "OpenWebText"},
        {"REDPAJAMA",     "RedPajama"},
        {"DOLMA",         "Dolma"},
        {"FINEWEB",       "FineWeb"},
        // ── General AI/ML abbreviations ───────────────────────────────────────
        {"AI",            "AI"},
        {"ML",            "ML"},
        {"DL",            "DL"},
        {"AGI",           "AGI"},
        {"LLM",           "LLM"},
        {"SLM",           "SLM"},
        {"VLM",           "VLM"},
        {"MLLM",          "MLLM"},
        // ── Hardware ──────────────────────────────────────────────────────────
        {"CPU",           "CPU"},
        {"GPU",           "GPU"},
        {"TPU",           "TPU"},
        {"NPU",           "NPU"},
        {"FPGA",          "FPGA"},
        {"ASIC",          "ASIC"},
        {"SOC",           "SoC"},
        {"CUDA",          "CUDA"},
        {"OPENCL",        "OpenCL"},
        {"OPENGL",        "OpenGL"},
        {"DIRECTX",       "DirectX"},
        {"MPI",           "MPI"},
        {"SIMD",          "SIMD"},
        {"AVX",           "AVX"},
        {"SSE",           "SSE"},
        {"VRAM",          "VRAM"},
        {"DRAM",          "DRAM"},
        // ── Systems / networking ──────────────────────────────────────────────
        {"API",           "API"},
        {"SDK",           "SDK"},
        {"CLI",           "CLI"},
        {"GUI",           "GUI"},
        {"IDE",           "IDE"},
        {"HTTP",          "HTTP"},
        {"HTTPS",         "HTTPS"},
        {"REST",          "REST"},
        {"GRPC",          "gRPC"},
        {"TCP",           "TCP"},
        {"UDP",           "UDP"},
        {"DNS",           "DNS"},
        {"TLS",           "TLS"},
        {"SSL",           "SSL"},
        {"OAUTH",         "OAuth"},
        {"JWT",           "JWT"},
        {"SQL",           "SQL"},
        {"NOSQL",         "NoSQL"},
        {"JSON",          "JSON"},
        {"XML",           "XML"},
        {"YAML",          "YAML"},
        {"HTML",          "HTML"},
        {"CSS",           "CSS"},
        {"PDF",           "PDF"},
        {"DOI",           "DOI"},
        {"URL",           "URL"},
        {"URI",           "URI"},
        {"OS",            "OS"},
        {"JVM",           "JVM"},
        {"JIT",           "JIT"},
        {"LLVM",          "LLVM"},
        {"GCC",           "GCC"},
        // ── Frameworks / tools ────────────────────────────────────────────────
        {"PYTORCH",       "PyTorch"},
        {"TENSORFLOW",    "TensorFlow"},
        {"JAX",           "JAX"},
        {"NUMPY",         "NumPy"},
        {"SCIPY",         "SciPy"},
        {"OPENAI",        "OpenAI"},
        {"DEEPMIND",      "DeepMind"},
        {"GITHUB",        "GitHub"},
        {"ARXIV",         "arXiv"},
        {"GROBID",        "GROBID"},
        // ── Quantisation / precision ──────────────────────────────────────────
        {"INT8",          "INT8"},
        {"INT4",          "INT4"},
        {"FP16",          "FP16"},
        {"FP32",          "FP32"},
        {"BF16",          "BF16"},
        {"GPTQ",          "GPTQ"},
        {"AWQ",           "AWQ"},
        // ── Languages / formats with non-standard casing ──────────────────────
        {"JAVASCRIPT",    "JavaScript"},
        {"TYPESCRIPT",    "TypeScript"},
        {"WEBASSEMBLY",   "WebAssembly"},
        {"WASM",          "WASM"},
        {"MATLAB",        "MATLAB"},
        {"LATEX",         "LaTeX"},
        {"BIBTEX",        "BibTeX"},
    };
    return m;
}

// Determine casing for a single whitespace-delimited token (may contain hyphens).
static std::string apply_word_casing(
    const std::string& word,
    bool force_cap,
    const std::unordered_map<std::string, std::string>& amap,
    const std::unordered_set<std::string>& small_words)
{
    if (word.empty()) return word;

    // Peel leading and trailing non-alphanumeric, non-hyphen punctuation.
    size_t ls = 0;
    while (ls < word.size() && !std::isalnum((unsigned char)word[ls]) && word[ls] != '-')
        ++ls;
    size_t le = word.size();
    while (le > ls && !std::isalnum((unsigned char)word[le-1]) && word[le-1] != '-')
        --le;

    if (ls >= le) return word;  // pure punctuation token

    std::string lead  = word.substr(0, ls);
    std::string core  = word.substr(ls, le - ls);
    std::string trail = word.substr(le);

    // Build lookup keys.
    std::string key_full = str_to_upper(core);
    std::string key_nodash;
    for (char c : core) if (c != '-') key_nodash += (char)std::toupper((unsigned char)c);

    // 1. Full token (hyphens included) in map — e.g. "DALL-E" stored as "DALLE".
    {
        auto it = amap.find(key_nodash);
        if (it != amap.end()) return lead + it->second + trail;
    }

    // 2. Plural of a known acronym: strip trailing 's', look up root.
    if (key_nodash.size() > 1 && key_nodash.back() == 'S') {
        std::string singular = key_nodash.substr(0, key_nodash.size() - 1);
        auto it = amap.find(singular);
        if (it != amap.end()) return lead + it->second + "s" + trail;
    }

    // 3. Hyphenated compound: split, process each part, rejoin.
    size_t dash = core.find('-');
    if (dash != std::string::npos) {
        std::string left  = apply_word_casing(core.substr(0, dash),  force_cap, amap, small_words);
        std::string right = apply_word_casing(core.substr(dash + 1), true,      amap, small_words);
        return lead + left + "-" + right + trail;
    }

    // 4. Small word (lower unless it is force-capitalised).
    std::string lower_core;
    for (char c : core) lower_core += (char)std::tolower((unsigned char)c);
    if (!force_cap && small_words.count(lower_core))
        return lead + lower_core + trail;

    // 5. Default: Title Case.
    return lead + title_case_word(core) + trail;
}

std::string normalize_paper_title(const std::string& raw) {
    if (raw.empty()) return raw;

    // Replace Unicode punctuation with plain ASCII equivalents.
    std::string s = norm_unicode_punct(raw);

    // Collapse whitespace and trim.
    std::string collapsed;
    collapsed.reserve(s.size());
    bool prev_space = true;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!prev_space) { collapsed += ' '; prev_space = true; }
        } else {
            collapsed += (char)c;
            prev_space = false;
        }
    }
    if (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();

    // Strip trailing sentence-ending punctuation (.  :  ;).
    while (!collapsed.empty() &&
           (collapsed.back() == '.' || collapsed.back() == ':' || collapsed.back() == ';'))
        collapsed.pop_back();
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();

    if (collapsed.empty()) return raw;

    static const std::unordered_set<std::string> small_words = {
        "a", "an", "the",
        "and", "but", "or", "nor", "for", "yet", "so",
        "at", "by", "in", "of", "on", "to", "up", "as", "via", "per", "vs"
    };
    const auto& amap = get_acronym_map();

    std::vector<std::string> tokens;
    {
        std::istringstream ss(collapsed);
        std::string tok;
        while (ss >> tok) tokens.push_back(tok);
    }

    std::string result;
    bool force_next = true;  // always capitalise first word

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) result += ' ';
        const std::string& tok = tokens[i];
        result += apply_word_casing(tok, force_next, amap, small_words);
        // Capitalise the word following a colon or a standalone dash (from em-dash).
        force_next = (!tok.empty() && tok.back() == ':') || tok == "-";
    }

    return result;
}

std::string sanitize_for_filename(const std::string& title) {
    std::string result;
    for (char c : title) {
        if (c == ':') {
            result += " -";
        } else if (c == '/' || c == '\\' || c == '*' ||
                   c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            result += ' ';
        } else {
            result += c;
        }
    }

    // Trim leading/trailing spaces
    size_t start = result.find_first_not_of(' ');
    size_t end = result.find_last_not_of(' ');
    if (start == std::string::npos) return "untitled";
    result = result.substr(start, end - start + 1);

    // Limit length
    if (result.size() > 200) result.resize(200);
    return result.empty() ? "untitled" : result;
}

std::string normalize_title(const std::string& title) {
    std::string res;
    for (char c : title) {
        if (isalnum((unsigned char)c) || isspace((unsigned char)c))
            res += std::tolower(c);
    }
    if (res.size() > 100) res.resize(100);
    return res;
}

std::string to_lower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return res;
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
}

std::string normalize_for_matching(const std::string& s) {
    std::string tmp = s;

    replace_all(tmp, "-", "");
    replace_all(tmp, "–", "");
    replace_all(tmp, "—", "");

    std::string res;
    for (unsigned char c : tmp) {
        if (std::isalnum(c) || std::isspace(c)) {
            res += std::tolower(c);
        }
    }
    return res;
}

std::string remove_punct_except_apostrophes(const std::string& s) {
    std::string res;
    std::vector<std::string> apostrophes = {"'", "'", "´", "`"};

    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = s[i];

        if (std::isalnum(c) || std::isspace(c)) {
            res += c;
        } else {
            bool is_apostrophe = false;
            for (auto& ap : apostrophes) {
                if (s.compare(i, ap.size(), ap) == 0) {
                    res += ap;
                    i += ap.size() - 1;
                    is_apostrophe = true;
                    break;
                }
            }
        }
    }
    return res;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

std::vector<std::string> filter_words(const std::string& text) {
    std::set<std::string> stop = {
        "the", "a", "an", "of", "for", "and", "or", "on", "in", "to", "with",
        "by", "from", "as", "at", "via", "is", "are"
    };
    std::vector<std::string> result;
    std::vector<std::string> apostrophes = {"'", "'", "´", "`"};

    std::stringstream ss(text);
    std::string word;
    while (ss >> word) {
        bool has_apostrophe = false;
        for (auto& ap : apostrophes) {
            if (word.find(ap) != std::string::npos) {
                has_apostrophe = true;
                break;
            }
        }

        if (!has_apostrophe && word.size() > 2 &&
            stop.find(to_lower(word)) == stop.end()) {
            result.push_back(word);
        }

        if (result.size() >= 8) break;
    }
    return result;
}

std::string build_query(const std::vector<std::string>& words) {
    std::string query;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) query += " OR ";
        query += "ti:" + words[i];
    }
    return query;
}

double word_overlap_similarity(const std::string& a, const std::string& b) {
    std::string norm_a = normalize_for_matching(a);
    std::string norm_b = normalize_for_matching(b);

    std::istringstream iss_a(norm_a), iss_b(norm_b);
    std::set<std::string> words_a, words_b;
    std::string w;
    while (iss_a >> w) words_a.insert(w);
    while (iss_b >> w) words_b.insert(w);

    if (words_a.empty() || words_b.empty()) return 0.0;

    size_t matches = 0;
    for (const auto& word : words_a) {
        if (words_b.count(word)) matches++;
    }

    return static_cast<double>(matches) / std::max(words_a.size(), words_b.size());
}

/* ============================================================
   DOI Lookups
   ============================================================ */

std::string normalize_arxiv_from_text(const std::string& text) {
    std::regex re(R"(arXiv:(\d{4}\.\d{4,5}))");
    std::smatch m;
    if (std::regex_search(text, m, re)) {
        return "10.48550/arXiv." + m[1].str();
    }
    return "";
}

std::string query_crossref_for_doi(const std::string& title) {
    if (title.empty()) return "";

    httplib::SSLClient cli("api.crossref.org");
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);

    std::string url = "/works?query.title=" + url_encode(title) +
                      "&rows=1&mailto=you@example.com";

    auto res = cli.Get(url.c_str());
    if (!res || res->status != 200) return "";

    try {
        auto j = json::parse(res->body);
        auto& items = j["message"]["items"];
        if (!items.empty() && items[0].contains("DOI")) {
            return items[0]["DOI"].get<std::string>();
        }
    } catch (...) {
        return "";
    }

    return "";
}

std::string query_arxiv_for_doi(const std::string& title) {
    if (title.empty()) return "";

    std::string norm_title = title;
    replace_all(norm_title, "–", "-");
    replace_all(norm_title, "—", "-");

    auto split_dash = split(norm_title, '-');
    std::string temp_clean;
    for (auto& part : split_dash) {
        temp_clean += remove_punct_except_apostrophes(part) + " ";
    }
    if (!temp_clean.empty()) {
        temp_clean.pop_back();  // Remove trailing space
    }
    std::string clean_for_match = to_lower(temp_clean);

    auto words = filter_words(temp_clean);
    if (words.empty()) return "";

    std::string query = build_query(words);
    std::string encoded_query = url_encode(query);
    std::string url = "/api/query?search_query=" + encoded_query + "&max_results=5";

    httplib::SSLClient cli("export.arxiv.org", 443);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);
    auto res = cli.Get(url.c_str());
    if (!res || res->status != 200) return "";

    tinyxml2::XMLDocument doc;
    if (doc.Parse(res->body.c_str()) != tinyxml2::XML_SUCCESS) return "";

    auto* feed = doc.FirstChildElement("feed");
    if (!feed) return "";

    std::vector<std::string> matches;
    std::vector<std::string> titles;

    for (auto* entry = feed->FirstChildElement("entry");
         entry;
         entry = entry->NextSiblingElement("entry")) {
        auto* id_el = entry->FirstChildElement("id");
        auto* title_el = entry->FirstChildElement("title");
        if (id_el && title_el && id_el->GetText() && title_el->GetText()) {
            matches.push_back(id_el->GetText());
            titles.push_back(title_el->GetText());
        }
    }

    for (size_t i = 0; i < matches.size(); ++i) {
        std::string resp_title = to_lower(remove_punct_except_apostrophes(titles[i]));
        double score = word_overlap_similarity(clean_for_match, resp_title);

        if (score > 0.6) {
            std::regex re(R"(arxiv\.org/abs/([\d.]+)v?\d*)");
            std::smatch m;
            if (std::regex_search(matches[i], m, re)) {
                return "https://arxiv.org/abs/" + m[1].str();
            }
        }
    }

    return "";
}

std::string read_file_binary(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}



std::vector<std::string> extract_bibl_titles(const std::string& tei_xml) {
    std::vector<std::string> titles;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(tei_xml.c_str()) != tinyxml2::XML_SUCCESS) {
        return titles;
    }

    auto* root = doc.RootElement();
    if (!root) return titles;

    // Navigate to listBibl: text/back/div/listBibl or directly under root
    auto* text = root->FirstChildElement("text");
    tinyxml2::XMLElement* listBibl = nullptr;

    if (text) {
        auto* back = text->FirstChildElement("back");
        if (back) {
            auto* div = back->FirstChildElement("div");
            if (div) {
                listBibl = div->FirstChildElement("listBibl");
            }
        }
    }

    // If not found in standard location, try directly under root
    if (!listBibl) {
        listBibl = root->FirstChildElement("listBibl");
    }

    if (!listBibl) return titles;

    for (auto* bibl = listBibl->FirstChildElement("biblStruct");
         bibl;
         bibl = bibl->NextSiblingElement("biblStruct")) {

        // Try analytic/title first (for articles)
        auto* analytic = bibl->FirstChildElement("analytic");
        if (analytic) {
            auto* title = analytic->FirstChildElement("title");
            if (title && title->GetText()) {
                titles.push_back(normalize_paper_title(title->GetText()));
                continue;
            }
        }

        // Try monogr/title (for books)
        auto* monogr = bibl->FirstChildElement("monogr");
        if (monogr) {
            auto* title = monogr->FirstChildElement("title");
            if (title && title->GetText()) {
                titles.push_back(normalize_paper_title(title->GetText()));
                continue;
            }
        }

        // Fallback: collect all text
        std::ostringstream text_content;
        for (auto* el = bibl->FirstChildElement(); el; el = el->NextSiblingElement()) {
            if (el->GetText()) {
                text_content << el->GetText() << " ";
            }
        }
        std::string s = text_content.str();
        if (!s.empty()) {
            titles.push_back(normalize_paper_title(s));
        }
    }

    return titles;
}

std::vector<std::string> extract_pdf_references_with_grobid(const std::string& pdf_path) {
    std::vector<std::string> refs;
    if (!ensure_grobid_running()) return refs;

    std::string pdf_data = read_file_binary(pdf_path);

    httplib::UploadFormDataItems items = {
        {
            "input",                                 // field name
            pdf_data,                                // file content
            fs::path(pdf_path).filename().string(),  // filename
            "application/pdf"                        // content type
        }
    };
    httplib::Client cli("http://localhost:8070");
    cli.set_read_timeout(60, 0);

    auto res = cli.Post("/api/processReferences", items);
    if (!res || res->status != 200) {
        std::cerr << "Grobid failed to process references\n";
        return refs;
    }

    refs = extract_bibl_titles(res->body);
    return refs;
}

std::string get_paper_title_with_grobid(const std::string& pdf_path) {
    if (!ensure_grobid_running()) return "";

    std::string pdf_data = read_file_binary(pdf_path);
    httplib::UploadFormDataItems items = {
        {
            "input",
            pdf_data,
            fs::path(pdf_path).filename().string(),
            "application/pdf"
        }
    };

    httplib::Client cli("http://localhost:8070");
    cli.set_read_timeout(30, 0);

    auto res = cli.Post("/api/processHeaderDocument", items);
    if (!res || res->status != 200) {
        std::cerr << "Grobid header extraction failed\n";
        return "";
    }

    tinyxml2::XMLDocument doc;
    if (doc.Parse(res->body.c_str()) != tinyxml2::XML_SUCCESS) return "";

    auto* root = doc.RootElement();
    if (!root) return "";

    auto* teiHeader = root->FirstChildElement("teiHeader");
    if (!teiHeader) return "";

    auto* fileDesc = teiHeader->FirstChildElement("fileDesc");
    if (!fileDesc) return "";

    auto* titleStmt = fileDesc->FirstChildElement("titleStmt");
    if (!titleStmt) return "";

    auto* title_el = titleStmt->FirstChildElement("title");
    if (!title_el || !title_el->GetText()) return "";

    return normalize_paper_title(title_el->GetText());
}

void process_pdf_references_standalone(const std::string& pdf_path, const std::string& vault_config_path) {
    if (pdf_path.empty() || !fs::exists(pdf_path)) {
        std::cerr << "Invalid PDF path: " << pdf_path << "\n";
        return;
    }

    std::cout << "Processing: " << pdf_path << "\n";

    // Get paper title
    std::string paper_title = get_paper_title_with_grobid(pdf_path);
    if (paper_title.empty()) {
        std::cerr << "Could not extract paper title, using PDF filename instead\n";
        paper_title = normalize_paper_title(fs::path(pdf_path).stem().string());
    }
    std::cout << "Title: " << paper_title << "\n";

    // Get DOI
    std::string doi = query_arxiv_for_doi(paper_title);
    if (doi.empty()) doi = query_crossref_for_doi(paper_title);
    if (!doi.empty()) std::cout << "DOI: " << doi << "\n";

    // Extract references
    std::vector<std::string> references = extract_pdf_references_with_grobid(pdf_path);
    std::cout << "Found " << references.size() << " references\n";

    // Look up DOIs for references
    std::vector<std::string> reference_dois;
    for (const auto& ref : references) {
        std::string ref_doi = normalize_arxiv_from_text(ref);
        if (ref_doi.empty()) ref_doi = query_arxiv_for_doi(ref);
        if (ref_doi.empty()) ref_doi = query_crossref_for_doi(ref);
        reference_dois.push_back(ref_doi);
    }

    // Load vault config and create markdown file path
    ObsidianConfig cfg = load_vault_config(vault_config_path);
    std::string safe_title = sanitize_for_filename(paper_title) + ".md";

    fs::path md_path = safe_title;
    if (!cfg.vault_path.empty()) {
        fs::create_directories(cfg.vault_path);
        md_path = fs::path(cfg.vault_path) / safe_title;
    }

    std::cout << "Creating markdown: " << md_path.string() << "\n";

    // Create and populate markdown file
    MarkdownFile md(md_path.string(), paper_title);

    md.add_alias(paper_title);
    if (!doi.empty()) md.add_alias(doi);

    md.add_references(references, reference_dois);

    md.save_no_open();

    std::cout << "Done: " << md_path.string() << "\n";
}

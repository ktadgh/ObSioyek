// grobid_utils.cpp
#include "grobid_utils.h"

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
    cli.set_read_timeout(10, 0);

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
    cli.set_read_timeout(10, 0);
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
                titles.push_back(title->GetText());
                continue;
            }
        }

        // Try monogr/title (for books)
        auto* monogr = bibl->FirstChildElement("monogr");
        if (monogr) {
            auto* title = monogr->FirstChildElement("title");
            if (title && title->GetText()) {
                titles.push_back(title->GetText());
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
            titles.push_back(s);
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

    return title_el->GetText();
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
        paper_title = fs::path(pdf_path).stem().string();
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

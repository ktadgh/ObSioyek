#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <unordered_map>
#include "grobid_utils.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT

#include <httplib.h>
#include <tinyxml2.h>
#include <nlohmann/json.hpp>

#include <QProcess>
#include <QThread>

#include <string>
#include <set>
#include <algorithm>
#include <cctype>
#include "markdown.h" 

namespace fs = std::filesystem;
using json = nlohmann::json;

/* ============================================================
   Globals
   ============================================================ */

static QProcess* g_grobid_process = nullptr; // static here ensures it only exists in this file


// Helper: simple URL encode
std::string url_encode(const std::string &value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == ':' ) {
            escaped << c;
        } else if (c == ' ') {
            escaped << '+';
        } else {
            escaped << '%' << std::uppercase << std::setw(2) << int((unsigned char)c) << std::nouppercase;
        }
    }
    return escaped.str();
}

std::string read_file_binary(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// running grobid
bool ensure_grobid_running() {
    httplib::Client cli("http://localhost:8070");
    cli.set_read_timeout(2, 0);

    // check if Grobid is alive
    if (auto res = cli.Get("/api/isalive"); res && res->status == 200) {
        return true;
    }

    if (!g_grobid_process) {
        g_grobid_process = new QProcess();
        g_grobid_process->setProcessChannelMode(QProcess::MergedChannels);

        QStringList args;
        args << "run";  // Gradle task
        g_grobid_process->setWorkingDirectory("/Users/tadghk/grobid"); 
        g_grobid_process->start("./gradlew", args);

        if (!g_grobid_process->waitForStarted(10000)) { // wait 10s for Gradle to launch
            std::cerr << "Failed to start Grobid via Gradle\n";
            return false;
        }
        QObject::connect(g_grobid_process, &QProcess::readyRead, []() {
            std::cout << g_grobid_process->readAll().toStdString();
        });
    }

    // wait for server to be alive
    for (int i = 0; i < 60; ++i) {
        if (auto res = cli.Get("/api/isalive"); res && res->status == 200) {
            std::cout << "Grobid is alive!\n";
            return true;
        }
        QThread::sleep(1);
    }

    std::cerr << "Grobid did not respond in time\n";
    return false;
}

// querying crossref
std::string query_crossref_for_doi(const std::string& title) {
    if (title.empty()) return "";

    httplib::SSLClient cli("api.crossref.org"); // HTTPS version
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

// getting doi from text
std::string normalize_arxiv_from_text(const std::string& text) {
    std::regex re(R"(arXiv:(\d{4}\.\d{4,5}))");
    std::smatch m;
    if (std::regex_search(text, m, re)) {
        return "10.48550/arXiv." + m[1].str();
    }
    return "";
}


std::string extract_paper_title_from_tei(const std::string& tei_xml) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(tei_xml.c_str()) != tinyxml2::XML_SUCCESS) {
        return "";
    }

    auto* root = doc.RootElement();
    if (!root) return "";

    // Navigate to title: teiHeader/fileDesc/titleStmt/title
    auto* teiHeader = root->FirstChildElement("teiHeader");
    if (!teiHeader) return "";

    auto* fileDesc = teiHeader->FirstChildElement("fileDesc");
    if (!fileDesc) return "";

    auto* titleStmt = fileDesc->FirstChildElement("titleStmt");
    if (!titleStmt) return "";

    auto* title = titleStmt->FirstChildElement("title");
    if (title && title->GetText()) {
        return title->GetText();
    }

    return "";
}

std::string sanitize_for_filename(const std::string& title) {
    std::string result;
    for (char c : title) {
        // Replace invalid filename characters
        if (c == ':') {
            result += " -";  // Replace colon with " -" for readability
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

std::string extract_paper_title_with_grobid(const std::string& pdf_path) {
    if (!fs::exists(pdf_path)) {
        return "";
    }

    if (!ensure_grobid_running()) {
        return "";
    }

    std::string pdf_data = read_file_binary(pdf_path);

    httplib::Client cli("http://localhost:8070");
    cli.set_read_timeout(60, 0);

    httplib::UploadFormDataItems items = {
        {
            "input",
            pdf_data,
            fs::path(pdf_path).filename().string(),
            "application/pdf"
        }
    };

    auto res = cli.Post("/api/processHeaderDocument", httplib::Headers{}, items);

    if (!res || res->status != 200) {
        std::cerr << "GROBID processHeaderDocument failed\n";
        return "";
    }

    return extract_paper_title_from_tei(res->body);
}

std::vector<std::string> extract_bibl_titles(const std::string& tei_xml) {
    std::vector<std::string> results;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(tei_xml.c_str()) != tinyxml2::XML_SUCCESS) {
        return results;
    }

    auto* root = doc.RootElement();
    if (!root) return results;

    // Navigate to listBibl
    auto* listBibl = root->FirstChildElement("text")
                            ->FirstChildElement("back")
                            ->FirstChildElement("div")
                            ->FirstChildElement("listBibl");
    if (!listBibl) return results;

    for (auto* bibl = listBibl->FirstChildElement("biblStruct"); bibl; bibl = bibl->NextSiblingElement("biblStruct")) {
        
        // Usually the title is inside <analytic><title> for articles
        auto* analytic = bibl->FirstChildElement("analytic");
        if (analytic) {
            auto* title = analytic->FirstChildElement("title");
            if (title && title->GetText()) {
                results.push_back(title->GetText());
                continue; // done with this biblStruct
            }
        }

        // If <analytic> not found, maybe it's a book: <monogr><title>
        auto* monogr = bibl->FirstChildElement("monogr");
        if (monogr) {
            auto* title = monogr->FirstChildElement("title");
            if (title && title->GetText()) {
                results.push_back(title->GetText());
                continue;
            }
        }

        // fallback: just concatenate everything
        std::ostringstream text;
        for (auto* el = bibl->FirstChildElement(); el; el = el->NextSiblingElement()) {
            if (el->GetText()) {
                text << el->GetText() << " ";
            }
        }
        std::string s = text.str();
        if (!s.empty()) results.push_back(s);
    }

    return results;
}

// getting bibliography from text
std::vector<std::string> extract_biblstruct_text(const std::string& tei_xml) {
    std::vector<std::string> results;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(tei_xml.c_str()) != tinyxml2::XML_SUCCESS) {
        return results;
    }

    auto* root = doc.RootElement();
    if (!root) return results;

    for (auto* bibl = root->FirstChildElement("text")
                              ->FirstChildElement("back")
                              ->FirstChildElement("div")
                              ->FirstChildElement("listBibl")
                              ->FirstChildElement("biblStruct");
         bibl;
         bibl = bibl->NextSiblingElement("biblStruct")) {

        std::ostringstream text;

        for (auto* el = bibl->FirstChildElement(); el; el = el->NextSiblingElement()) {
            if (el->GetText()) {
                text << el->GetText() << " ";
            }
        }

        std::string s = text.str();
        if (!s.empty()) results.push_back(s);
    }

    return results;
}


// get references from a pdf
void extract_pdf_references_with_grobid(const std::string& pdf_path) {
    if (!fs::exists(pdf_path)) {
        std::cerr << "PDF does not exist\n";
        return;
    }

    if (!ensure_grobid_running()) {
        return;
    }

    // First, extract the paper title using GROBID
    std::string paper_title = extract_paper_title_with_grobid(pdf_path);
    std::string md_filename;
    if (!paper_title.empty()) {
        md_filename = sanitize_for_filename(paper_title) + ".md";
        std::cout << "Extracted paper title: " << paper_title << "\n";
    } else {
        // Fall back to PDF filename if title extraction fails
        md_filename = fs::path(pdf_path).stem().string() + ".md";
        std::cerr << "Could not extract title, using PDF filename\n";
    }

    std::string pdf_data = read_file_binary(pdf_path);

    httplib::Client cli("http://localhost:8070");
    cli.set_read_timeout(60, 0);

    // create a vector of upload form data items
    httplib::UploadFormDataItems items = {
        {
            "input",
            pdf_data,
            fs::path(pdf_path).filename().string(),
            "application/pdf"
        }
    };

    // post to Grobid
    auto res = cli.Post("/api/processReferences", httplib::Headers{}, items);

    if (!res || res->status != 200) {
        std::cerr << "Grobid failed\n";
        return;
    }

    // Extract titles from the TEI XML
    auto refs = extract_bibl_titles(res->body);

    // --- Use MarkdownFile to manage the output ---
    MarkdownFile md(md_filename, paper_title);

    // Add each reference as an Obsidian link
    for (auto& ref : refs) {
        std::string doi = normalize_arxiv_from_text(ref);

        if (doi.empty()) {
            doi = query_arxiv_for_doi(ref);
        } else {
            printf("got doi from text for %s\n", ref.c_str());
        }

        if (doi.empty()) {
            doi = query_crossref_for_doi(ref);
        }

        md.add_references({ref}, {doi});
    }

    md.save();
    std::cout << "References written to " << md_filename << "\n";
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



// Helper: lowercase
std::string to_lower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c){ return std::tolower(c); });
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

std::string remove_punct_except_apostrophes(const std::string& s) {
    std::string res;
    std::vector<std::string> apostrophes = {"'", "’", "´", "`"};

    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = s[i];

        if (std::isalnum(c) || std::isspace(c)) {
            res += c;
        } else {

            bool is_apostrophe = false;
            for (auto& ap : apostrophes) {
                if (s.compare(i, ap.size(), ap) == 0) {
                    res += ap;
                    i += ap.size() - 1; // skip extra bytes
                    is_apostrophe = true;
                    break;
                }
            }
            // Otherwise skip punctuation
        }
    }
    return res;
}


std::vector<std::string> filter_words(const std::string& text) {
    std::set<std::string> stop = {"the","a","an","of","for","and","or","on","in","to","with",
                                  "by","from","as","at","via","is","are"};
    std::vector<std::string> result;
    std::vector<std::string> apostrophes = {"'", "’", "´", "`"};

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

        if (!has_apostrophe && word.size() > 2 && stop.find(to_lower(word)) == stop.end()) {
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


void replace_all(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
}


std::string normalize_for_matching(const std::string& s) {

    std::string tmp = s;
    
    // Remove dashes
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

    // normalize by the size of the larger set
    return static_cast<double>(matches) / std::max(words_a.size(), words_b.size());
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
    temp_clean = temp_clean.substr(0, temp_clean.size() - 1); // remove last space
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
    printf("title=\"%s\", no. matches = %zu, query = %s\n", title.c_str(), matches.size(), encoded_query.c_str());

    for (auto* entry = feed->FirstChildElement("entry"); entry; entry = entry->NextSiblingElement("entry")) {
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
        printf("score=%.2f, title=\"%s\", arxiv_title=\"%s\"",
            score, norm_title.c_str(), resp_title.c_str());

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


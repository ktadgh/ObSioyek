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
    fs::path out_path = fs::path(pdf_path).replace_extension(".md");
    MarkdownFile md(out_path.string());

    // Example: add a highlight section above references
    md.add_highlight("These are highlights from the extracted references:");
    md.add_highlight("- Check DOI links for accuracy");
    md.add_highlight("- Titles normalized for Obsidian linking");

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

        // Add as Markdown line
        std::string line = ref;
        if (!doi.empty()) {
            line += "  \n  DOI: " + doi;
        }

        md.add_references({ref}, {doi}); // can add one at a time
    }

    md.save();
    std::cout << "References written to " << out_path << "\n";
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



#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <set>
#include <sstream>
#include <cctype>
#include <httplib.h>
#include <tinyxml2.h>

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


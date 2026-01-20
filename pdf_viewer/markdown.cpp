#include "markdown.h"
#include "grobid_utils.h"
#include "utils.h"
#include <fstream>
#include <algorithm>
#include <set>
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp> 

namespace fs = std::filesystem;

MarkdownFile::MarkdownFile(const std::string& filename, const std::string& paper_title) {
    // Load vault config
    ObsidianConfig cfg = load_vault_config("/Users/tadghk/sioyek/config.json");
    vault_name = cfg.vault_name;

    fs::path md_path(filename);

    // Determine canonical path in vault or fallback to local
    if (!cfg.vault_path.empty()) {
        fs::path vault_dir(cfg.vault_path);
        std::error_code ec;
        if (!fs::exists(vault_dir) && !fs::create_directories(vault_dir, ec)) {
            std::cerr << "Failed to create vault directory: " << vault_dir << " (" << ec.message() << ")\n";
        }
        canonical_path = vault_dir / md_path.filename();   // use vault path
    } else {
        canonical_path = md_path;                          // fallback
    }

    file_path = canonical_path.string();

    references_index = -1; // default: no references section
    std::ifstream in(file_path);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
        in.close();

        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i] == "## References") {
                references_index = static_cast<int>(i);
                break;
            }
        }
    }

    if (!paper_title.empty()) {
        std::string doi = query_arxiv_for_doi(paper_title);
        if (doi.empty()) doi = query_crossref_for_doi(paper_title);
        if (!doi.empty()) add_alias(doi);
    }
}


void MarkdownFile::add_highlight(const std::wstring& wline, const std::string& uuid, char type) {
    std::string line = utf8_encode(wline);
    std::string full_line = line + " #sioyek-" + uuid;

    if (std::find(lines.begin(), lines.end(), full_line) != lines.end()) return;

    if (references_index == -1) {
        lines.push_back(full_line);
    } else {
        lines.insert(lines.begin() + references_index, full_line);
        references_index++;
    }
}

void MarkdownFile::ensure_references_section() {
    if (references_index == -1) {
        if (!lines.empty() && !lines.back().empty()) lines.push_back("");
        lines.push_back("## References");
        lines.push_back("");
        references_index = static_cast<int>(lines.size()) - 2;
    }
}

std::string strip_alias(const std::string& alias_url) {
    const std::string arxiv_prefix = "https://arxiv.org/abs/";
    if (alias_url.rfind(arxiv_prefix, 0) == 0) {
        return alias_url.substr(arxiv_prefix.size());
    }
    return alias_url;
}

void MarkdownFile::add_alias(const std::string& alias_url) {
    if (alias_url.empty()) return;

    // Strip "https://arxiv.org/abs/" if present
    std::string alias = strip_alias(alias_url);

    bool has_frontmatter = !lines.empty() && lines[0] == "---";
    if (has_frontmatter) {
        size_t end_idx = 1;
        for (; end_idx < lines.size(); ++end_idx) if (lines[end_idx] == "---") break;

        int aliases_line = -1;
        for (size_t i = 1; i < end_idx; ++i) {
            if (lines[i].find("aliases:") == 0) { aliases_line = static_cast<int>(i); break; }
        }

        if (aliases_line >= 0) {
            std::string alias_entry = "  - \"" + alias + "\"";
            for (size_t i = aliases_line + 1; i < end_idx; ++i) {
                if (lines[i].rfind("  - ", 0) != 0) break;
                if (lines[i] == alias_entry) return;
            }
            lines.insert(lines.begin() + aliases_line + 1, alias_entry);
            if (references_index > aliases_line) references_index++;
        } else {
            lines.insert(lines.begin() + end_idx, "aliases:");
            lines.insert(lines.begin() + end_idx + 1, "  - \"" + alias + "\"");
            if (references_index >= static_cast<int>(end_idx)) references_index += 2;
        }
    } else {
        lines.insert(lines.begin(), "---");
        lines.insert(lines.begin() + 1, "aliases:");
        lines.insert(lines.begin() + 2, "  - \"" + alias + "\"");
        lines.insert(lines.begin() + 3, "---");
        if (references_index >= 0) references_index += 4;
    }
}


void MarkdownFile::add_references(const std::vector<std::string>& refs,
                                  const std::vector<std::string>& urls)
{
    ensure_references_section();
    std::set<std::string> existing_refs;

    // Collect existing reference titles to avoid duplicates
    for (size_t i = references_index + 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.rfind("- [[", 0) == 0 && line.back() == ']') {
            size_t start = 4;
            size_t end_pos = line.rfind("]]");
            size_t pipe_pos = line.find('|');
            std::string title = (pipe_pos != std::string::npos && pipe_pos < end_pos)
                                ? line.substr(pipe_pos + 1, end_pos - pipe_pos - 1)
                                : line.substr(start, end_pos - start);
            existing_refs.insert(title);
        }
    }

    size_t insert_pos = lines.size();

    for (size_t i = 0; i < refs.size(); ++i) {
        const std::string& ref = refs[i];
        std::string url = (i < urls.size()) ? urls[i] : "";

        if (existing_refs.count(ref) == 0) {
            std::string stripped_id = strip_alias(url);  // just the ID part

            std::string line = "- [[" + ref + "]]";  // local note link

            if (!url.empty()) {
                line += " ([arXiv](" + url + "))";  // clickable arXiv URL
            }

            if (!stripped_id.empty()) {
                line += " ([[";
                line += stripped_id;  // DOI as local note
                line += "]])";
            }

            lines.insert(lines.begin() + insert_pos, line);
            insert_pos++;
            existing_refs.insert(ref);
        }
    }
}

int MarkdownFile::find_reference(const std::string& ref) {
    std::string ref_line = "- [[" + ref + "]]";
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i] == ref_line) return static_cast<int>(i);
    }
    return -1;
}

void MarkdownFile::save() {
    std::ofstream out(file_path);
    if (!out.is_open()) {
        std::cerr << "Failed to open file " << file_path << " for writing\n";
        return;
    }

    for (const auto& line : lines) out << line << "\n";
    out.close();

    // Get relative path in vault
    ObsidianConfig cfg = load_vault_config("/Users/tadghk/sioyek/config.json");
    fs::path relative_path = fs::relative(canonical_path, cfg.vault_path);

    std::string command = "open \"obsidian://open?vault=" + vault_name + "&file=" + relative_path.string() + "\"";
    std::cout << "Executing command: " << command << std::endl;
    std::system(command.c_str());
}

ObsidianConfig load_vault_config(const std::string& config_file)
{
    ObsidianConfig config;

    std::ifstream file(config_file);
    if (!file.is_open()) {
        std::cerr << "Could not open vault config file: " << config_file << "\n";
        return config;
    }

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("vault_name") && j["vault_name"].is_string())
            config.vault_name = j["vault_name"].get<std::string>();

        if (j.contains("vault_path") && j["vault_path"].is_string())
            config.vault_path = j["vault_path"].get<std::string>();
    }
    catch (const std::exception& e) {
        std::cerr << "Error parsing vault config JSON: " << e.what() << "\n";
    }

    return config;
}

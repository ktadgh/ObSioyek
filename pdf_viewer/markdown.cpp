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

std::string strip_alias(const std::string& alias_url) {
    const std::string arxiv_prefix = "https://arxiv.org/abs/";
    if (alias_url.rfind(arxiv_prefix, 0) == 0) {
        return alias_url.substr(arxiv_prefix.size());
    }
    return alias_url;
}

MarkdownFile::MarkdownFile(const std::string& md_path, const std::string& paper_title) {
    file_path = md_path;

    printf("markdownpath = %s\n", md_path.c_str());
    // Load vault config and set vault_name
    ObsidianConfig cfg = load_vault_config("/Users/tadghk/sioyek/config.json");
    vault_name = cfg.vault_name;

    // Set canonical_path from file_path
    canonical_path = fs::weakly_canonical(md_path);

    references_index = -1;
    std::ifstream in(file_path);
    if (in.is_open()) {
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
            size_t sioyek_pos = line.find("sioyek://");
            if (sioyek_pos != std::string::npos) {
                size_t paren_pos = line.find(')', sioyek_pos);
                if (paren_pos != std::string::npos && paren_pos + 2 < line.size()) {
                    std::string content = line.substr(paren_pos + 2);
                    existing_highlights.insert(content);
                }
            }
        }
        in.close();

        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i] == "## References") {
                references_index = static_cast<int>(i);
                break;
            }
        }
    }

    arxiv_id = get_alias();

    if (arxiv_id.empty() && !paper_title.empty()) {
        std::string doi = query_arxiv_for_doi(paper_title);
        if (doi.empty()) doi = query_crossref_for_doi(paper_title);
        if (!doi.empty()) {
            add_alias(doi);
            arxiv_id = strip_alias(doi);
        }
    }
}

void MarkdownFile::add_highlight(const std::wstring& wline, const std::string& uuid, char type) {
    std::string line = utf8_encode(wline);

    if (existing_highlights.count(line)) return;

    existing_highlights.insert(line);
    std::string full_line = "[Back to Sioyek](sioyek://open?" + uuid + ") " + line;

    if (references_index == -1) {
        lines.push_back(full_line);
    } else {
        lines.insert(lines.begin() + references_index, full_line);
        references_index++;
    }
}

void MarkdownFile::update_highlight_comment(const std::string& uuid, const std::wstring& comment) {
    std::string search_pattern = "sioyek://open?" + uuid + ")";

    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(search_pattern) != std::string::npos) {
            // Check if next line is an existing comment (indented bullet)
            bool has_existing_comment = (i + 1 < lines.size() &&
                (lines[i + 1].rfind("  - ", 0) == 0 || lines[i + 1].rfind("\t- ", 0) == 0));

            if (comment.empty()) {
                // Remove existing comment line if present
                if (has_existing_comment) {
                    lines.erase(lines.begin() + i + 1);
                    if (references_index > static_cast<int>(i + 1)) {
                        references_index--;
                    }
                }
            } else {
                std::string comment_line = "  - " + utf8_encode(comment);
                if (has_existing_comment) {
                    // Update existing comment line
                    lines[i + 1] = comment_line;
                } else {
                    // Insert new comment line after the highlight
                    lines.insert(lines.begin() + i + 1, comment_line);
                    if (references_index > static_cast<int>(i)) {
                        references_index++;
                    }
                }
            }
            return;
        }
    }
}

void MarkdownFile::delete_highlight(const std::string& uuid) {
    std::string search_pattern = "sioyek://open?" + uuid + ")";

    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(search_pattern) != std::string::npos) {
            // Remove the highlight text from existing_highlights set
            size_t paren_pos = lines[i].find(')', lines[i].find("sioyek://"));
            if (paren_pos != std::string::npos && paren_pos + 2 < lines[i].size()) {
                std::string content = lines[i].substr(paren_pos + 2);
                existing_highlights.erase(content);
            }

            // Check if next line is a comment (indented bullet) and remove it too
            if (i + 1 < lines.size() &&
                (lines[i + 1].rfind("  - ", 0) == 0 || lines[i + 1].rfind("\t- ", 0) == 0)) {
                lines.erase(lines.begin() + i + 1);
                if (references_index > static_cast<int>(i + 1)) {
                    references_index--;
                }
            }

            // Remove the highlight line
            lines.erase(lines.begin() + i);
            if (references_index > static_cast<int>(i)) {
                references_index--;
            }
            return;
        }
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


std::string MarkdownFile::get_alias() const {
    if (lines.empty() || lines[0] != "---") {
        return "";
    }

    size_t end_idx = 1;
    for (; end_idx < lines.size(); ++end_idx) {
        if (lines[end_idx] == "---") break;
    }

    int aliases_line = -1;
    for (size_t i = 1; i < end_idx; ++i) {
        if (lines[i].find("aliases:") == 0) {
            aliases_line = static_cast<int>(i);
            break;
        }
    }

    if (aliases_line < 0) {
        return "";
    }

    for (size_t i = aliases_line + 1; i < end_idx; ++i) {
        const std::string& line = lines[i];
        if (line.rfind("  - ", 0) != 0) break;

        std::string alias = line.substr(4);
        if (alias.size() >= 2 && alias.front() == '"' && alias.back() == '"') {
            alias = alias.substr(1, alias.size() - 2);
        }

        if (!alias.empty() && std::isdigit(alias[0])) {
            return alias;
        }
    }

    return "";
}


std::string sanitize_filename(const std::string& name) {
    std::string safe;
    for (auto c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            safe += '-';  // replace illegal filename chars with dash
        }
        else if (c == '{' || c == '}') {
            // skip curly braces entirely
            continue;
        }
        else {
            safe += c;  // keep valid chars
        }
    }
    return safe;
}


void MarkdownFile::create_note_for_paper(const std::string& paper_title, const std::string& alias_url) {
    std::string safe_title = sanitize_filename(paper_title);

    // Put reference notes in a "references" subdirectory within the vault
    fs::path references_dir = canonical_path.parent_path() / "references";
    fs::create_directories(references_dir);  // Create if doesn't exist

    fs::path md_path = references_dir / (safe_title + ".md");

    // Skip if file already exists
    if (fs::exists(md_path)) {
        return;
    }

    MarkdownFile paper_note(md_path.string(), "");

    if (!alias_url.empty()) paper_note.add_alias(alias_url);

    paper_note.save_no_open();
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

        if (existing_refs.count(ref) == 0 && !ref.empty()) {
            // Create the markdown note for this reference
            create_note_for_paper(ref, url);

            std::string safe_ref = sanitize_filename(ref);
            std::string stripped_id = strip_alias(url);

            // Generate the reference line
            std::string line = "- [[" + safe_ref + "]]";
            if (!url.empty()) line += " ([arXiv](" + url + "))";
            if (!stripped_id.empty()) line += " ([[" + stripped_id + "]])";  // no extra spaces

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

void MarkdownFile::save_no_open() {
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
    // std::system(command.c_str());
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

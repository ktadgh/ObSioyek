#include "markdown.h"
#include <fstream>
#include <algorithm>
#include <set>
#include <iostream> 

int MarkdownFile::find_reference(const std::string& ref) {
    std::string ref_line = "- [[" + ref + "]]";
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i] == ref_line) return static_cast<int>(i);
    }
    return -1;
}

MarkdownFile::MarkdownFile(const std::string& path)
    : file_path(path)
{
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
}

void MarkdownFile::add_highlight(const std::string& line) {
    if (std::find(lines.begin(), lines.end(), line) != lines.end()) return;
    if (references_index == -1) {
        lines.push_back(line);
    } else {
        lines.insert(lines.begin() + references_index, line);
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

void MarkdownFile::add_references(const std::vector<std::string>& refs,
                                  const std::vector<std::string>& dois)
{
    ensure_references_section();
    std::set<std::string> existing_refs;
    for (size_t i = references_index + 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.rfind("- [[", 0) == 0 && line.back() == ']') {
            std::string title = line.substr(4, line.size() - 6);
            existing_refs.insert(title);
        }
    }

    size_t insert_pos = lines.size();
    for (size_t i = 0; i < refs.size(); ++i) {
        const std::string& ref = refs[i];
        std::string doi_text = (i < dois.size() && !dois[i].empty()) ? "  DOI: " + dois[i] : "";

        if (existing_refs.count(ref) == 0) {
            lines.insert(lines.begin() + insert_pos, "- [[" + ref + "]]");
            insert_pos++;
            if (!doi_text.empty()) {
                lines.insert(lines.begin() + insert_pos, doi_text);
                insert_pos++;
            }
            existing_refs.insert(ref);
        } else if (!doi_text.empty()) {
            for (size_t j = references_index + 1; j < lines.size(); ++j) {
                if (lines[j] == "- [[" + ref + "]]") {
                    size_t next_line = j + 1;
                    if (next_line >= lines.size() || lines[next_line] != doi_text) {
                        lines.insert(lines.begin() + next_line, doi_text);
                    }
                    break;
                }
            }
        }
    }
}

ObsidianConfig load_vault_config(const std::string& config_file) {
    ObsidianConfig cfg;
    std::ifstream in(config_file);
    if (!in.is_open()) {
        std::cerr << "Could not open config file: " << config_file << "\n";
        return cfg;
    }
    try {
        nlohmann::json j;
        in >> j;
        if (j.contains("vault_name")) cfg.vault_name = j["vault_name"];
        if (j.contains("vault_path")) cfg.vault_path = j["vault_path"];
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse config: " << e.what() << "\n";
    }
    return cfg;
}


void MarkdownFile::save() {
    ObsidianConfig cfg = load_vault_config("/Users/tadghk/grobid-test/config.json");

    fs::path output_path = file_path;
    
    if (!cfg.vault_path.empty()) {
        fs::path vault_dir = fs::path(cfg.vault_path);

        std::error_code ec;
        fs::create_directories(vault_dir, ec);

        output_path = vault_dir / fs::path(file_path).filename();
    }

    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "Failed to open file " << output_path << " for writing\n";
        return;
    }

    for (const auto& line : lines) {
        out << line << "\n";
    }
    out.close();

    // Open the file in Obsidian
    std::string command = "obsidian \"" + output_path.string() + "\"";
    int result = std::system(command.c_str());
    if (result != 0) {
        std::cerr << "Failed to open file in Obsidian. Make sure Obsidian is in your PATH.\n";
    }
}



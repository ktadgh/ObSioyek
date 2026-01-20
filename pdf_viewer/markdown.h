// markdown.h
#ifndef MARKDOWN_H
#define MARKDOWN_H

#include <string>
#include <vector>
#include <filesystem>

class MarkdownFile {
private:
    std::string file_path;
    std::string vault_name;
    std::vector<std::string> lines;
    int references_index = -1;

    int find_reference(const std::string& ref);

public:
    MarkdownFile(const std::string& path, const std::string& paper_title = "");
    void add_highlight(const std::wstring& line, const std::string& uuid, char type);
    void ensure_references_section();
    void add_references(const std::vector<std::string>& refs,
                        const std::vector<std::string>& dois = {});
    void add_alias(const std::string& alias);
    void save();
    std::filesystem::path canonical_path;

};

struct ObsidianConfig {
    std::string vault_name;
    std::string vault_path;
};

ObsidianConfig load_vault_config(const std::string& config_file);

#endif // MARKDOWN_H

// markdown.h
#ifndef MARKDOWN_H
#define MARKDOWN_H

#include <string>
#include <vector>
#include <filesystem>
#include <set>

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
    void update_highlight_comment(const std::string& uuid, const std::wstring& comment);
    void delete_highlight(const std::string& uuid);
    void ensure_references_section();
    void add_references(const std::vector<std::string>& refs,
                        const std::vector<std::string>& dois = {});
    void add_alias(const std::string& alias);
    void save();
    void save_no_open();
    void create_note_for_paper(const std::string& paper_title, const std::string& alias_url);
    std::string arxiv_id;
    std::filesystem::path canonical_path;
    std::string get_alias() const;
    std::set<std::string> existing_highlights;

};

struct ObsidianConfig {
    std::string vault_name;
    std::string vault_path;
};

ObsidianConfig load_vault_config(const std::string& config_file);

#endif // MARKDOWN_H

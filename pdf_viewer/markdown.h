// markdown.h
#ifndef MARKDOWN_H
#define MARKDOWN_H

#include <string>
#include <vector>

class MarkdownFile {
private:
    std::string file_path;
    std::vector<std::string> lines;
    int references_index = -1;

    int find_reference(const std::string& ref);

public:
    MarkdownFile(const std::string& path);
    void add_highlight(const std::string& line);
    void ensure_references_section();
    void add_references(const std::vector<std::string>& refs,
                        const std::vector<std::string>& dois = {});
    void save();
};

struct ObsidianConfig {
    std::string vault_name;
    std::string vault_path;
};

#endif // MARKDOWN_H

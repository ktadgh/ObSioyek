#pragma once
#include <map>
#include <string>
#include "markdown.h"

class Vault {
public:
    Vault(const std::string& config_file_path);
    void Index();

    std::string vault_name;
    std::string vault_path;
    std::map<std::string, MarkdownFile> papers;
};

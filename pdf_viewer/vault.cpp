#include "vault.h"
#include "markdown.h"
#include <filesystem>

namespace fs = std::filesystem;

Vault::Vault(const std::string& config_file_path) {
    ObsidianConfig cfg = load_vault_config(config_file_path);
    vault_name = cfg.vault_name;
    vault_path = cfg.vault_path;
}

void Vault::Index() {
    papers.clear();

    for (const auto& entry : fs::recursive_directory_iterator(vault_path)) {
        if (entry.path().extension() == ".md") {
            MarkdownFile md(entry.path().string(), "");
            if (!md.arxiv_id.empty()) {
                papers.emplace(md.arxiv_id, md);
            }
        }
    }
}

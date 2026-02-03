// grobid_utils.h
#pragma once

#include <string>
#include <vector>
#include <filesystem>

// GROBID server management
bool ensure_grobid_running();

// DOI/reference lookups
std::string query_crossref_for_doi(const std::string& title);
std::string query_arxiv_for_doi(const std::string& title);
std::string normalize_arxiv_from_text(const std::string& text);

// String utilities
std::string url_encode(const std::string& value);
std::string sanitize_for_filename(const std::string& title);
std::string normalize_title(const std::string& title);
std::string to_lower(const std::string& s);
std::string normalize_for_matching(const std::string& s);
std::string remove_punct_except_apostrophes(const std::string& s);
double word_overlap_similarity(const std::string& a, const std::string& b);

// Parsing helpers
std::vector<std::string> split(const std::string& s, char delim);
std::vector<std::string> filter_words(const std::string& text);
std::string build_query(const std::vector<std::string>& words);
void replace_all(std::string& s, const std::string& from, const std::string& to);

// File utilities
std::string read_file_binary(const std::filesystem::path& path);

// GROBID extraction
std::vector<std::string> extract_bibl_titles(const std::string& tei_xml);
std::vector<std::string> extract_pdf_references_with_grobid(const std::string& pdf_path);

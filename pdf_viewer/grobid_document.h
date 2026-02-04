// grobid_document.h
#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

struct Reference {
    std::string title;
    std::string doi;
};

class GrobidDocument {
public:
    explicit GrobidDocument(const std::string& pdf_path);

    // Parses the PDF via GROBID (only once, caches result)
    bool parse();

    // Accessors
    const std::string& get_title() const { return m_title; }
    const std::string& get_doi() const { return m_doi; }
    const std::vector<Reference>& get_references() const { return m_references; }
    const std::string& get_pdf_path() const { return m_pdf_path; }
    bool is_parsed() const { return m_parsed; }

    // Resolve DOIs for all references (calls arxiv/crossref)
    void resolve_dois();

private:
    std::string m_pdf_path;
    std::string m_title;
    std::string m_doi;  // DOI/arxiv_id of the main document
    std::vector<Reference> m_references;
    std::string m_tei_xml;  // Cache the raw TEI XML too
    bool m_parsed = false;
    bool m_dois_resolved = false;

    void parse_header_from_tei();
    void parse_references_from_tei();
};

// grobid_document.cpp
#include "grobid_document.h"
#include "grobid_utils.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <tinyxml2.h>

#include <iostream>
#include <fstream>
#include <sstream>

GrobidDocument::GrobidDocument(const std::string& pdf_path)
    : m_pdf_path(pdf_path) {}

bool GrobidDocument::parse() {
    if (m_parsed) {
        return true;  // Already parsed, use cached data
    }

    if (!fs::exists(m_pdf_path)) {
        std::cerr << "PDF does not exist: " << m_pdf_path << "\n";
        return false;
    }

    if (!ensure_grobid_running()) {
        std::cerr << "Failed to start GROBID\n";
        return false;
    }

    std::string pdf_data = read_file_binary(m_pdf_path);

    httplib::Client cli("http://localhost:8070");
    cli.set_read_timeout(120, 0);  // Full document processing can take a while

    httplib::UploadFormDataItems items = {
        {
            "input",
            pdf_data,
            fs::path(m_pdf_path).filename().string(),
            "application/pdf"
        }
    };

    // Try processFulltextDocument first - returns header AND references in one call
    auto res = cli.Post("/api/processFulltextDocument", httplib::Headers{}, items);

    if (res && res->status == 200) {
        m_tei_xml = res->body;
        parse_header_from_tei();
        parse_references_from_tei();
        m_parsed = true;
        return true;
    }

    // Fallback: try separate calls if full text fails
    std::cerr << "processFulltextDocument failed, trying separate calls...\n";

    // Get header
    res = cli.Post("/api/processHeaderDocument", httplib::Headers{}, items);
    if (res && res->status == 200) {
        m_tei_xml = res->body;
        parse_header_from_tei();
    } else {
        std::cerr << "processHeaderDocument failed\n";
    }

    // Get references
    res = cli.Post("/api/processReferences", httplib::Headers{}, items);
    if (res && res->status == 200) {
        // Store this XML (may overwrite header XML, but we already parsed header)
        std::string refs_xml = res->body;
        // Temporarily swap to parse references
        std::string saved = m_tei_xml;
        m_tei_xml = refs_xml;
        parse_references_from_tei();
        m_tei_xml = saved;
    } else {
        std::cerr << "processReferences failed\n";
    }

    m_parsed = true;
    return !m_title.empty() || !m_references.empty();
}

void GrobidDocument::parse_header_from_tei() {
    if (m_tei_xml.empty()) return;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(m_tei_xml.c_str()) != tinyxml2::XML_SUCCESS) {
        return;
    }

    auto* root = doc.RootElement();
    if (!root) return;

    // Navigate to title: teiHeader/fileDesc/titleStmt/title
    auto* teiHeader = root->FirstChildElement("teiHeader");
    if (!teiHeader) return;

    auto* fileDesc = teiHeader->FirstChildElement("fileDesc");
    if (!fileDesc) return;

    auto* titleStmt = fileDesc->FirstChildElement("titleStmt");
    if (!titleStmt) return;

    auto* title = titleStmt->FirstChildElement("title");
    if (title && title->GetText()) {
        m_title = title->GetText();
    }
}

void GrobidDocument::parse_references_from_tei() {
    if (m_tei_xml.empty()) return;

    tinyxml2::XMLDocument doc;
    if (doc.Parse(m_tei_xml.c_str()) != tinyxml2::XML_SUCCESS) {
        return;
    }

    auto* root = doc.RootElement();
    if (!root) return;

    // Navigate to listBibl: text/back/div/listBibl
    auto* text = root->FirstChildElement("text");
    if (!text) return;

    auto* back = text->FirstChildElement("back");
    if (!back) return;

    auto* div = back->FirstChildElement("div");
    if (!div) return;

    auto* listBibl = div->FirstChildElement("listBibl");
    if (!listBibl) return;

    for (auto* bibl = listBibl->FirstChildElement("biblStruct");
         bibl;
         bibl = bibl->NextSiblingElement("biblStruct")) {

        Reference ref;

        // Try analytic/title first (for articles)
        auto* analytic = bibl->FirstChildElement("analytic");
        if (analytic) {
            auto* title = analytic->FirstChildElement("title");
            if (title && title->GetText()) {
                ref.title = title->GetText();
                m_references.push_back(ref);
                continue;
            }
        }

        // Try monogr/title (for books)
        auto* monogr = bibl->FirstChildElement("monogr");
        if (monogr) {
            auto* title = monogr->FirstChildElement("title");
            if (title && title->GetText()) {
                ref.title = title->GetText();
                m_references.push_back(ref);
                continue;
            }
        }

        std::ostringstream text_content;
        for (auto* el = bibl->FirstChildElement(); el; el = el->NextSiblingElement()) {
            if (el->GetText()) {
                text_content << el->GetText() << " ";
            }
        }
        std::string s = text_content.str();
        if (!s.empty()) {
            ref.title = s;
            m_references.push_back(ref);
        }
    }
}

void GrobidDocument::resolve_dois() {
    if (m_dois_resolved) return;

    // Resolve DOI for the main document
    if (m_doi.empty() && !m_title.empty()) {
        m_doi = normalize_arxiv_from_text(m_title);

        if (m_doi.empty()) {
            m_doi = query_arxiv_for_doi(m_title);
        }

        if (m_doi.empty()) {
            m_doi = query_crossref_for_doi(m_title);
        }
    }

    // Resolve DOIs for references
    for (auto& ref : m_references) {
        if (!ref.doi.empty()) continue;  // Already has DOI

        ref.doi = normalize_arxiv_from_text(ref.title);

        if (ref.doi.empty()) {
            ref.doi = query_arxiv_for_doi(ref.title);
        }

        if (ref.doi.empty()) {
            ref.doi = query_crossref_for_doi(ref.title);
        }
    }

    m_dois_resolved = true;
}

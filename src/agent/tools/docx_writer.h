#pragma once
//
// docx_writer — emit a minimal, valid Office Open XML (.docx) word-processing
// document by writing the required OOXML parts into a ZIP container. Internal to
// src/agent (NOT part of the frozen public contract).
//
// We deliberately avoid a third-party OOXML/zip dependency: a .docx is just a
// ZIP with a fixed set of XML parts. We build the bytes ourselves with a tiny
// STORE-method (uncompressed) ZIP writer — Word/LibreOffice open stored entries
// fine — and a hand-rolled minimal document.xml. This keeps the doc tools free
// of native deps while still producing a real, openable, printable Word file.
//
// Supported content model (enough for drafts + lab reports):
//   * a document title (Title style),
//   * headings (Heading1/Heading2),
//   * body paragraphs,
//   * simple bullet list items.
//
#include <filesystem>
#include <string>
#include <vector>

namespace polymath::docx {

// A logical block of the document, rendered to one or more OOXML paragraphs.
struct Block {
    enum class Type { Title, Heading1, Heading2, Paragraph, Bullet };
    Type        type = Type::Paragraph;
    std::string text;
};

class Document {
public:
    void setTitle(const std::string& t) { title_ = t; }

    void addTitle(const std::string& t)     { blocks_.push_back({Block::Type::Title, t}); }
    void addHeading1(const std::string& t)   { blocks_.push_back({Block::Type::Heading1, t}); }
    void addHeading2(const std::string& t)   { blocks_.push_back({Block::Type::Heading2, t}); }
    void addParagraph(const std::string& t)  { blocks_.push_back({Block::Type::Paragraph, t}); }
    void addBullet(const std::string& t)     { blocks_.push_back({Block::Type::Bullet, t}); }

    // Serialize the whole package to `out_path` (creating parent dirs). Returns
    // false on any I/O failure.
    bool save(const std::filesystem::path& out_path) const;

private:
    std::string buildDocumentXml() const;

    std::string              title_;
    std::vector<Block>       blocks_;
};

} // namespace polymath::docx

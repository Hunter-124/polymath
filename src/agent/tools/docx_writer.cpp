#include "docx_writer.h"
#include "logging.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <system_error>

// Minimal ZIP (STORE method) + OOXML emitter. No external deps.
//
// ZIP format reference: PKWARE APPNOTE 6.3.x (local file header sig 0x04034b50,
// central directory header 0x02014b50, end-of-central-directory 0x06054b50).
// We use method 0 (stored) so we never link zlib; Word and LibreOffice both
// open uncompressed .docx packages.

namespace polymath::docx {

namespace {

// --- CRC-32 (IEEE 802.3 polynomial, as required by ZIP) ---------------------
uint32_t crc32(const std::string& data) {
    static std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char b : data)
        crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void put16(std::string& s, uint16_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void put32(std::string& s, uint32_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 24) & 0xFF));
}

// Accumulates entries and writes a complete stored-method ZIP archive.
class ZipWriter {
public:
    void add(const std::string& name, const std::string& content) {
        Entry e;
        e.name = name;
        e.content = content;
        e.crc = crc32(content);
        e.offset = static_cast<uint32_t>(out_.size());

        // Local file header.
        put32(out_, 0x04034b50);
        put16(out_, 20);                 // version needed
        put16(out_, 0);                  // flags
        put16(out_, 0);                  // method = stored
        put16(out_, 0);                  // mod time
        put16(out_, 0x21);               // mod date (1980-01-01-ish placeholder)
        put32(out_, e.crc);
        put32(out_, static_cast<uint32_t>(content.size()));   // compressed
        put32(out_, static_cast<uint32_t>(content.size()));   // uncompressed
        put16(out_, static_cast<uint16_t>(name.size()));
        put16(out_, 0);                  // extra len
        out_ += name;
        out_ += content;

        entries_.push_back(std::move(e));
    }

    std::string finish() {
        const uint32_t cdStart = static_cast<uint32_t>(out_.size());
        for (const auto& e : entries_) {
            put32(out_, 0x02014b50);     // central dir header
            put16(out_, 20);             // version made by
            put16(out_, 20);             // version needed
            put16(out_, 0);              // flags
            put16(out_, 0);              // method
            put16(out_, 0);              // mod time
            put16(out_, 0x21);           // mod date
            put32(out_, e.crc);
            put32(out_, static_cast<uint32_t>(e.content.size()));
            put32(out_, static_cast<uint32_t>(e.content.size()));
            put16(out_, static_cast<uint16_t>(e.name.size()));
            put16(out_, 0);              // extra
            put16(out_, 0);              // comment
            put16(out_, 0);              // disk number
            put16(out_, 0);              // internal attrs
            put32(out_, 0);              // external attrs
            put32(out_, e.offset);       // local header offset
            out_ += e.name;
        }
        const uint32_t cdSize = static_cast<uint32_t>(out_.size()) - cdStart;

        put32(out_, 0x06054b50);         // end of central directory
        put16(out_, 0);                  // disk
        put16(out_, 0);                  // cd start disk
        put16(out_, static_cast<uint16_t>(entries_.size()));
        put16(out_, static_cast<uint16_t>(entries_.size()));
        put32(out_, cdSize);
        put32(out_, cdStart);
        put16(out_, 0);                  // comment len
        return out_;
    }

private:
    struct Entry {
        std::string name;
        std::string content;
        uint32_t    crc = 0;
        uint32_t    offset = 0;
    };
    std::string        out_;
    std::vector<Entry> entries_;
};

// XML-escape text destined for an element body / attribute value.
std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

// Emit one <w:p> with the given paragraph-style id, splitting on newlines into
// <w:br/> so multi-line block text stays inside a single paragraph.
std::string paragraph(const std::string& styleId, const std::string& text) {
    std::ostringstream p;
    p << "<w:p>";
    if (!styleId.empty())
        p << "<w:pPr><w:pStyle w:val=\"" << styleId << "\"/></w:pPr>";
    p << "<w:r><w:t xml:space=\"preserve\">";
    bool first = true;
    std::string line;
    std::istringstream lines(text);
    while (std::getline(lines, line)) {
        if (!first) p << "</w:t></w:r><w:r><w:br/><w:t xml:space=\"preserve\">";
        p << xmlEscape(line);
        first = false;
    }
    p << "</w:t></w:r></w:p>";
    return p.str();
}

const char* kContentTypes = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
</Types>)XML";

const char* kRootRels = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
</Relationships>)XML";

const char* kDocumentRels = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>)XML";

// Minimal style set: Title, Heading1, Heading2, ListParagraph. Word supplies
// sensible built-in formatting for these style ids; we just declare them.
const char* kStyles = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>
<w:style w:type="paragraph" w:styleId="Title"><w:name w:val="Title"/><w:basedOn w:val="Normal"/><w:pPr><w:spacing w:after="240"/></w:pPr><w:rPr><w:b/><w:sz w:val="48"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:pPr><w:spacing w:before="240" w:after="120"/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:sz w:val="32"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/><w:pPr><w:spacing w:before="200" w:after="100"/><w:outlineLvl w:val="1"/></w:pPr><w:rPr><w:b/><w:sz w:val="26"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="ListParagraph"><w:name w:val="List Paragraph"/><w:basedOn w:val="Normal"/><w:pPr><w:ind w:left="720"/></w:pPr></w:style>
</w:styles>)XML";

std::string coreProps(const std::string& title) {
    std::ostringstream s;
    s << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
         "<cp:coreProperties "
         "xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
         "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      << "<dc:title>" << xmlEscape(title) << "</dc:title>"
      << "<dc:creator>Polymath</dc:creator>"
      << "</cp:coreProperties>";
    return s.str();
}

} // namespace

std::string Document::buildDocumentXml() const {
    std::ostringstream body;
    body << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<w:document "
            "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
            "<w:body>";
    for (const auto& b : blocks_) {
        switch (b.type) {
            case Block::Type::Title:     body << paragraph("Title", b.text);     break;
            case Block::Type::Heading1:  body << paragraph("Heading1", b.text);  break;
            case Block::Type::Heading2:  body << paragraph("Heading2", b.text);  break;
            case Block::Type::Bullet:    body << paragraph("ListParagraph", "• " + b.text); break;
            case Block::Type::Paragraph: body << paragraph("", b.text);          break;
        }
    }
    // A final sectPr keeps Word happy (page size = US Letter; margins default).
    body << "<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/>"
            "<w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\"/>"
            "</w:sectPr>";
    body << "</w:body></w:document>";
    return body.str();
}

bool Document::save(const std::filesystem::path& out_path) const {
    ZipWriter zip;
    zip.add("[Content_Types].xml", kContentTypes);
    zip.add("_rels/.rels", kRootRels);
    zip.add("word/_rels/document.xml.rels", kDocumentRels);
    zip.add("word/document.xml", buildDocumentXml());
    zip.add("word/styles.xml", kStyles);
    zip.add("docProps/core.xml", coreProps(title_));
    const std::string archive = zip.finish();

    std::error_code ec;
    std::filesystem::create_directories(out_path.parent_path(), ec);

    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f) {
        PM_ERROR("docx: cannot open '{}' for writing", out_path.string());
        return false;
    }
    f.write(archive.data(), static_cast<std::streamsize>(archive.size()));
    if (!f) {
        PM_ERROR("docx: write failed for '{}'", out_path.string());
        return false;
    }
    return true;
}

} // namespace polymath::docx

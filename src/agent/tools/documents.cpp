#include "documents.h"
#include "docx_writer.h"
#include "tool_support.h"
#include "database.h"
#include "paths.h"
#include "logging.h"

#include <filesystem>

// draft_document / generate_lab_report — render a structured document to a real
// .docx (minimal OOXML, no native deps) under Paths::documents() and record it
// in the `documents` table so the Task Queue / Documents UI can open + print it.
//
// Argument shape (shared):
//   title:   string (required)
//   summary: string (optional intro paragraph)
//   sections: [ { heading: string, body: string, bullets: [string,...] }, ... ]
//   body:    string  (fallback when no sections are given)
//
// generate_lab_report adds conventional lab-report scaffolding (Objective,
// Materials, Method, Results, Analysis, Conclusion) when those sections are
// supplied, and is flagged isDeepTask() so the runtime queues it.

namespace polymath {

namespace {

// Append the shared {summary, sections[], body} content model to a docx::Document.
void buildBody(docx::Document& doc, const nlohmann::json& args) {
    if (args.value("summary", "") != "")
        doc.addParagraph(args.value("summary", std::string{}));

    bool wroteSection = false;
    if (args.contains("sections") && args["sections"].is_array()) {
        for (const auto& s : args["sections"]) {
            if (!s.is_object()) continue;
            const std::string heading = s.value("heading", "");
            if (!heading.empty()) { doc.addHeading1(heading); wroteSection = true; }
            const std::string body = s.value("body", "");
            if (!body.empty()) { doc.addParagraph(body); wroteSection = true; }
            if (s.contains("bullets") && s["bullets"].is_array()) {
                for (const auto& b : s["bullets"]) {
                    if (b.is_string()) { doc.addBullet(b.get<std::string>()); wroteSection = true; }
                }
            }
        }
    }

    if (!wroteSection && args.value("body", "") != "")
        doc.addParagraph(args.value("body", std::string{}));
}

// Render + persist. Returns a populated ToolResult; on success records a row in
// `documents` and reports the saved path.
ToolResult renderAndRecord(const nlohmann::json& args, ToolContext& ctx,
                           const std::string& kind, const char* logName) {
    const std::string title = args.value("title", "");
    if (title.empty())
        return {false, {{"error", "title required"}},
                std::string(logName) + ": missing title"};

    docx::Document doc;
    doc.setTitle(title);
    doc.addTitle(title);
    buildBody(doc, args);

    const std::string slug = tool_support::slugify(title);
    const std::string fname = slug + "-" + std::to_string(tool_support::nowUnix()) + ".docx";
    const std::filesystem::path out = Paths::instance().documents() / fname;

    if (!doc.save(out)) {
        return {false, {{"error", "failed to write document"}},
                std::string(logName) + ": write failed"};
    }

    const int64_t id = ctx.db->exec(
        "INSERT INTO documents(title,kind,path,created_at) VALUES(?1,?2,?3,?4)",
        {title, kind, out.string(), tool_support::nowUnix()});

    PM_INFO("{}: wrote '{}' (documents.id={})", logName, out.string(), id);
    nlohmann::json content = {
        {"document_id", id},
        {"title", title},
        {"kind", kind},
        {"path", out.string()},
    };
    return {true, std::move(content), "Saved " + kind + " \"" + title + "\""};
}

} // namespace

// --- draft_document ---------------------------------------------------------

std::string DraftDocumentTool::name() const { return "draft_document"; }
std::string DraftDocumentTool::description() const {
    return "Draft a document (letter, note, essay, plan) and save it as a .docx the user "
           "can open and print. Provide a title plus either sections or a body.";
}

nlohmann::json DraftDocumentTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title",   {{"type", "string"}, {"description", "Document title"}}},
            {"summary", {{"type", "string"}, {"description", "Optional intro paragraph"}}},
            {"body",    {{"type", "string"}, {"description", "Body text (used if no sections)"}}},
            {"sections", {
                {"type", "array"},
                {"description", "Ordered sections"},
                {"items", {
                    {"type", "object"},
                    {"properties", {
                        {"heading", {{"type", "string"}}},
                        {"body",    {{"type", "string"}}},
                        {"bullets", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                    }},
                }},
            }},
        }},
        {"required", {"title"}},
    };
}

ToolResult DraftDocumentTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    return renderAndRecord(args, ctx, "draft", "draft_document");
}

// --- generate_lab_report (deep task) ----------------------------------------

std::string GenerateLabReportTool::name() const { return "generate_lab_report"; }
std::string GenerateLabReportTool::description() const {
    return "Generate a full lab report (Objective, Materials, Method, Results, Analysis, "
           "Conclusion) and save it as a .docx. This is a longer task and runs in the "
           "background; the user is notified when it is ready.";
}

nlohmann::json GenerateLabReportTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title",      {{"type", "string"}, {"description", "Report title / experiment name"}}},
            {"summary",    {{"type", "string"}, {"description", "Abstract / overview"}}},
            {"objective",  {{"type", "string"}}},
            {"materials",  {{"type", "array"}, {"items", {{"type", "string"}}}}},
            {"method",     {{"type", "string"}}},
            {"results",    {{"type", "string"}}},
            {"analysis",   {{"type", "string"}}},
            {"conclusion", {{"type", "string"}}},
        }},
        {"required", {"title"}},
    };
}

ToolResult GenerateLabReportTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    // Map the lab-report-specific fields onto the shared section model, then
    // reuse the renderer. (When queued as a deep task the scheduler runs the
    // "lab_report" job type; this method is the canonical generator and also
    // works if ever invoked inline.)
    nlohmann::json shaped = {
        {"title", args.value("title", "")},
        {"summary", args.value("summary", "")},
    };
    nlohmann::json sections = nlohmann::json::array();
    auto addText = [&](const char* heading, const std::string& key) {
        if (args.value(key, "") != "")
            sections.push_back({{"heading", heading}, {"body", args.value(key, std::string{})}});
    };
    addText("Objective", "objective");
    if (args.contains("materials") && args["materials"].is_array() && !args["materials"].empty())
        sections.push_back({{"heading", "Materials"}, {"bullets", args["materials"]}});
    addText("Method", "method");
    addText("Results", "results");
    addText("Analysis", "analysis");
    addText("Conclusion", "conclusion");
    shaped["sections"] = std::move(sections);

    return renderAndRecord(shaped, ctx, "lab_report", "generate_lab_report");
}

} // namespace polymath

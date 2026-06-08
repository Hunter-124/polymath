#pragma once
//
// Document-generation tools: draft_document (inline) and generate_lab_report
// (deep task). Both emit a .docx under Paths::documents() and record a row in
// the `documents` table. Implementations in documents.cpp.
//
#include "i_tool.h"

namespace polymath {

class DraftDocumentTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class GenerateLabReportTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    bool isDeepTask() const override { return true; }   // heavy: queue it
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath

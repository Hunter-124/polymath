#pragma once
// Wave Z complete: optional IMAP inbox via host/user/pass settings (app password).
#include "i_tool.h"

namespace polymath {

class ImapFetchTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath

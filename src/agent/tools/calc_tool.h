#pragma once
//
// calculate — a deterministic arithmetic / scientific expression evaluator.
// LLMs are unreliable at mental math; this gives exact results for the home +
// lab calculations the assistant is asked to do (concentrations, conversions,
// report figures). Pure + self-contained: a hand-written recursive-descent
// parser, no eval of arbitrary code.
//
#include "i_tool.h"

namespace polymath {

class CalculateTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath

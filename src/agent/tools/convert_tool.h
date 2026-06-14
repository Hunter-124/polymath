#pragma once
//
// convert_units — exact unit conversion across the categories a home + lab
// assistant actually needs: mass, length, volume, time, pressure, and
// temperature. Lab-critical: a wrong conversion is a wrong result, so every
// factor is explicit and unit-tested, and cross-category requests fail loudly
// rather than guessing. Pure logic, no external services.
//
#include "i_tool.h"

namespace polymath {

class ConvertUnitsTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath

#pragma once
//
// Instrument tools: read_instrument (latest persisted reading) and
// record_measurement (voice/manual-entered value with range check). Both work
// against the v2 `instruments` / `measurements` tables. Implementations in
// instrument_tool.cpp.
//
#include "i_tool.h"

namespace polymath {

class ReadInstrumentTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class RecordMeasurementTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath

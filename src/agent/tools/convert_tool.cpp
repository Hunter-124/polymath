#include "convert_tool.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>

namespace polymath {
namespace {

struct Unit { const char* category; double toBase; };

// Canonical + aliased unit symbols -> (category, factor to the category's base
// unit). Bases: mass=gram, length=metre, volume=litre, time=second,
// pressure=pascal. Temperature is affine (offset), so it's handled separately.
const std::unordered_map<std::string, Unit>& table() {
    static const std::unordered_map<std::string, Unit> t = {
        // --- mass (base: gram) ---
        {"g",{"mass",1.0}}, {"gram",{"mass",1.0}}, {"grams",{"mass",1.0}},
        {"kg",{"mass",1000.0}}, {"kilogram",{"mass",1000.0}},
        {"mg",{"mass",0.001}}, {"milligram",{"mass",0.001}},
        {"ug",{"mass",1e-6}}, {"µg",{"mass",1e-6}}, {"mcg",{"mass",1e-6}},
        {"lb",{"mass",453.59237}}, {"lbs",{"mass",453.59237}}, {"pound",{"mass",453.59237}},
        {"oz",{"mass",28.349523125}}, {"ounce",{"mass",28.349523125}},
        {"t",{"mass",1e6}}, {"tonne",{"mass",1e6}},
        // --- length (base: metre) ---
        {"m",{"length",1.0}}, {"meter",{"length",1.0}}, {"metre",{"length",1.0}},
        {"km",{"length",1000.0}}, {"cm",{"length",0.01}}, {"mm",{"length",0.001}},
        {"um",{"length",1e-6}}, {"µm",{"length",1e-6}}, {"nm",{"length",1e-9}},
        {"mi",{"length",1609.344}}, {"mile",{"length",1609.344}},
        {"yd",{"length",0.9144}}, {"ft",{"length",0.3048}}, {"foot",{"length",0.3048}},
        {"in",{"length",0.0254}}, {"inch",{"length",0.0254}},
        // --- volume (base: litre) ---
        {"l",{"volume",1.0}}, {"liter",{"volume",1.0}}, {"litre",{"volume",1.0}},
        {"ml",{"volume",0.001}}, {"cl",{"volume",0.01}}, {"dl",{"volume",0.1}},
        {"ul",{"volume",1e-6}}, {"µl",{"volume",1e-6}},
        {"m3",{"volume",1000.0}}, {"cm3",{"volume",0.001}}, {"cc",{"volume",0.001}},
        {"gal",{"volume",3.785411784}}, {"gallon",{"volume",3.785411784}},
        {"qt",{"volume",0.946352946}}, {"pt",{"volume",0.473176473}},
        {"cup",{"volume",0.2365882365}}, {"floz",{"volume",0.0295735295625}},
        {"tbsp",{"volume",0.01478676478125}}, {"tsp",{"volume",0.00492892159375}},
        // --- time (base: second) ---
        {"s",{"time",1.0}}, {"sec",{"time",1.0}}, {"second",{"time",1.0}},
        {"ms",{"time",0.001}}, {"us",{"time",1e-6}},
        {"min",{"time",60.0}}, {"minute",{"time",60.0}},
        {"h",{"time",3600.0}}, {"hr",{"time",3600.0}}, {"hour",{"time",3600.0}},
        {"day",{"time",86400.0}}, {"d",{"time",86400.0}},
        {"wk",{"time",604800.0}}, {"week",{"time",604800.0}},
        // --- pressure (base: pascal) ---
        {"pa",{"pressure",1.0}}, {"kpa",{"pressure",1000.0}}, {"hpa",{"pressure",100.0}},
        {"bar",{"pressure",1e5}}, {"mbar",{"pressure",100.0}},
        {"atm",{"pressure",101325.0}}, {"psi",{"pressure",6894.757293168}},
        {"mmhg",{"pressure",133.322387415}}, {"torr",{"pressure",133.322368421}},
    };
    return t;
}

std::string norm(std::string s) {
    const size_t b = s.find_first_not_of(" \t");
    const size_t e = s.find_last_not_of(" \t");
    if (b == std::string::npos) return "";
    s = s.substr(b, e - b + 1);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isTemp(const std::string& u) {
    return u == "c" || u == "°c" || u == "celsius"
        || u == "f" || u == "°f" || u == "fahrenheit"
        || u == "k" || u == "kelvin";
}
double toCelsius(double v, const std::string& u) {
    if (u == "f" || u == "°f" || u == "fahrenheit") return (v - 32.0) * 5.0 / 9.0;
    if (u == "k" || u == "kelvin")                  return v - 273.15;
    return v;  // c
}
double fromCelsius(double c, const std::string& u) {
    if (u == "f" || u == "°f" || u == "fahrenheit") return c * 9.0 / 5.0 + 32.0;
    if (u == "k" || u == "kelvin")                  return c + 273.15;
    return c;  // c
}

std::string pretty(double v) {
    if (v == std::floor(v) && std::fabs(v) < 1e15)
        return std::to_string(static_cast<long long>(v));
    std::string t = std::to_string(v);
    if (t.find('.') != std::string::npos) {
        size_t last = t.find_last_not_of('0');
        if (t[last] == '.') --last;
        t.erase(last + 1);
    }
    return t;
}

} // namespace

std::string ConvertUnitsTool::name() const { return "convert_units"; }

std::string ConvertUnitsTool::description() const {
    return "Convert a value between units of the same kind and return the exact result. Supports "
           "mass (g, kg, mg, ug, lb, oz, tonne), length (m, km, cm, mm, um, nm, mi, yd, ft, in), "
           "volume (L, mL, uL, gal, qt, pt, cup, floz, tbsp, tsp, m3, cc), time (s, ms, min, h, "
           "day, wk), pressure (Pa, kPa, bar, atm, psi, mmHg, torr), and temperature (C, F, K). "
           "Converting between different kinds (e.g. kg to L) fails. Example: from \"C\" to \"F\".";
}

nlohmann::json ConvertUnitsTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"value", {{"type", "number"},  {"description", "The numeric value to convert"}}},
            {"from",  {{"type", "string"},  {"description", "The unit to convert FROM (e.g. \"kg\", \"C\")"}}},
            {"to",    {{"type", "string"},  {"description", "The unit to convert TO (e.g. \"g\", \"F\")"}}},
        }},
        {"required", {"value", "from", "to"}},
    };
}

ToolResult ConvertUnitsTool::invoke(const nlohmann::json& args, ToolContext& /*ctx*/) {
    if (!args.contains("value") || !args["value"].is_number())
        return {false, {{"error", "'value' must be a number"}}, "convert_units: missing/!number value"};
    const double value = args["value"].get<double>();
    const std::string from = norm(args.value("from", std::string{}));
    const std::string to   = norm(args.value("to", std::string{}));
    if (from.empty() || to.empty())
        return {false, {{"error", "'from' and 'to' units are required"}}, "convert_units: missing units"};

    double result = 0.0;
    if (isTemp(from) || isTemp(to)) {
        if (!(isTemp(from) && isTemp(to)))
            return {false, {{"error", "cannot convert between temperature and non-temperature units"}},
                    "convert_units: incompatible units"};
        result = fromCelsius(toCelsius(value, from), to);
    } else {
        const auto& t = table();
        const auto fi = t.find(from);
        const auto ti = t.find(to);
        if (fi == t.end()) return {false, {{"error", "unknown unit '" + from + "'"}},
                                   "convert_units: unknown from-unit"};
        if (ti == t.end()) return {false, {{"error", "unknown unit '" + to + "'"}},
                                   "convert_units: unknown to-unit"};
        if (std::string(fi->second.category) != ti->second.category)
            return {false,
                    {{"error", "incompatible units: " + from + " (" + fi->second.category +
                               ") cannot convert to " + to + " (" + ti->second.category + ")"}},
                    "convert_units: incompatible categories"};
        result = value * fi->second.toBase / ti->second.toBase;
    }

    const std::string text = pretty(result);
    nlohmann::json content = {{"value", value}, {"from", from}, {"to", to},
                              {"result", result}, {"text", text}};
    return {true, std::move(content),
            pretty(value) + " " + from + " = " + text + " " + to};
}

} // namespace polymath

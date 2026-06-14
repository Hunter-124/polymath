#include "calc_tool.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

// A small recursive-descent evaluator. Grammar (standard precedence; unary minus
// binds LOOSER than '^', so -2^2 == -4 as in Python/most calculators):
//   expr   : term  (('+'|'-') term)*
//   term   : unary (('*'|'/'|'%') unary)*
//   unary  : ('+'|'-') unary | power
//   power  : primary ('^' unary)?            // right-associative
//   primary: '(' expr ')' | number | ident
// idents are constants (pi, e) or function calls f(a, b, ...). Anything malformed
// throws std::runtime_error, which the tool turns into a clean ok=false result.

namespace polymath {
namespace {

struct Parser {
    std::string s;
    size_t      i = 0;
    explicit Parser(std::string in) : s(std::move(in)) {}

    void skipws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool eof()    { skipws(); return i >= s.size(); }
    char peek()   { skipws(); return i < s.size() ? s[i] : '\0'; }
    [[noreturn]] void fail(const std::string& m) { throw std::runtime_error(m); }

    double parse() {
        if (eof()) fail("empty expression");
        double v = expr();
        if (!eof()) fail("unexpected trailing input");
        return v;
    }

    double expr() {
        double v = term();
        for (;;) {
            char c = peek();
            if (c == '+')      { ++i; v += term(); }
            else if (c == '-') { ++i; v -= term(); }
            else break;
        }
        return v;
    }
    double term() {
        double v = unary();
        for (;;) {
            char c = peek();
            if (c == '*')      { ++i; v *= unary(); }
            else if (c == '/') { ++i; v /= unary(); }
            else if (c == '%') { ++i; double d = unary(); v = std::fmod(v, d); }
            else break;
        }
        return v;
    }
    double unary() {
        char c = peek();
        if (c == '-') { ++i; return -unary(); }
        if (c == '+') { ++i; return  unary(); }
        return power();
    }
    double power() {
        double b = primary();
        if (peek() == '^') { ++i; return std::pow(b, unary()); }  // right-assoc via unary
        return b;
    }
    double primary() {
        char c = peek();
        if (c == '(') {
            ++i;
            double v = expr();
            if (peek() != ')') fail("missing ')'");
            ++i;
            return v;
        }
        if (std::isdigit((unsigned char)c) || c == '.') return number();
        if (std::isalpha((unsigned char)c))             return ident();
        fail(c ? std::string("unexpected character '") + c + "'" : "unexpected end of expression");
    }
    double number() {
        skipws();
        char* end = nullptr;
        const double v = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) fail("invalid number");
        i = static_cast<size_t>(end - s.c_str());
        return v;
    }
    double ident() {
        skipws();
        const size_t start = i;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) ++i;
        const std::string name = s.substr(start, i - start);

        if (peek() != '(') {                 // constant
            if (name == "pi") return 3.14159265358979323846;
            if (name == "e")  return 2.71828182845904523536;
            fail("unknown name '" + name + "'");
        }
        ++i;                                 // consume '('
        std::vector<double> a;
        if (peek() != ')') {
            a.push_back(expr());
            while (peek() == ',') { ++i; a.push_back(expr()); }
        }
        if (peek() != ')') fail("missing ')' in call to " + name + "()");
        ++i;
        return call(name, a);
    }
    double call(const std::string& f, const std::vector<double>& a) {
        auto one = [&]() -> double {
            if (a.size() != 1) fail(f + "() takes exactly one argument");
            return a[0];
        };
        if (f == "sqrt")  return std::sqrt(one());
        if (f == "abs")   return std::fabs(one());
        if (f == "sin")   return std::sin(one());
        if (f == "cos")   return std::cos(one());
        if (f == "tan")   return std::tan(one());
        if (f == "asin")  return std::asin(one());
        if (f == "acos")  return std::acos(one());
        if (f == "atan")  return std::atan(one());
        if (f == "exp")   return std::exp(one());
        if (f == "ln")    return std::log(one());
        if (f == "log")   return std::log10(one());
        if (f == "log2")  return std::log2(one());
        if (f == "floor") return std::floor(one());
        if (f == "ceil")  return std::ceil(one());
        if (f == "round") return std::round(one());
        if (f == "pow")   { if (a.size() != 2) fail("pow() takes two arguments"); return std::pow(a[0], a[1]); }
        if (f == "min")   { if (a.empty()) fail("min() needs at least one argument");
                            double m = a[0]; for (double x : a) m = std::min(m, x); return m; }
        if (f == "max")   { if (a.empty()) fail("max() needs at least one argument");
                            double m = a[0]; for (double x : a) m = std::max(m, x); return m; }
        fail("unknown function '" + f + "()'");
    }
};

// Tidy display: integers without a trailing ".0"; otherwise trim noise.
std::string pretty(double v) {
    if (v == std::floor(v) && std::fabs(v) < 1e15)
        return std::to_string(static_cast<long long>(v));
    std::string t = std::to_string(v);          // "%f" style, 6 decimals
    if (t.find('.') != std::string::npos) {
        size_t last = t.find_last_not_of('0');
        if (t[last] == '.') --last;
        t.erase(last + 1);
    }
    return t;
}

} // namespace

std::string CalculateTool::name() const { return "calculate"; }

std::string CalculateTool::description() const {
    return "Evaluate a math expression and return the EXACT numeric result — use this for any "
           "arithmetic rather than computing it yourself. Supports + - * / %, ^ (power), "
           "parentheses, and the functions sqrt, abs, sin, cos, tan, asin, acos, atan, exp, ln, "
           "log (base 10), log2, floor, ceil, round, pow(x,y), min(...), max(...), plus the "
           "constants pi and e. Trig is in radians. Example: \"(1+2)^3 * sqrt(16)\".";
}

nlohmann::json CalculateTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"expression", {{"type", "string"},
                            {"description", "The math expression to evaluate, e.g. \"2.5 * (3 + 4)\""}}},
        }},
        {"required", {"expression"}},
    };
}

ToolResult CalculateTool::invoke(const nlohmann::json& args, ToolContext& /*ctx*/) {
    const std::string expression = args.value("expression", "");
    if (expression.empty())
        return {false, {{"error", "no expression provided"}}, "calculate: empty expression"};
    try {
        Parser p(expression);
        const double v = p.parse();
        if (!std::isfinite(v))
            return {false,
                    {{"error", "result is not a finite number (division by zero or a domain error?)"}},
                    "calculate: non-finite result"};
        const std::string text = pretty(v);
        nlohmann::json content = {{"expression", expression}, {"result", v}, {"text", text}};
        return {true, std::move(content), expression + " = " + text};
    } catch (const std::exception& e) {
        return {false, {{"error", e.what()}}, std::string("calculate: ") + e.what()};
    }
}

} // namespace polymath

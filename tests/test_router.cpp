// Router v2 + final-answer hygiene unit tests (overhaul2 A1).
//
// DETERMINISTIC (no model, no DB): all checks always run.
//   * classifyRouteHeuristic route table (B-ROUTE): media-intent verb/object
//     table + skill-trigger matching route "open a youtube video for me" and
//     "play some lofi" to Command, while "add milk to the list" / "what's 2+2"
//     stay Quick; existing Quick/Goal/Command regressions preserved.
//   * sanitizeFinalText (B-LEAK): tool-call JSON in → prose out, and the output
//     NEVER begins with '{' (raw JSON can't reach chat via the final answer).
//
// classifyRouteHeuristic consults the process-shared SkillRegistry for trigger
// matching, which lazily constructs a QObject — hence the QCoreApplication.
//
#include "agent_loop.h"

#include <QCoreApplication>

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

using namespace polymath;

namespace {

// No sanitized final answer may ever start with '{' (the leak invariant).
void assertNoLeadingBrace(const std::string& s, const char* what) {
    if (!s.empty())
        assert(s.front() != '{' && what);
    (void)what;
}

void testRouteTable() {
    // B-ROUTE: the reported failure — must route to Command now.
    assert(AgentLoop::classifyRouteHeuristic("open a youtube video for me") ==
           TurnRoute::Command);
    assert(AgentLoop::classifyRouteHeuristic("play some lofi") ==
           TurnRoute::Command);

    // Media-intent verb/object coverage.
    assert(AgentLoop::classifyRouteHeuristic("put on some music") ==
           TurnRoute::Command);
    assert(AgentLoop::classifyRouteHeuristic("watch a movie") ==
           TurnRoute::Command);
    assert(AgentLoop::classifyRouteHeuristic("pull up a netflix show") ==
           TurnRoute::Command);

    // Quick: tool-ish requests with no launch-verb + media-object pair.
    assert(AgentLoop::classifyRouteHeuristic("add milk to the list") ==
           TurnRoute::Quick);
    assert(AgentLoop::classifyRouteHeuristic("what's 2+2") ==
           TurnRoute::Quick);
    // Launch verb but non-media object stays Quick (conservative table).
    assert(AgentLoop::classifyRouteHeuristic("open the door") ==
           TurnRoute::Quick);
    assert(AgentLoop::classifyRouteHeuristic("play chess with me") ==
           TurnRoute::Quick);

    // Regressions from the original heuristic suite.
    assert(AgentLoop::classifyRouteHeuristic("open youtube") ==
           TurnRoute::Command);
    assert(AgentLoop::classifyRouteHeuristic("put on slop mode about cats") ==
           TurnRoute::Command);
    assert(AgentLoop::classifyRouteHeuristic(
               "research quantum computing and then write a report") ==
           TurnRoute::Goal);
    assert(AgentLoop::classifyRouteHeuristic(
               "make a plan to clean the garage step by step") ==
           TurnRoute::Goal);

    std::puts("  [ok] router v2 route table (B-ROUTE)");
}

void testSanitizeFinalText() {
    // (a) final_answer tool call → its answer argument, verbatim prose.
    {
        const std::string out = AgentLoop::sanitizeFinalText(
            R"({"tool":"final_answer","arguments":{"answer":"The capital of France is Paris."}})");
        assert(out == "The capital of France is Paris.");
        assertNoLeadingBrace(out, "final_answer extraction");
    }

    // (c) another tool call, no salvageable prose → synthesized digest summary.
    {
        const std::string digest = "- web_search: found 3 results\n";
        const std::string out = AgentLoop::sanitizeFinalText(
            R"({"tool":"web_search","arguments":{"query":"cats"}})", digest);
        assert(!out.empty());
        assert(out.find("web_search") != std::string::npos);
        assertNoLeadingBrace(out, "unknown-tool synthesize");
    }

    // Plain prose passes through untouched.
    {
        const std::string out = AgentLoop::sanitizeFinalText("Hello there, all done!");
        assert(out == "Hello there, all done!");
    }

    // Leading prose followed by a JSON blob → keep the prose, drop the JSON.
    {
        const std::string out = AgentLoop::sanitizeFinalText(
            R"(Sure thing! {"tool":"x","arguments":{}})");
        assert(out == "Sure thing!");
        assertNoLeadingBrace(out, "prose-before-json strip");
    }

    // Pure JSON with no digest → empty (caller substitutes a generic fallback),
    // but crucially NOT raw JSON.
    {
        const std::string out = AgentLoop::sanitizeFinalText(
            R"({"tool":"mystery","arguments":{"a":1}})");
        assertNoLeadingBrace(out, "no-digest empties, not leaks");
    }

    // Whitespace-wrapped tool call still sanitized.
    {
        const std::string out = AgentLoop::sanitizeFinalText(
            "\n\n  {\"tool\":\"final_answer\",\"arguments\":{\"answer\":\"Yes.\"}}  ");
        assert(out == "Yes.");
        assertNoLeadingBrace(out, "whitespace-wrapped");
    }

    std::puts("  [ok] final-answer sanitizer (B-LEAK): JSON in → prose out");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::puts("test_router: overhaul2 A1 (router v2 + final-answer hygiene)");
    testRouteTable();
    testSanitizeFinalText();
    std::puts("test_router: ALL CHECKS PASSED");
    return 0;
}

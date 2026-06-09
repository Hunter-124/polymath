#pragma once
//
// ActivityLog — a durable record of agent web/tool actions for the privacy /
// activity view.  Tool calls already emit a transient Notice on the EventBus;
// this persists them so the user can audit, after the fact, "what did the
// assistant do on my behalf" (web searches, page fetches, prints, etc.).
//
// The frozen schema has no dedicated activity table, so we record into the
// existing `events` table with kind="tool" (a free-text column) — which the
// Timeline/Privacy view already surfaces and which the retention sweeper already
// ages out under retention.events_days. See docs/sessions/contract-requests.md
// for the proposal to give activity its own table/category.
//
#include <string>

namespace polymath {

class Database;

class ActivityLog {
public:
    explicit ActivityLog(Database& db) : db_(db) {}

    // Records a tool/web action. `tool` is the tool name (e.g. "web_search"),
    // `summary` the human-readable line shown in the activity view, `ok` whether
    // it succeeded. Returns the events row id (or -1 on failure). Cheap; safe to
    // call from any service thread.
    int64_t record(const std::string& tool, const std::string& summary, bool ok = true);

private:
    Database& db_;
};

} // namespace polymath

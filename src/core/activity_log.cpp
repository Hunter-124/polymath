#include "activity_log.h"
#include "database.h"
#include "types.h"
#include "logging.h"

namespace polymath {

int64_t ActivityLog::record(const std::string& tool, const std::string& summary, bool ok) {
    if (tool.empty()) return -1;

    // kind="tool" groups all agent actions in the events feed; label carries the
    // tool name and the ok/failed status so the activity view can render it. We
    // reuse the events table (camera_id/user_id/thumb_path stay NULL/empty) so
    // the existing Timeline query and retention sweep pick it up unchanged.
    const std::string label =
        tool + (ok ? "" : " (failed)") + (summary.empty() ? "" : ": " + summary);

    const int64_t id = db_.exec(
        "INSERT INTO events(kind,camera_id,user_id,label,thumb_path,ts) "
        "VALUES('tool',NULL,NULL,?1,'',?2)",
        {label, to_unix(Clock::now())});

    PM_DEBUG("ActivityLog: recorded tool='{}' ok={} (#{})", tool, ok, id);
    return id;
}

} // namespace polymath

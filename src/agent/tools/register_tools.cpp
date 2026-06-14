#include "tool_registry.h"

#include "shopping_tool.h"
#include "web_search.h"
#include "fetch_page.h"
#include "browser_drive.h"
#include "documents.h"
#include "print.h"
#include "reminders.h"
#include "memory_tools.h"
#include "camera_tools.h"
#include "queue_tool.h"
#include "instrument_tool.h"
#include "lab_session.h"
#include "doc_rag.h"

#include "logging.h"

#include <memory>

// register_tools — the single place that wires every builtin ITool into the
// registry. Moved out of shopping_tool.cpp so each tool family lives in its own
// translation unit. The AgentRuntime constructs one ToolRegistry and calls this
// once; per-turn filtering against the active personality's allow-list happens
// later in ToolRegistry::specs().

namespace polymath {

void registerBuiltinTools(ToolRegistry& reg) {
    // Shopping list (SQLite).
    reg.add(std::make_shared<ShoppingAddTool>());
    reg.add(std::make_shared<ShoppingListTool>());
    reg.add(std::make_shared<ShoppingRemoveTool>());

    // Web (Qt Network).
    reg.add(std::make_shared<WebSearchTool>());
    reg.add(std::make_shared<FetchPageTool>());

    // Browser automation (Chrome via DevTools Protocol over a QTcpSocket WebSocket).
    reg.add(std::make_shared<BrowserDriveTool>());

    // Documents (.docx via OOXML) + printing (QPrinter).
    reg.add(std::make_shared<DraftDocumentTool>());
    reg.add(std::make_shared<GenerateLabReportTool>());
    reg.add(std::make_shared<PrintDocumentTool>());
    reg.add(std::make_shared<PrintImageTool>());

    // Reminders / proactive (reminders table).
    reg.add(std::make_shared<SetReminderTool>());

    // Long-term memory (memories table + vector upgrade via MemoryService).
    reg.add(std::make_shared<RememberTool>());
    reg.add(std::make_shared<RecallTool>());
    reg.add(std::make_shared<SearchMemoryTool>());

    // Home / cameras (events table + EventBus).
    reg.add(std::make_shared<CameraSnapshotTool>());
    reg.add(std::make_shared<WhoIsHomeTool>());

    // Deep-task queue (tasks table).
    reg.add(std::make_shared<QueueDeepTaskTool>());

    // Lab instruments (instruments/measurements tables + fabric readings).
    reg.add(std::make_shared<ReadInstrumentTool>());
    reg.add(std::make_shared<RecordMeasurementTool>());

    // Interactive guided lab sessions (lab_sessions/lab_session_steps state machine).
    reg.add(std::make_shared<StartLabSessionTool>());
    reg.add(std::make_shared<NextLabStepTool>());
    reg.add(std::make_shared<VerifyLabStepTool>());
    reg.add(std::make_shared<FinishLabSessionTool>());

    // Local document RAG (knowledge_files/knowledge_chunks; offline embeddings).
    reg.add(std::make_shared<SearchDocumentsTool>());
    reg.add(std::make_shared<ReindexDocumentsTool>());

    PM_INFO("registerBuiltinTools: {} tools registered", reg.names().size());
}

} // namespace polymath

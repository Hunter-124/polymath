#pragma once
//
// Pure stream-json line parser for Claude Code (`claude -p --output-format stream-json`).
// Deterministic / fixture-testable — no QProcess. Spec: 05 §2.1 + §6.
//
#include "i_agent_provider.h"
#include <QString>
#include <QVector>
#include <optional>

namespace polymath {

// Parse one stdout NDJSON line into zero or more AgentEvents.
// session_id is the Polymath id stamped on every emitted event.
// On system/init, out_native_id (if non-null) receives the CLI session_id.
// Returns empty if the line is whitespace / unrecognised / not event-worthy.
QVector<AgentEvent> parseClaudeStreamLine(const QString& line,
                                          const QString& session_id,
                                          QString* out_native_id = nullptr);

// Convenience: parse a multi-line transcript (fixture file body).
QVector<AgentEvent> parseClaudeStreamTranscript(const QString& body,
                                                const QString& session_id,
                                                QString* out_native_id = nullptr);

// True if the content looks like a permission / plan-approval / AskUserQuestion
// prompt that must be relayed as NeedsInput (never auto-approved).
bool looksLikeNeedsInput(const QString& text, const QString& raw_json);

} // namespace polymath

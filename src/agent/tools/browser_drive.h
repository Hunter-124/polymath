#pragma once
//
// browser_drive — web-automation ITool via Chrome DevTools Protocol (CDP).
// Launches Chrome with `--remote-debugging-port`, speaks CDP over a minimal
// RFC6455 WebSocket (QTcpSocket; Qt6::WebSockets not in kit). C5: persistent
// session reuse across invokes + optional Page.captureScreenshot into the
// tool result (path + short b64 prefix). Implementation in browser_drive.cpp.
//
// Why not Qt6::WebSockets? That module is NOT in the deployed Qt kit (only
// Qt6::Network is). Rather than pull a large new third-party dependency, the CDP
// transport is a small RFC6455 client implemented on QTcpSocket — CDP's socket is
// plain `ws://127.0.0.1:PORT/...` to localhost, so a masked-text-frame client is
// all that's required. See docs/sessions/reports/J-phase2.md for the contract note.
//
#include "i_tool.h"

#include <QByteArray>
#include <cstdint>

namespace polymath {

class BrowserDriveTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

// RFC6455 framing helpers for the CDP transport. Exposed here (not just internal
// to browser_drive.cpp) so the e2e test can verify the wire format without a live
// Chrome. Client->server frames are masked; server->client frames are not.
namespace cdpws {

// Encode a single masked client text frame (FIN=1, opcode=0x1) carrying `payload`.
QByteArray encodeTextFrame(const QByteArray& payload, uint32_t mask_key);

struct DecodedFrame {
    bool       complete = false;   // a full frame was available
    quint8     opcode = 0;         // 0x1 text, 0x2 binary, 0x8 close, 0x9/0xA ping/pong
    QByteArray payload;            // unmasked payload
    int        consumed = 0;       // bytes consumed from the front of the buffer
};

// Decode the first frame at the front of `buf`. complete=false (consumed=0) when
// more bytes are needed. Handles the 16-/64-bit extended length forms.
DecodedFrame decodeFrame(const QByteArray& buf);

} // namespace cdpws

} // namespace polymath

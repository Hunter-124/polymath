#include "browser_drive.h"
#include "event_bus.h"
#include "logging.h"

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QThread>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <vector>

// browser_drive — drive Chrome over the DevTools Protocol (CDP).
//
//   1. launch:    chrome --headless=new --remote-debugging-port=<p> ...
//   2. discover:  GET http://127.0.0.1:<p>/json   -> page target's
//                 webSocketDebuggerUrl (uses the shared blocking httpGet).
//   3. connect:   minimal RFC6455 WebSocket over QTcpSocket (Qt6::WebSockets is
//                 not in the kit; CDP is plain ws:// to localhost).
//   4. drive:     Page.navigate -> wait for load -> Runtime.evaluate to extract
//                 title + text; optional click / type for forms.
//   5. report:    publish a Notice so the activity log shows the automation.
//
// All blocking work (process start, socket I/O) happens on the agent worker
// thread via SYNCHRONOUS QTcpSocket waits (no nested QEventLoop), so the UI
// thread is never stalled and we avoid event-loop re-entrancy.

namespace polymath {

// Lightweight step tracing for diagnosing CDP stalls (set PM_BROWSER_DEBUG=1).
// Compiled in always but no-ops unless the env var is set; keeps the happy path
// quiet while making a hang in the wild diagnosable.
static void btrace(const char* step) {
    static const bool on = qEnvironmentVariableIsSet("PM_BROWSER_DEBUG");
    if (on) std::fprintf(stderr, "[browser_drive] %s\n", step);
}

// ===========================================================================
//  cdpws — RFC6455 framing helpers (free functions so the e2e test can verify
//  the wire format without a live Chrome).
// ===========================================================================
namespace cdpws {

// Build the bytes of a single masked client text frame carrying `payload`.
// Client->server frames MUST be masked (RFC6455 §5.3). FIN=1, opcode=0x1 (text).
QByteArray encodeTextFrame(const QByteArray& payload, uint32_t mask_key) {
    QByteArray frame;
    frame.append(static_cast<char>(0x81));   // FIN + text opcode

    const quint64 len = static_cast<quint64>(payload.size());
    const char mask_bit = static_cast<char>(0x80);   // client frames are masked
    if (len <= 125) {
        frame.append(static_cast<char>(mask_bit | static_cast<char>(len)));
    } else if (len <= 0xFFFF) {
        frame.append(static_cast<char>(mask_bit | 126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    } else {
        frame.append(static_cast<char>(mask_bit | 127));
        for (int s = 56; s >= 0; s -= 8)
            frame.append(static_cast<char>((len >> s) & 0xFF));
    }

    std::array<unsigned char, 4> mask{
        static_cast<unsigned char>((mask_key >> 24) & 0xFF),
        static_cast<unsigned char>((mask_key >> 16) & 0xFF),
        static_cast<unsigned char>((mask_key >> 8) & 0xFF),
        static_cast<unsigned char>(mask_key & 0xFF)};
    for (unsigned char b : mask) frame.append(static_cast<char>(b));

    for (int i = 0; i < payload.size(); ++i)
        frame.append(static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % 4]));
    return frame;
}

// Decode the first frame at the front of `buf` (server->client: never masked).
// Returns complete=false (consumed=0) when more bytes are needed. Handles the
// 16-/64-bit extended length forms. Continuation/fragmentation is not used by
// CDP (responses arrive as single text frames), so a single-frame decode is
// sufficient; FIN is reported via complete.
DecodedFrame decodeFrame(const QByteArray& buf) {
    DecodedFrame out;
    if (buf.size() < 2) return out;
    const auto u = [&](int i) { return static_cast<unsigned char>(buf[i]); };

    out.opcode = u(0) & 0x0F;
    const bool masked = (u(1) & 0x80) != 0;          // server frames must be unmasked
    quint64 len = u(1) & 0x7F;
    int pos = 2;
    if (len == 126) {
        if (buf.size() < pos + 2) return out;
        len = (static_cast<quint64>(u(pos)) << 8) | u(pos + 1);
        pos += 2;
    } else if (len == 127) {
        if (buf.size() < pos + 8) return out;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8) | u(pos + i);
        pos += 8;
    }
    std::array<unsigned char, 4> mask{0, 0, 0, 0};
    if (masked) {
        if (buf.size() < pos + 4) return out;
        for (int i = 0; i < 4; ++i) mask[i] = u(pos + i);
        pos += 4;
    }
    if (static_cast<quint64>(buf.size() - pos) < len) return out;   // payload incomplete

    out.payload.resize(static_cast<int>(len));
    for (quint64 i = 0; i < len; ++i) {
        unsigned char b = u(pos + static_cast<int>(i));
        out.payload[static_cast<int>(i)] = static_cast<char>(masked ? (b ^ mask[i % 4]) : b);
    }
    out.consumed = pos + static_cast<int>(len);
    out.complete = true;
    return out;
}

} // namespace cdpws

namespace {

// Block until `pred()` is true, the socket dies, or timeout — using SYNCHRONOUS
// QTcpSocket waits (no nested QEventLoop). A nested loop would re-enter / stall
// when invoke() runs under an already-spinning Qt loop (the agent worker thread,
// or a test pumping events). `pred()` is responsible for draining the socket
// (it calls readAll/pumpFrames); we just block for I/O readiness between checks.
template <typename Pred>
bool waitFor(QTcpSocket& sock, int timeout_ms, Pred pred) {
    QElapsedTimer t; t.start();
    if (pred()) return true;
    while (t.elapsed() < timeout_ms) {
        if (sock.state() == QAbstractSocket::UnconnectedState) return pred();
        // Wait for bytes (short slice so we re-check pred and the deadline often).
        sock.waitForReadyRead(static_cast<int>(
            std::min<qint64>(200, timeout_ms - t.elapsed())));
        if (pred()) return true;
    }
    return pred();
}

// A minimal CDP WebSocket session over a localhost ws:// URL.
class CdpSession {
public:
    bool connectTo(const QString& wsUrl, int timeout_ms = 8000) {
        // Parse ws://host:port/path manually. QUrl::port() has proven unreliable
        // for the `ws` scheme here (returns -1 even with an explicit port), so we
        // split the authority ourselves.
        QString rest = wsUrl;
        if (rest.startsWith("ws://"))  rest = rest.mid(5);
        else if (rest.startsWith("wss://")) rest = rest.mid(6);
        const int slash = rest.indexOf('/');
        const QString authority = slash >= 0 ? rest.left(slash) : rest;
        path_ = slash >= 0 ? rest.mid(slash) : QStringLiteral("/");
        const int colon = authority.indexOf(':');
        if (colon >= 0) {
            host_ = authority.left(colon);
            port_ = static_cast<quint16>(authority.mid(colon + 1).toUInt());
        } else {
            host_ = authority;
            port_ = 9222;
        }
        static const bool dbg = qEnvironmentVariableIsSet("PM_BROWSER_DEBUG");
        if (dbg) std::fprintf(stderr, "[browser_drive] ws connect host=%s port=%u path=%s\n",
                              host_.toStdString().c_str(), port_, path_.toStdString().c_str());

        // The page target's WS endpoint can briefly refuse right after it appears
        // in /json; retry the TCP connect a few times before giving up.
        bool connected = false;
        for (int i = 0; i < 10 && !connected; ++i) {
            sock_.abort();
            sock_.connectToHost(host_, port_);
            connected = sock_.waitForConnected(timeout_ms);
            if (!connected) QThread::msleep(200);
        }
        if (!connected) {
            err_ = "tcp connect failed: " + sock_.errorString().toStdString() +
                   " (host=" + host_.toStdString() + " port=" + std::to_string(port_) + ")";
            return false;
        }
        return handshake(timeout_ms);
    }

    // Send a CDP command and block for the matching response (by id). Returns the
    // parsed JSON "result" (or "error") object; sets ok=false on transport/timeout.
    struct Reply { bool ok = false; nlohmann::json json; std::string error; };

    Reply call(const std::string& method, const nlohmann::json& params, int timeout_ms = 15000) {
        const int id = ++last_id_;
        nlohmann::json msg = {{"id", id}, {"method", method}};
        if (!params.is_null()) msg["params"] = params;
        const std::string text = msg.dump();
        sock_.write(cdpws::encodeTextFrame(QByteArray::fromStdString(text), nextMask()));
        sock_.flush();

        nlohmann::json found;
        bool got = false;
        waitFor(sock_, timeout_ms, [&] {
            pumpFrames();
            for (auto it = inbox_.begin(); it != inbox_.end(); ++it) {
                if (it->is_object() && it->value("id", -1) == id) {
                    found = *it;
                    inbox_.erase(it);
                    got = true;
                    return true;
                }
            }
            return false;
        });
        Reply r;
        if (!got) { r.error = "timeout waiting for CDP id=" + std::to_string(id); return r; }
        if (found.contains("error")) {
            r.error = found["error"].dump();
            return r;
        }
        r.ok = true;
        r.json = found.value("result", nlohmann::json::object());
        return r;
    }

    // Block until a CDP event named `method` arrives (e.g. Page.loadEventFired),
    // or timeout. Events already buffered count.
    bool waitForEvent(const std::string& method, int timeout_ms) {
        bool seen = false;
        waitFor(sock_, timeout_ms, [&] {
            pumpFrames();
            for (auto it = inbox_.begin(); it != inbox_.end(); ++it) {
                if (it->is_object() && it->value("method", std::string{}) == method) {
                    inbox_.erase(it);
                    seen = true;
                    return true;
                }
            }
            return false;
        });
        return seen;
    }

    const std::string& error() const { return err_; }

private:
    bool handshake(int timeout_ms) {
        // RFC6455 client opening handshake. Chrome accepts any Sec-WebSocket-Key
        // and (since recent versions) requires Origin to be allow-listed via the
        // --remote-allow-origins launch flag (we pass *).
        const QByteArray key = "dGhlIHNhbXBsZSBub25jZQ==";   // fixed nonce is fine for a client
        QByteArray req;
        req += "GET " + path_.toUtf8() + " HTTP/1.1\r\n";
        req += "Host: " + host_.toUtf8() + ":" + QByteArray::number(port_) + "\r\n";
        req += "Upgrade: websocket\r\n";
        req += "Connection: Upgrade\r\n";
        req += "Sec-WebSocket-Key: " + key + "\r\n";
        req += "Sec-WebSocket-Version: 13\r\n";
        req += "Origin: http://127.0.0.1\r\n\r\n";
        sock_.write(req);
        sock_.flush();

        QByteArray hdr;
        const bool ok = waitFor(sock_, timeout_ms, [&] {
            hdr += sock_.readAll();
            return hdr.contains("\r\n\r\n");
        });
        if (!ok || !hdr.contains("\r\n\r\n")) {
            err_ = "websocket handshake: no response headers";
            return false;
        }
        const int sep = hdr.indexOf("\r\n\r\n");
        const QByteArray headerBlock = hdr.left(sep);
        if (!headerBlock.startsWith("HTTP/1.1 101")) {
            err_ = "websocket handshake rejected: " +
                   headerBlock.left(headerBlock.indexOf("\r\n")).toStdString();
            return false;
        }
        // Any bytes after the header separator are the first WS frames.
        rx_.append(hdr.mid(sep + 4));
        pumpFrames();
        return true;
    }

    void pumpFrames() {
        rx_.append(sock_.readAll());
        while (true) {
            cdpws::DecodedFrame f = cdpws::decodeFrame(rx_);
            if (!f.complete) break;
            rx_.remove(0, f.consumed);
            if (f.opcode == 0x8) { sock_.disconnectFromHost(); break; }   // close
            if (f.opcode == 0x9 || f.opcode == 0xA) continue;            // ping/pong: ignore
            if (f.opcode == 0x1 || f.opcode == 0x2) {
                auto j = nlohmann::json::parse(f.payload.toStdString(), nullptr, false);
                if (!j.is_discarded()) inbox_.push_back(std::move(j));
            }
        }
    }

    uint32_t nextMask() { return rng_(); }

    QTcpSocket sock_;
    QString    host_, path_;
    quint16    port_ = 9222;
    QByteArray rx_;
    std::vector<nlohmann::json> inbox_;
    int        last_id_ = 0;
    std::string err_;
    std::mt19937 rng_{std::random_device{}()};
};

// Locate a Chrome/Chromium/Edge executable. Returns "" if none found.
QString findChrome() {
    const QStringList candidates = {
        "C:/Program Files/Google/Chrome/Application/chrome.exe",
        "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
        QString::fromLocal8Bit(qgetenv("LOCALAPPDATA")) + "/Google/Chrome/Application/chrome.exe",
        "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
        "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
        // POSIX fallbacks (the tool is cross-platform even though we ship Windows).
        "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser",
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) return c;
    return {};
}

// Blocking HTTP/1.1 GET of a localhost path via a raw QTcpSocket (synchronous
// socket waits, NO nested QEventLoop). With Connection: close, CDP's /json
// endpoint closes the socket after the body, so reading until disconnect gives
// us the whole response. We avoid QNetworkAccessManager deliberately: its nested
// event loop can
// re-enter / stall when invoke() runs under an already-spinning Qt loop.
static QByteArray cdpHttpGet(quint16 port, const char* path, int timeout_ms) {
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, port);
    if (!s.waitForConnected(timeout_ms)) return {};
    // Chrome's embedded DevTools HTTP server expects HTTP/1.1; an HTTP/1.0 request
    // is closed without a response. Connection: close still makes it hang up after
    // the body, so reading until disconnect yields the whole reply.
    QByteArray req = "GET ";
    req += path;
    req += " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    s.write(req);
    if (!s.waitForBytesWritten(timeout_ms)) return {};

    QByteArray all;
    QElapsedTimer t; t.start();
    while (s.state() != QAbstractSocket::UnconnectedState && t.elapsed() < timeout_ms) {
        if (s.waitForReadyRead(200)) all += s.readAll();
        else if (s.state() == QAbstractSocket::UnconnectedState) break;
    }
    all += s.readAll();
    return all;
}

// Chrome writes <profile>/DevToolsActivePort once the debug server is listening:
// line 1 = the actual bound port (authoritative even if our requested port was
// taken), line 2 = the browser-target ws path. Returns the real port, or 0.
static quint16 readActivePort(const QString& profileDir) {
    QFile f(profileDir + "/DevToolsActivePort");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    const QByteArray first = f.readLine().trimmed();
    bool ok = false;
    const uint p = first.toUInt(&ok);
    return ok ? static_cast<quint16>(p) : 0;
}

// Discover the first `page` target's WebSocket URL from CDP's HTTP endpoint.
// `profileDir` lets us read the authoritative bound port from DevToolsActivePort
// (more reliable than the requested port, which Chrome may not honour).
std::string discoverPageWs(int requestedPort, const QString& profileDir, std::string& err) {
    static const bool dbg = qEnvironmentVariableIsSet("PM_BROWSER_DEBUG");
    // Wait (up to ~20s) for Chrome to publish its port, then poll /json for a page.
    for (int attempt = 0; attempt < 100; ++attempt) {
        quint16 port = readActivePort(profileDir);
        if (port == 0) port = static_cast<quint16>(requestedPort);   // fallback
        const QByteArray resp = cdpHttpGet(port, "/json", 1200);
        if (dbg && (attempt == 0 || attempt % 20 == 0))
            std::fprintf(stderr, "[browser_drive] discover attempt %d: port=%u, %d bytes\n",
                         attempt, port, (int)resp.size());
        const int sep = resp.indexOf("\r\n\r\n");
        if (sep >= 0) {
            const QByteArray body = resp.mid(sep + 4);
            auto arr = nlohmann::json::parse(body.toStdString(), nullptr, false);
            if (arr.is_array()) {
                for (const auto& t : arr) {
                    if (t.value("type", std::string{}) == "page" &&
                        t.contains("webSocketDebuggerUrl")) {
                        // Chrome may report the ws URL WITHOUT a port (e.g.
                        // ws://127.0.0.1/devtools/page/<id>) when the bound port
                        // differs from the requested one. Always rebuild the
                        // authority with the port we just reached /json on.
                        std::string raw = t["webSocketDebuggerUrl"].get<std::string>();
                        const auto pathPos = raw.find("/devtools/");
                        const std::string path =
                            pathPos != std::string::npos ? raw.substr(pathPos) : raw;
                        return "ws://127.0.0.1:" + std::to_string(port) + path;
                    }
                }
            }
        }
        QThread::msleep(200);
    }
    err = "could not reach Chrome CDP /json (requested port " +
          std::to_string(requestedPort) + ")";
    return {};
}

} // namespace

// ===========================================================================
//  ITool surface
// ===========================================================================

std::string BrowserDriveTool::name() const { return "browser_drive"; }

std::string BrowserDriveTool::description() const {
    return "Drive a real Chrome browser to automate the web via the DevTools "
           "Protocol: navigate to a URL, optionally click an element or type into "
           "a field (CSS selectors), then extract the page's title and readable "
           "text. Use for logged-in sites or pages that need scripted interaction "
           "(prefer fetch_page for simple static reads).";
}

nlohmann::json BrowserDriveTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"url",       {{"type", "string"},
                           {"description", "Absolute http(s) URL to navigate to"}}},
            {"click",     {{"type", "string"},
                           {"description", "Optional CSS selector to click after load"}}},
            {"type_into", {{"type", "string"},
                           {"description", "Optional CSS selector of an input to type into"}}},
            {"type_text", {{"type", "string"},
                           {"description", "Text to type when type_into is set"}}},
            {"extract_selector",
                          {{"type", "string"},
                           {"description", "Optional CSS selector to extract text from "
                                           "(default: whole document body)"}}},
            {"max_chars", {{"type", "integer"},
                           {"description", "Truncate extracted text to N chars (default 6000)"}}},
            {"headless",  {{"type", "boolean"},
                           {"description", "Run Chrome headless (default true)"}}},
        }},
        {"required", {"url"}},
    };
}

ToolResult BrowserDriveTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string url = args.value("url", "");
    if (url.empty())
        return {false, {{"error", "url required"}}, "browser_drive: missing url"};
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0 &&
        url.rfind("file://", 0) != 0)
        return {false, {{"error", "url must be http(s)/file"}}, "browser_drive: bad url"};

    size_t max_chars = static_cast<size_t>(args.value("max_chars", 6000));
    if (max_chars == 0 || max_chars > 64000) max_chars = 6000;
    const bool headless = args.value("headless", true);

    const QString chrome = findChrome();
    if (chrome.isEmpty()) {
        return {false,
                {{"error", "no Chrome/Chromium/Edge executable found"},
                 {"hint", "install Google Chrome; browser_drive launches it with "
                          "--remote-debugging-port"}},
                "browser_drive: Chrome not installed"};
    }

    // --- 1) launch Chrome with a debugging port + throwaway profile -----------
    const int port = 9222 + (static_cast<int>(::time(nullptr)) % 5000);   // avoid clashes
    const QString profile =
        QDir::tempPath() + QStringLiteral("/pm-browser-drive-%1").arg(port);
    QDir(profile).removeRecursively();

    QStringList chromeArgs = {
        QStringLiteral("--remote-debugging-port=%1").arg(port),
        "--remote-allow-origins=*",          // required by modern Chrome for the WS handshake
        "--no-first-run", "--no-default-browser-check",
        "--disable-gpu", "--disable-extensions",
        QStringLiteral("--user-data-dir=%1").arg(profile),
        "about:blank",
    };
    if (headless) chromeArgs.prepend("--headless=new");

    QProcess proc;
    proc.setProgram(chrome);
    proc.setArguments(chromeArgs);
    // Discard Chrome's (verbose) stdout/stderr to the null device. Otherwise the
    // OS pipe buffer fills, Chrome blocks on a logging write, and the CDP server
    // goes unresponsive — a classic QProcess deadlock when nobody drains the pipe.
    proc.setStandardOutputFile(QProcess::nullDevice());
    proc.setStandardErrorFile(QProcess::nullDevice());
    btrace("launching chrome");
    proc.start();
    if (!proc.waitForStarted(8000)) {
        return {false, {{"error", "failed to start Chrome"}},
                "browser_drive: Chrome launch failed"};
    }
    btrace("chrome started");

    // Ensure Chrome is always torn down, however we exit.
    struct Guard {
        QProcess& p; QString dir;
        ~Guard() {
            if (p.state() != QProcess::NotRunning) {
                p.terminate();
                if (!p.waitForFinished(3000)) p.kill();
                p.waitForFinished(2000);
            }
            QDir(dir).removeRecursively();
        }
    } guard{proc, profile};

    EventBus::instance().publishNotice(
        {"info", "browser",
         QStringLiteral("browser_drive: launched Chrome (port %1), navigating %2")
             .arg(port).arg(QString::fromStdString(url))});

    // --- 2) discover the page target's WS URL ---------------------------------
    btrace("discovering page ws");
    std::string discErr;
    const std::string wsUrl = discoverPageWs(port, profile, discErr);
    if (wsUrl.empty())
        return {false, {{"error", discErr}}, "browser_drive: " + discErr};
    btrace("got page ws url");

    // --- 3) connect the CDP WebSocket -----------------------------------------
    CdpSession cdp;
    if (!cdp.connectTo(QString::fromStdString(wsUrl))) {
        return {false, {{"error", cdp.error()}, {"ws", wsUrl}},
                "browser_drive: CDP connect failed (" + cdp.error() + ")"};
    }
    btrace("ws connected");

    cdp.call("Page.enable", nlohmann::json::object());
    cdp.call("Runtime.enable", nlohmann::json::object());
    btrace("page+runtime enabled");

    // --- 4) navigate + wait for load -----------------------------------------
    auto nav = cdp.call("Page.navigate", {{"url", url}}, 20000);
    if (!nav.ok)
        return {false, {{"error", nav.error}, {"url", url}},
                "browser_drive: navigate failed (" + nav.error + ")"};
    btrace("navigated");
    // Best-effort wait for the load event; proceed on timeout (SPA/no event).
    cdp.waitForEvent("Page.loadEventFired", 15000);
    btrace("load wait done");

    nlohmann::json actions = nlohmann::json::array();

    // --- 4a) optional type-into (set .value + fire input/change) --------------
    if (args.contains("type_into") && args["type_into"].is_string()) {
        const std::string sel = args["type_into"].get<std::string>();
        const std::string txt = args.value("type_text", "");
        const std::string js =
            "(function(){var e=document.querySelector(" + nlohmann::json(sel).dump() + ");"
            "if(!e)return 'no-element';"
            "e.focus();e.value=" + nlohmann::json(txt).dump() + ";"
            "e.dispatchEvent(new Event('input',{bubbles:true}));"
            "e.dispatchEvent(new Event('change',{bubbles:true}));return 'ok';})()";
        auto r = cdp.call("Runtime.evaluate",
                          {{"expression", js}, {"returnByValue", true}});
        actions.push_back({{"type", sel}, {"result",
            r.ok ? r.json["result"].value("value", std::string{"?"}) : std::string{"error"}}});
    }

    // --- 4b) optional click ---------------------------------------------------
    if (args.contains("click") && args["click"].is_string()) {
        const std::string sel = args["click"].get<std::string>();
        const std::string js =
            "(function(){var e=document.querySelector(" + nlohmann::json(sel).dump() + ");"
            "if(!e)return 'no-element';e.click();return 'ok';})()";
        auto r = cdp.call("Runtime.evaluate",
                          {{"expression", js}, {"returnByValue", true}});
        actions.push_back({{"click", sel}, {"result",
            r.ok ? r.json["result"].value("value", std::string{"?"}) : std::string{"error"}}});
        // Give a click-triggered navigation/render a moment.
        cdp.waitForEvent("Page.loadEventFired", 4000);
    }

    // --- 5) extract title + readable text -------------------------------------
    const std::string extractSel = args.value("extract_selector", "");
    const std::string textExpr =
        extractSel.empty()
            ? std::string("(document.body? document.body.innerText : '')")
            : ("(function(){var e=document.querySelector(" + nlohmann::json(extractSel).dump() +
               ");return e? e.innerText : '';})()");
    const std::string extractJs =
        "JSON.stringify({title: document.title, url: location.href, text: " + textExpr + "})";

    auto ext = cdp.call("Runtime.evaluate",
                        {{"expression", extractJs}, {"returnByValue", true}}, 15000);
    if (!ext.ok)
        return {false, {{"error", ext.error}}, "browser_drive: extract failed (" + ext.error + ")"};

    std::string title, finalUrl, text;
    {
        const std::string raw = ext.json["result"].value("value", std::string{});
        auto parsed = nlohmann::json::parse(raw, nullptr, false);
        if (parsed.is_object()) {
            title    = parsed.value("title", std::string{});
            finalUrl = parsed.value("url", std::string{});
            text     = parsed.value("text", std::string{});
        }
    }
    if (text.size() > max_chars) { text.resize(max_chars); text += "…"; }

    nlohmann::json content = {
        {"url", finalUrl.empty() ? url : finalUrl},
        {"title", title},
        {"text", text},
        {"actions", actions},
    };

    const std::string label = title.empty() ? url : title;
    EventBus::instance().publishNotice(
        {"info", "browser",
         QStringLiteral("browser_drive: extracted \"%1\" (%2 chars)")
             .arg(QString::fromStdString(label))
             .arg(static_cast<int>(text.size()))});

    return {true, std::move(content),
            "Drove browser to \"" + label + "\" (" + std::to_string(text.size()) + " chars)"};
}

} // namespace polymath

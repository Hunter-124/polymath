#include "ws_hub.h"

#include "auth.h"
#include "json_map.h"
#include "logging.h"

#include <nlohmann/json.hpp>

#include <QUrlQuery>
#include <QWebSocket>

namespace polymath {

using nlohmann::json;
namespace jm = json_map;

// ─── real-socket adapter ────────────────────────────────────────────────────
//
// Wraps a QWebSocket as an IClientChannel.  The hub parents the QWebSocket to
// itself; this adapter holds a non-owning pointer.

namespace {
class SocketChannel : public IClientChannel {
public:
    SocketChannel(QWebSocket* s, QString id) : socket_(s), id_(std::move(id)) {}
    void sendText(const QString& text) override {
        if (socket_) socket_->sendTextMessage(text);
    }
    void close() override {
        if (socket_) socket_->close();
    }
    QString id() const override { return id_; }
private:
    QWebSocket* socket_;   // owned by the hub (parented), not us
    QString     id_;
};

// Pull ?token= out of an upgrade query string.
QString tokenFromQuery(const QString& query) {
    const QUrlQuery q(query);
    return q.queryItemValue(QStringLiteral("token"), QUrl::FullyDecoded);
}
} // namespace

// ─── construction ───────────────────────────────────────────────────────────

WsHub::WsHub(Auth& auth, QObject* parent) : QObject(parent), auth_(auth) {
    connectBus();
}

WsHub::~WsHub() {
    // Channels (and any parented QWebSockets) are torn down by QObject ownership.
    clients_.clear();
}

// ─── EventBus subscription ──────────────────────────────────────────────────

void WsHub::connectBus() {
    auto& bus = EventBus::instance();

    // Each connection uses the default (auto) type: because the bus emits from
    // other threads, Qt queues delivery onto the gateway thread where this hub
    // lives, so the client map is only ever touched from one thread.

    connect(&bus, &EventBus::tokenStreamed, this, [this](const TokenChunk& t) {
        broadcast("token", jm::tokenEvent(t));
    });
    connect(&bus, &EventBus::notice, this, [this](const Notice& n) {
        broadcast("notice", jm::noticeEvent(n));
    });
    connect(&bus, &EventBus::speakRequested, this, [this](const SpeakRequest& s) {
        broadcast("speak", jm::speakEvent(s));
    });
    connect(&bus, &EventBus::utterance, this, [this](const Utterance& u) {
        broadcast("utterance", jm::utteranceEvent(u));
    });
    connect(&bus, &EventBus::detection, this, [this](const Detection& d) {
        broadcast("detection", jm::detectionEvent(d));
    });
    connect(&bus, &EventBus::findObjectDone, this, [this](const FindObjectResult& r) {
        broadcast("find_object", jm::findObjectEvent(r));
    });
    connect(&bus, &EventBus::taskUpdated, this, [this](const TaskEvent& t) {
        broadcast("task", jm::taskEvent(t));
    });
    connect(&bus, &EventBus::reminderFired, this, [this](const ReminderFired& r) {
        broadcast("reminder", jm::reminderEvent(r));
    });
    connect(&bus, &EventBus::privacyChanged, this, [this](const PrivacyChanged& p) {
        broadcast("privacy", jm::privacyEvent(p));
    });
    // --- device fabric (v2) ---
    connect(&bus, &EventBus::instrumentReading, this, [this](const InstrumentReading& r) {
        broadcast("instrument_reading", jm::instrumentReadingEvent(r));
    });
    connect(&bus, &EventBus::devicePresence, this, [this](const DevicePresence& p) {
        broadcast("device_presence", jm::devicePresenceEvent(p));
    });
    connect(&bus, &EventBus::labStep, this, [this](const LabStepEvent& s) {
        broadcast("lab_step", jm::labStepEvent(s));
    });
    // Frames are opt-in per camera, so they get their own path.
    connect(&bus, &EventBus::frameReady, this, [this](const Frame& f) {
        broadcastFrame(f);
    });
}

void WsHub::broadcast(const char* type, json data) {
    const QString frame =
        QString::fromStdString(jm::serverEvent(type, std::move(data)).dump());
    const QString topic = QString::fromLatin1(type);
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        const Client& c = it.value();
        // Empty topic set => subscribed to everything (default on connect).
        if (c.topics.isEmpty() || c.topics.contains(topic))
            c.channel->sendText(frame);
    }
}

void WsHub::broadcastFrame(const Frame& f) {
    QString frame;   // built lazily (only if someone wants this camera)
    const QString topic = QStringLiteral("frame");
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        const Client& c = it.value();
        const bool topicOk = c.topics.isEmpty() || c.topics.contains(topic);
        if (!topicOk) continue;
        // Frames are bandwidth-heavy: require an explicit camera subscription.
        if (!c.cameras.contains(f.camera_id)) continue;
        if (frame.isEmpty())
            frame = QString::fromStdString(jm::serverEvent("frame", jm::frameEvent(f)).dump());
        c.channel->sendText(frame);
    }
}

// ─── control protocol ───────────────────────────────────────────────────────

void WsHub::handleControl(const QString& clientId, const QString& text) {
    auto it = clients_.find(clientId);
    if (it == clients_.end()) return;
    Client& c = it.value();

    json msg;
    try {
        msg = json::parse(text.toStdString());
    } catch (...) {
        return;   // ignore malformed control frames
    }
    const std::string type = msg.value("type", std::string());

    if (type == "ping") {
        // Reply with a status snapshot envelope so the client can measure RTT and
        // refresh lightweight state in one go.  (Pure pong would also be fine.)
        c.channel->sendText(QString::fromStdString(
            jm::serverEvent("status", json{{"pong", true}, {"ts", jm::nowUnix()}}).dump()));
        return;
    }

    const bool subscribe   = (type == "subscribe");
    const bool unsubscribe = (type == "unsubscribe");
    if (!subscribe && !unsubscribe) return;

    if (msg.contains("topics") && msg["topics"].is_array()) {
        for (const auto& t : msg["topics"]) {
            if (!t.is_string()) continue;
            const QString topic = QString::fromStdString(t.get<std::string>());
            if (subscribe) c.topics.insert(topic);
            else           c.topics.remove(topic);
        }
    }
    if (msg.contains("camera_ids") && msg["camera_ids"].is_array()) {
        for (const auto& cid : msg["camera_ids"]) {
            if (!cid.is_number_integer()) continue;
            const int id = cid.get<int>();
            if (subscribe) c.cameras.insert(id);
            else           c.cameras.remove(id);
        }
    }
}

// ─── client lifecycle ───────────────────────────────────────────────────────

QString WsHub::registerClient(std::shared_ptr<IClientChannel> ch, const QString& deviceId) {
    Client c;
    c.channel  = std::move(ch);
    c.deviceId = deviceId;
    // Default: subscribed to all non-frame topics (topics empty == all); frames
    // stay opt-in because cameras starts empty.
    const QString id = c.channel->id();
    clients_.insert(id, std::move(c));
    emit clientCountChanged(clients_.size());
    PM_INFO("gateway/ws: client connected ({}), total={}",
            id.toStdString(), clients_.size());

    // Greet with a hello envelope carrying capabilities so the client knows it's
    // live (and which features to show).
    clients_[id].channel->sendText(QString::fromStdString(
        jm::serverEvent("status", json{{"connected", true},
                                        {"capabilities", jm::serverCapabilities()}}).dump()));
    return id;
}

void WsHub::dropClient(const QString& clientId) {
    if (clients_.remove(clientId)) {
        emit clientCountChanged(clients_.size());
        PM_INFO("gateway/ws: client gone ({}), total={}",
                clientId.toStdString(), clients_.size());
    }
}

// ─── real local sockets ─────────────────────────────────────────────────────

bool WsHub::addSocket(QWebSocket* socket, const QString& query) {
    const QString tok = tokenFromQuery(query);
    auto claims = auth_.verifyToken(tok);
    if (!claims) {
        PM_WARN("gateway/ws: rejecting socket (bad token)");
        socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "unauthorized");
        socket->deleteLater();
        return false;
    }

    socket->setParent(this);   // hub owns the socket
    // Stable, unique id for a local socket (monotonic counter).
    static quint64 s_counter = 0;
    const QString cid = QStringLiteral("ws-%1").arg(++s_counter);
    auto channel = std::make_shared<SocketChannel>(socket, cid);
    const QString id = registerClient(channel, claims->device_id);

    connect(socket, &QWebSocket::textMessageReceived, this,
            [this, id](const QString& text) { handleControl(id, text); });
    connect(socket, &QWebSocket::disconnected, this, [this, id, socket]() {
        dropClient(id);
        socket->deleteLater();
    });
    return true;
}

// ─── relay-bridged logical channels ─────────────────────────────────────────

bool WsHub::addChannel(std::shared_ptr<IClientChannel> channel, const QString& query) {
    const QString tok = tokenFromQuery(query);
    auto claims = auth_.verifyToken(tok);
    if (!claims) {
        PM_WARN("gateway/ws: rejecting relay channel (bad token)");
        return false;
    }
    registerClient(std::move(channel), claims->device_id);
    return true;
}

void WsHub::feedChannel(const QString& channelId, const QString& text) {
    handleControl(channelId, text);
}

void WsHub::removeChannel(const QString& channelId) {
    dropClient(channelId);
}

} // namespace polymath

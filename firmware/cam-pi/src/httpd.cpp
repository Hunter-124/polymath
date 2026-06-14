#include "hearth_pi/httpd.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <sstream>

namespace hearth {

Httpd::Httpd(uint16_t port, std::string deviceId, std::string kind, std::string name,
             std::string fw, Auth& auth, std::string lanHost, std::string softap,
             std::function<long()> nowFn, Hooks hooks)
    : port_(port), deviceId_(std::move(deviceId)), kind_(std::move(kind)),
      name_(std::move(name)), fw_(std::move(fw)), lanHost_(std::move(lanHost)),
      softap_(std::move(softap)), auth_(auth), now_(std::move(nowFn)),
      hooks_(std::move(hooks)) {}

static std::string lower(std::string s) { for (auto& c : s) c = tolower(c); return s; }

bool Httpd::authorized(const HttpRequest& r, const std::string& path) {
    auto it = r.headers.find("authorization");
    auto ts = r.headers.find("x-hearth-ts");
    long t = (ts != r.headers.end()) ? strtol(ts->second.c_str(), nullptr, 10) : 0;
    return auth_.verify(it != r.headers.end() ? it->second : "", path, t, now_());
}

HttpResponse Httpd::route(const HttpRequest& r) {
    HttpResponse res;
    if (r.path == "/" && r.method == "GET") {
        res.contentType = "text/html";
        std::string qr = auth_.qrPayload(deviceId_, kind_, softap_, lanHost_);
        res.body = "<h3>Hearth " + kind_ + " — " + name_ + "</h3><pre>" + qr + "</pre>";
        return res;
    }
    if (r.path == "/status" && r.method == "GET") {
        res.body = "{\"device_id\":\"" + deviceId_ + "\",\"kind\":\"" + kind_ +
                   "\",\"name\":\"" + name_ + "\",\"fw\":\"" + fw_ +
                   "\",\"lan_host\":\"" + lanHost_ + "\"";
        if (hooks_.statusExtra) { std::string x = hooks_.statusExtra(); if (!x.empty()) res.body += "," + x; }
        res.body += "}";
        return res;
    }
    if (r.path == "/snapshot" && r.method == "GET") {
        if (!authorized(r, r.path)) { res.code = 401; res.body = "{\"ok\":false}"; return res; }
        std::string jpg = hooks_.snapshotJpeg ? hooks_.snapshotJpeg() : "";
        if (jpg.empty()) { res.code = 503; res.body = "{\"ok\":false}"; return res; }
        res.contentType = "image/jpeg"; res.body = std::move(jpg); return res;
    }
    if (r.path == "/clips" && r.method == "GET") {
        if (!authorized(r, r.path)) { res.code = 401; res.body = "{\"ok\":false}"; return res; }
        res.body = hooks_.clipsListJson ? hooks_.clipsListJson() : "[]"; return res;
    }
    if (r.path.rfind("/clips/", 0) == 0 && r.method == "GET") {
        if (!authorized(r, r.path)) { res.code = 401; res.body = "{\"ok\":false}"; return res; }
        std::string file = r.path.substr(7);
        if (file.find("..") != std::string::npos) { res.code = 400; res.body = "{\"ok\":false}"; return res; }
        std::string bytes = hooks_.clipFile ? hooks_.clipFile(file) : "";
        if (bytes.empty()) { res.code = 404; res.body = "{\"ok\":false}"; return res; }
        res.contentType = "video/mp4"; res.body = std::move(bytes); return res;
    }
    if (r.path == "/config" && r.method == "GET") {
        res.body = hooks_.configJson ? hooks_.configJson() : "{}"; return res;
    }
    if (r.path == "/config" && r.method == "POST") {
        if (!authorized(r, r.path)) { res.code = 401; res.body = "{\"ok\":false}"; return res; }
        if (hooks_.onConfig) hooks_.onConfig(r.body);
        res.body = "{\"ok\":true}"; return res;
    }
    if (r.path == "/pair" && r.method == "POST") {
        if (!authorized(r, r.path)) { res.code = 401; res.body = "{\"ok\":false}"; return res; }
        auto ts = r.headers.find("x-hearth-ts");
        long t = (ts != r.headers.end()) ? strtol(ts->second.c_str(), nullptr, 10) : 0;
        res.body = "{\"ok\":true,\"device_id\":\"" + deviceId_ + "\",\"token\":\"" +
                   auth_.sign("/pair", t) + "\"}";
        return res;
    }
    if (r.path == "/edge/detect" && r.method == "POST" && hooks_.edgeDetect) {
        if (!authorized(r, r.path)) { res.code = 401; res.body = "{\"ok\":false}"; return res; }
        res.body = hooks_.edgeDetect(r.body); return res;
    }
    res.code = 404; res.body = "{\"ok\":false}"; return res;
}

void Httpd::run() {
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) != 0) { perror("bind"); return; }
    listen(srv, 8);
    for (;;) {
        int c = accept(srv, nullptr, nullptr);
        if (c < 0) continue;
        std::thread([this, c] {
            char buf[8192]; ssize_t n = recv(c, buf, sizeof(buf) - 1, 0);
            if (n <= 0) { close(c); return; }
            buf[n] = 0;
            std::string raw(buf, n);
            HttpRequest req;
            std::istringstream ss(raw);
            std::string line; std::getline(ss, line);
            { std::istringstream ls(line); std::string ver; ls >> req.method >> req.path >> ver;
              auto q = req.path.find('?'); if (q != std::string::npos) req.path = req.path.substr(0, q); }
            while (std::getline(ss, line) && line != "\r" && !line.empty()) {
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string k = lower(line.substr(0, colon));
                    std::string v = line.substr(colon + 1);
                    while (!v.empty() && (v.front() == ' ')) v.erase(v.begin());
                    while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
                    req.headers[k] = v;
                }
            }
            auto bpos = raw.find("\r\n\r\n");
            if (bpos != std::string::npos) req.body = raw.substr(bpos + 4);

            HttpResponse res = route(req);
            std::ostringstream out;
            out << "HTTP/1.1 " << res.code << " OK\r\n"
                << "Content-Type: " << res.contentType << "\r\n"
                << "Content-Length: " << res.body.size() << "\r\n"
                << "Access-Control-Allow-Origin: *\r\n\r\n";
            std::string head = out.str();
            send(c, head.data(), head.size(), 0);
            send(c, res.body.data(), res.body.size(), 0);
            close(c);
        }).detach();
    }
}

} // namespace hearth

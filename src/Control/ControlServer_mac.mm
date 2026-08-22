// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer_mac.mm — transporte UDS con dispatch sources.
// Misma ruta que Linux: $XDG_RUNTIME_DIR (o TMPDIR)/owear-<pid>.sock
//
#include "ControlServer.hpp"
#include "../Core/Log.hpp"
#include "ow/App.h"

#include <dispatch/dispatch.h>
#include <sys/socket.h>
#include <sys/ucred.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace ow {

namespace {

class UdsServer final : public ControlServer {
public:
    bool PlatformListen() override {
        const char* xdg = std::getenv("XDG_RUNTIME_DIR");
        const char* tmp = std::getenv("TMPDIR");
        std::string dir = (xdg && *xdg) ? xdg : (tmp && *tmp ? tmp : "/tmp");

        char path[512];
        std::snprintf(path, sizeof(path), "%s/owear-%d.sock", dir.c_str(),
                      static_cast<int>(getpid()));

        ::unlink(path);
        int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return false;

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            ::listen(fd, 8) < 0) {
            ::close(fd);
            return false;
        }
        listenFd_ = fd;
        socketPath_ = path;

        dispatch_queue_t q =
            dispatch_queue_create("owear.control", DISPATCH_QUEUE_SERIAL);
        acceptSource_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, fd, 0, q);
        dispatch_source_set_event_handler(acceptSource_, ^{
            while (true) {
                int cfd = ::accept4(listenFd_, nullptr, nullptr,
                                    SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (cfd < 0) break;

                // Verificación mínima de seguridad: solo aceptar clientes que
                // corran con el mismo uid que este proceso (evita que otro
                // usuario local del sistema controle la app vía el socket).
                struct xucred cred{};
                socklen_t credLen = sizeof(cred);
                if (::getsockopt(cfd, SOL_LOCAL, LOCAL_PEERCRED, &cred, &credLen) != 0 ||
                    cred.cr_uid != ::getuid()) {
                    log::Warn("control", "conexión rechazada: uid remoto no coincide");
                    ::close(cfd);
                    continue;
                }

                uint64_t id = nextClientId_++;
                clients_[id] = Client{cfd, {}};
                StartReading(id, cfd, q);
            }
        });
        dispatch_resume(acceptSource_);
        return true;
    }

    void PlatformSend(uint64_t clientId, std::string_view line) override {
        std::lock_guard lock(mu_);
        std::string out(line);
        out += '\n';
        if (clientId == 0) {
            for (auto& [id, c] : clients_) WriteAll(c.fd, out);
            return;
        }
        if (auto it = clients_.find(clientId); it != clients_.end())
            WriteAll(it->second.fd, out);
    }

    void PlatformStop() override {
        if (acceptSource_) {
            dispatch_source_cancel(acceptSource_);
            acceptSource_ = nullptr;
        }
        std::lock_guard lock(mu_);
        for (auto& [id, c] : clients_) ::close(c.fd);
        clients_.clear();
        if (listenFd_ >= 0) ::close(listenFd_), listenFd_ = -1;
        if (!socketPath_.empty()) {
            ::unlink(socketPath_.c_str());
            socketPath_.clear();
        }
    }

private:
    struct Client {
        int fd;
        std::string buf;
    };

    static void WriteAll(int fd, const std::string& data) {
        size_t off = 0;
        while (off < data.size()) {
            ssize_t n = ::write(fd, data.data() + off, data.size() - off);
            if (n <= 0) return;
            off += static_cast<size_t>(n);
        }
    }

    void StartReading(uint64_t id, int cfd, dispatch_queue_t q) {
        dispatch_source_t src = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_READ, cfd, 0, q);
        readSources_[id] = src;
        dispatch_source_set_event_handler(src, ^{
            char buf[8192];
            ssize_t n = ::read(cfd, buf, sizeof(buf));
            if (n <= 0) {
                Cleanup(id, cfd);
                return;
            }
            std::lock_guard lock(mu_);
            auto& acc = clients_[id].buf;
            acc.append(buf, static_cast<size_t>(n));
            size_t pos;
            while ((pos = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, pos);
                acc.erase(0, pos + 1);
                // AppKit/WebKit exigen tocar UI solo desde el hilo principal:
                // HandleLine (y HandleCommand, que crea/destruye NSWindow y
                // WKWebView) se despacha al main loop. La lectura/accept del
                // socket sigue en esta cola GCD dedicada (solo I/O).
                if (!line.empty()) {
                    App::Post([this, id, line] { HandleLine(id, line); });
                }
            }
        });
        dispatch_source_set_cancel_handler(src, ^{ ::close(cfd); });
        dispatch_resume(src);
    }

    void Cleanup(uint64_t id, int cfd) {
        std::lock_guard lock(mu_);
        clients_.erase(id);
        auto it = readSources_.find(id);
        if (it != readSources_.end()) {
            dispatch_source_cancel(it->second);
            readSources_.erase(it);
        }
        HandleClientDisconnected(id);
    }

    int listenFd_ = -1;
    dispatch_source_t acceptSource_ = nullptr;
    uint64_t nextClientId_ = 1;
    std::mutex mu_;
    std::map<uint64_t, Client> clients_;
    std::map<uint64_t, dispatch_source_t> readSources_;
};

} // namespace

ControlServer& ControlServer::Get() {
    static UdsServer instance;
    return instance;
}

} // namespace ow

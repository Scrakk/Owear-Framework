// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer_linux.cpp — transporte UDS sobre GLib main loop.
//
#include "ControlServer.hpp"
#include "../Core/Log.hpp"

#include <glib-unix.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
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
        std::string dir = (xdg && *xdg) ? xdg : "/tmp";

        char path[256];
        std::snprintf(path, sizeof(path), "%s/owear-%d.sock", dir.c_str(),
                      static_cast<int>(getpid()));

        ::unlink(path);
        int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return false;

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd);
            return false;
        }
        if (::listen(fd, 8) < 0) {
            ::close(fd);
            return false;
        }
        listenFd_ = fd;
        socketPath_ = path;

        sourceId_ = g_unix_fd_add(fd, G_IO_IN,
                                  [](gint fd, GIOCondition, gpointer user_data) -> gboolean {
                                      static_cast<UdsServer*>(user_data)->OnAccept(fd);
                                      return G_SOURCE_CONTINUE;
                                  },
                                  this);
        return true;
    }

    void PlatformSend(uint64_t clientId, std::string_view line) override {
        if (clientId == 0) {
            for (const auto& [id, c] : clients_) WriteAll(c.fd, line);
            return;
        }
        auto it = clients_.find(clientId);
        if (it != clients_.end()) WriteAll(it->second.fd, line);
    }

    void PlatformStop() override {
        if (sourceId_) g_source_remove(sourceId_);
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
        for (const auto& [id, c] : clients_) ::close(c.fd);
        clients_.clear();
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
    struct ClientCtx {
        UdsServer* self;
        uint64_t id;
    };

    static void WriteAll(int fd, std::string_view data) {
        std::string out(data);
        out += '\n';
        ssize_t off = 0;
        while (off < static_cast<ssize_t>(out.size())) {
            ssize_t n = ::write(fd, out.data() + off, out.size() - off);
            if (n <= 0) return;
            off += n;
        }
    }

    void OnAccept(int fd) {
        while (true) {
            int cfd = ::accept4(fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (cfd < 0) break;

            // Verificación mínima de seguridad: solo aceptar clientes que
            // corran con el mismo uid que este proceso (evita que otro
            // usuario local del sistema controle la app vía el socket).
            struct ucred cred{};
            socklen_t credLen = sizeof(cred);
            if (::getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) != 0 ||
                cred.uid != ::getuid()) {
                log::Warn("control", "conexión rechazada: uid remoto no coincide");
                ::close(cfd);
                continue;
            }

            uint64_t id = nextClientId_++;
            clients_[id] = Client{cfd, {}};
            auto* ctx = new ClientCtx{this, id};
            g_unix_fd_add_full(G_PRIORITY_DEFAULT, cfd, G_IO_IN,
                               [](gint fd, GIOCondition cond, gpointer user_data) -> gboolean {
                                   auto* ctx = static_cast<ClientCtx*>(user_data);
                                   return ctx->self->OnReadable(ctx->id, cond)
                                              ? G_SOURCE_CONTINUE
                                              : G_SOURCE_REMOVE;
                               },
                               ctx,
                               [](gpointer user_data) {
                                   delete static_cast<ClientCtx*>(user_data);
                               });
        }
    }

    bool OnReadable(uint64_t clientId, GIOCondition cond) {
        auto it = clients_.find(clientId);
        if (it == clients_.end()) return G_SOURCE_REMOVE;
        int fd = it->second.fd;

        if (cond & (G_IO_HUP | G_IO_ERR)) {
            ::close(fd);
            clients_.erase(it);
            HandleClientDisconnected(clientId);
            return G_SOURCE_REMOVE;
        }

        char buf[8192];
        ssize_t n;
        bool closed = false;
        while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
            it->second.buf.append(buf, static_cast<size_t>(n));
            if (n < static_cast<ssize_t>(sizeof(buf))) break;
        }
        if (n == 0) closed = true;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) closed = true;

        // extrae líneas completas
        auto& b = it->second.buf;
        size_t pos;
        while ((pos = b.find('\n')) != std::string::npos) {
            std::string line = b.substr(0, pos);
            b.erase(0, pos + 1);
            if (!line.empty()) HandleLine(clientId, line);
        }

        if (closed) {
            ::close(fd);
            clients_.erase(it);
            HandleClientDisconnected(clientId);
            return G_SOURCE_REMOVE;
        }
        // it puede haberse invalidado si HandleLine cerró algo — re-buscar
        return clients_.count(clientId) ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
    }

    int listenFd_ = -1;
    guint sourceId_ = 0;
    uint64_t nextClientId_ = 1;
    std::map<uint64_t, Client> clients_;
};

} // namespace

ControlServer& ControlServer::Get() {
    static UdsServer instance;
    return instance;
}

} // namespace ow

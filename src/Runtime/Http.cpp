// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/Http.cpp — cliente HTTP/1.1 + TLS (OpenSSL) sobre sockets POSIX.
// Solo lo que el Runtime Manager necesita: GET con redirects,
// Content-Length y chunked. Sin dependencia de libcurl.
//
#include "Http.hpp"

#include "../Core/Log.hpp"

#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace ow::http {

namespace {

std::once_flag g_sslInit;

struct Url {
    std::string host;
    std::string port = "443";
    std::string path = "/";
};

bool ParseUrl(const std::string& url, Url& out, std::string& error) {
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos || url.compare(0, schemeEnd, "https") != 0) {
        error = "solo se soporta https://" ;
        return false;
    }
    std::string rest = url.substr(schemeEnd + 3);
    auto pathStart = rest.find('/');
    std::string authority = pathStart == std::string::npos ? rest : rest.substr(0, pathStart);
    out.path = pathStart == std::string::npos ? "/" : rest.substr(pathStart);
    auto colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']') == std::string::npos) {
        out.host = authority.substr(0, colon);
        out.port = authority.substr(colon + 1);
    } else {
        out.host = authority;
    }
    if (out.host.empty()) { error = "URL sin host"; return false; }
    return true;
}

class TlsConnection {
public:
    ~TlsConnection() {
        if (ssl_) SSL_free(ssl_);
        if (ctx_) SSL_CTX_free(ctx_);
        if (fd_ >= 0) ::close(fd_);
    }

    bool Connect(const Url& url, std::string& error) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        int rc = ::getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &res);
        if (rc != 0 || !res) {
            error = "DNS falló para " + url.host;
            return false;
        }

        for (addrinfo* p = res; p; p = p->ai_next) {
            fd_ = ::socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol);
            if (fd_ < 0) continue;
            if (::connect(fd_, p->ai_addr, p->ai_addrlen) == 0) break;
            ::close(fd_);
            fd_ = -1;
        }
        ::freeaddrinfo(res);
        if (fd_ < 0) { error = "connect falló"; return false; }

        static SSL_CTX* sharedCtx = nullptr;
        std::call_once(g_sslInit, [] {
            OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
                             nullptr);
            sharedCtx = SSL_CTX_new(TLS_client_method());
            if (sharedCtx) SSL_CTX_set_default_verify_paths(sharedCtx);
        });
        ctx_ = sharedCtx;
        if (!ctx_) { error = "SSL_CTX_new falló"; return false; }

        ssl_ = SSL_new(ctx_);
        SSL_set_fd(ssl_, fd_);
        SSL_set_tlsext_host_name(ssl_, url.host.c_str());
        X509_VERIFY_PARAM* param = SSL_get0_param(ssl_);
        X509_VERIFY_PARAM_set1_host(param, url.host.c_str(), 0);
        SSL_set_verify(ssl_, SSL_VERIFY_PEER, nullptr);

        if (SSL_connect(ssl_) != 1) {
            unsigned long e = ERR_get_error();
            char buf[256];
            ERR_error_string_n(e, buf, sizeof(buf));
            error = std::string("TLS: ") + buf;
            return false;
        }
        return true;
    }

    bool SendAll(const void* data, size_t len) const {
        const char* p = static_cast<const char*>(data);
        while (len > 0) {
            int n = SSL_write(ssl_, p, static_cast<int>(len));
            if (n <= 0) return false;
            p += n;
            len -= static_cast<size_t>(n);
        }
        return true;
    }

    /// Lee hasta EOF o hasta que el peer cierre. false en error de transporte.
    bool ReadAll(std::string& out) const {
        char buf[16384];
        while (true) {
            int n = SSL_read(ssl_, buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, static_cast<size_t>(n));
                continue;
            }
            int err = SSL_get_error(ssl_, n);
            if (err == SSL_ERROR_ZERO_RETURN) return true;
            if (err == SSL_ERROR_SYSCALL && (errno == ECONNRESET)) return true;
            return err == SSL_ERROR_ZERO_RETURN;
        }
    }

private:
    int fd_ = -1;
    SSL_CTX* ctx_ = nullptr;
    SSL* ssl_ = nullptr;
};

struct Response {
    int status = 0;
    std::map<std::string, std::string> headers; // claves en minúsculas
    std::string body;
};

bool ParseHeaders(const std::string& head, Response& r) {
    size_t lineEnd = head.find("\r\n");
    if (lineEnd == std::string::npos) return false;
    // HTTP/1.1 200 OK
    std::istringstream ss(head.substr(9, 3));
    ss >> r.status;
    size_t pos = lineEnd + 2;
    while (pos < head.size()) {
        size_t eol = head.find("\r\n", pos);
        if (eol == std::string::npos) break;
        std::string line = head.substr(pos, eol - pos);
        pos = eol + 2;
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        for (auto& c : key) c = static_cast<char>(::tolower(c));
        size_t vstart = colon + 1;
        while (vstart < line.size() && line[vstart] == ' ') ++vstart;
        r.headers[key] = line.substr(vstart);
    }
    return r.status > 0;
}

bool RequestOnce(const Url& url, Response& resp, std::string& error) {
    TlsConnection conn;
    if (!conn.Connect(url, error)) return false;

    std::string req = "GET " + url.path +
                      " HTTP/1.1\r\nHost: " + url.host +
                      "\r\nUser-Agent: owear/0.1 (+https://owear.dev)\r\n"
                      "Accept: */*\r\nConnection: close\r\n\r\n";
    if (!conn.SendAll(req.data(), req.size())) {
        error = "envío falló";
        return false;
    }

    std::string all;
    if (!conn.ReadAll(all)) {
        error = "lectura falló";
        return false;
    }

    auto sep = all.find("\r\n\r\n");
    if (sep == std::string::npos) {
        error = "respuesta malformada";
        return false;
    }
    if (!ParseHeaders(all.substr(0, sep), resp)) {
        error = "cabeceras inválidas";
        return false;
    }
    std::string body = all.substr(sep + 4);

    auto itChunked = resp.headers.find("transfer-encoding");
    if (itChunked != resp.headers.end() &&
        itChunked->second.find("chunked") != std::string::npos) {
        // decode chunked
        size_t pos = 0;
        while (pos < body.size()) {
            auto eol = body.find("\r\n", pos);
            if (eol == std::string::npos) break;
            uint64_t sz = std::strtoull(body.substr(pos, eol - pos).c_str(), nullptr, 16);
            if (sz == 0) break;
            size_t dataStart = eol + 2;
            if (dataStart + sz > body.size()) {
                error = "chunked truncado";
                return false;
            }
            resp.body.append(body, dataStart, static_cast<size_t>(sz));
            pos = dataStart + sz + 2;
        }
    } else {
        resp.body = std::move(body);
    }
    return true;
}

} // namespace

bool DownloadToString(const std::string& url, std::string& out, std::string& error) {
    constexpr int kMaxRedirects = 8;
    std::string currentUrl = url;
    for (int i = 0; i <= kMaxRedirects; ++i) {
        Url u;
        if (!ParseUrl(currentUrl, u, error)) return false;
        Response resp;
        if (!RequestOnce(u, resp, error)) return false;

        if ((resp.status == 301 || resp.status == 302 || resp.status == 307 ||
             resp.status == 308)) {
            auto loc = resp.headers.find("location");
            if (loc == resp.headers.end()) {
                error = "redirect sin location";
                return false;
            }
            currentUrl = loc->second;
            continue;
        }
        if (resp.status < 200 || resp.status >= 300) {
            error = "HTTP " + std::to_string(resp.status) + " en " + currentUrl;
            return false;
        }
        if (out.capacity() == 0) out.reserve(resp.body.size());
        out = std::move(resp.body);
        return true;
    }
    error = "demasiados redirects";
    return false;
}

bool DownloadToFile(const std::string& url, const std::filesystem::path& dest,
                    std::string& error) {
    std::string body;
    if (!DownloadToString(url, body, error)) return false;
    std::error_code ec;
    if (dest.has_parent_path())
        std::filesystem::create_directories(dest.parent_path(), ec);
    std::ofstream f(dest, std::ios::binary | std::ios::trunc);
    if (!f) { error = "no se pudo crear " + dest.string(); return false; }
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!f) {
        error = "escritura fallida";
        std::filesystem::remove(dest, ec);
        return false;
    }
    return true;
}

} // namespace ow::http

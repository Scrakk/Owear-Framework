// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Runtime/Http.hpp — descargas con libcurl.
#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace ow::http {

struct Request {
    std::string method;                       // "GET" | "POST" | …
    std::string url;
    std::map<std::string, std::string> headers;
    std::string bodyData;
    int timeoutSecs = 30;

    Request(std::string method, std::string url);
    Request& header(std::string k, std::string v);
    Request& body(std::string b);
    Request& timeout(int secs);
};

struct Response {
    int status = 0;
    std::map<std::string, std::string> headers; // claves en minúsculas
    std::string body;
};

/// Ejecuta la request siguiendo redirects. error vacío = éxito.
Response Perform(const Request& req, std::string& error);

} // namespace ow::http

namespace ow::http {

/// Descarga a archivo. Crea directorios padre. Sigue redirects.
bool DownloadToFile(const std::string& url, const std::filesystem::path& dest,
                    std::string& error);

/// Descarga a memoria (máx ~64 MB por seguridad).
bool DownloadToString(const std::string& url, std::string& out, std::string& error);

} // namespace ow::http

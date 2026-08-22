// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// ow/Base64.h — codec base64 header-only, disponible para módulos y kernel.
// v1 del bridge transporta binarios embebidos en JSON con este codec;
// F3 introduce handles SHM para payloads grandes sin copia.
//
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ow::b64 {

inline const char* Table() {
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

inline std::string Encode(const uint8_t* data, size_t len) {
    const char* tbl = Table();
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < len) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
        i += 3;
    }
    if (i + 1 == len) {
        uint32_t v = data[i] << 16;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == len) {
        uint32_t v = (data[i] << 16) | (data[i + 1] << 8);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += "=";
    }
    return out;
}

inline std::string Encode(const std::vector<uint8_t>& v) { return Encode(v.data(), v.size()); }
inline std::string Encode(std::string_view s) {
    return Encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

/// false si hay caracteres inválidos (ignora '=' y whitespace).
inline bool Decode(std::string_view in, std::string& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    out.reserve(in.size() / 4 * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int v = val(c);
        if (v < 0) return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

inline bool Decode(std::string_view in, std::vector<uint8_t>& out) {
    std::string tmp;
    if (!Decode(in, tmp)) return false;
    out.assign(tmp.begin(), tmp.end());
    return true;
}

} // namespace ow::b64

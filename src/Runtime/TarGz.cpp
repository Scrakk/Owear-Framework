// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/TarGz.cpp — streaming: inflate por chunks + parser ustar
// incremental (sin cargar todo en RAM).
//
#include "TarGz.hpp"

#include "../Core/Log.hpp"

#include <zlib.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string.h>
#include <fstream>
#include <sys/stat.h>

namespace ow::archive {

namespace {

constexpr size_t kBlock = 512;

bool IsZeroBlock(const uint8_t* b) {
    for (size_t i = 0; i < kBlock; ++i)
        if (b[i] != 0) return false;
    return true;
}

uint64_t ParseOctal(const uint8_t* p, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = p[i];
        if (c == 0 || c == ' ') break;
        if (c < '0' || c > '7') continue;
        v = v * 8 + (c - '0');
    }
    return v;
}

std::string ParseName(const uint8_t* h) {
    // ustar: name[0..99] + prefix[345..499]
    std::string name(reinterpret_cast<const char*>(h), 100);
    name.resize(::strnlen(name.c_str(), 100));
    if (h[345] != 0) {
        std::string prefix(reinterpret_cast<const char*>(h + 345), 155);
        prefix.resize(::strnlen(prefix.c_str(), 155));
        if (!prefix.empty()) return prefix + "/" + name;
    }
    return name;
}

class TarWriter {
public:
    explicit TarWriter(const std::filesystem::path& dest) : dest_(dest) {}

    bool Begin(const std::string& longName, const std::string& name, char type,
               uint64_t size, std::string& err) {
        std::string path = longName.empty() ? name : longName;
        // normaliza y anti-traversal
        std::filesystem::path rel(path);
        if (rel.is_absolute() || path.find("..") != std::string::npos) {
            err = "ruta peligrosa en tar: " + path;
            return false;
        }
        current_ = dest_ / rel;

        if (type == '5') { // directorio
            std::error_code ec;
            std::filesystem::create_directories(current_, ec);
            return true;
        }
        if (type == 'L') { // GNU longname: el "archivo" es el nombre siguiente
            longBuf_.clear();
            longBuf_.reserve(static_cast<size_t>(size));
            return true;
        }
        if (type == '0' || type == 0) {
            auto parent = current_.parent_path();
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            file_.open(current_, std::ios::binary | std::ios::trunc);
            if (!file_) { err = "no se pudo crear " + current_.string(); return false; }
            return true;
        }
        // symlink/hardlink/pax: solo consumir datos
        return true;
    }

    void Data(const uint8_t* p, size_t n) {
        if (collectingLong_) {
            longBuf_.append(reinterpret_cast<const char*>(p), n);
            return;
        }
        if (file_.is_open()) file_.write(reinterpret_cast<const char*>(p),
                                         static_cast<std::streamsize>(n));
    }

    bool End(std::string& err, std::string& outLongName,
             const std::function<void(const std::string&)>& onEntry) {
        if (file_.is_open()) {
            file_.close();
            // permisos ejecutables para bin/node se ajustan fuera
            std::error_code ec;
            std::filesystem::permissions(
                current_, std::filesystem::perms::owner_read |
                              std::filesystem::perms::owner_write |
                              std::filesystem::perms::group_read |
                              std::filesystem::perms::others_read,
                ec);
            if (onEntry) {
                auto rel = std::filesystem::relative(current_, dest_, ec);
                if (!ec) onEntry(rel.string());
            }
        }
        if (collectingLong_) {
            outLongName = longBuf_;
            longBuf_.clear();
            collectingLong_ = false;
        }
        (void)err;
        return true;
    }

    bool collectingLong_ = false;

private:
    std::filesystem::path dest_;
    std::filesystem::path current_;
    std::ofstream file_;
    std::string longBuf_;
};

} // namespace

bool ExtractTarGz(const std::filesystem::path& tarGz,
                  const std::filesystem::path& destDir,
                  std::string& error,
                  const std::function<void(const std::string&)>& onEntry) {
    std::FILE* f = std::fopen(tarGz.c_str(), "rb");
    if (!f) { error = "no se pudo abrir " + tarGz.string(); return false; }

    std::filesystem::create_directories(destDir);
    TarWriter writer(destDir);

    z_stream zs{};
    // 15+16 = gzip automático
    if (inflateInit2(&zs, 15 + 16) != Z_OK) {
        std::fclose(f);
        error = "inflateInit2 falló";
        return false;
    }

    std::array<uint8_t, 64 * 1024> in{};
    std::array<uint8_t, 128 * 1024> out{};

    // Estado del parser tar
    uint8_t hdr[kBlock];
    size_t hdrFill = 0;
    uint64_t dataLeft = 0;
    bool inData = false;
    std::string longName;
    bool pendingLong = false;
    bool sawEnd = false;
    bool archiveEnded = false;
    bool ok = true;

    auto feed = [&](const uint8_t* p, size_t n) -> bool {
        while (n > 0) {
            if (archiveEnded) return true; // padding/trailing garbage tras el fin

            if (!inData) {
                size_t need = kBlock - hdrFill;
                size_t take = need < n ? need : n;
                std::memcpy(hdr + hdrFill, p, take);
                hdrFill += take;
                p += take;
                n -= take;
                if (hdrFill < kBlock) continue;

                hdrFill = 0;
                if (IsZeroBlock(hdr)) {
                    if (sawEnd) {
                        // dos bloques cero seguidos = fin LEGÍTIMO del archivo
                        archiveEnded = true;
                        return true;
                    }
                    sawEnd = true;
                    continue;
                }
                sawEnd = false;

                std::string name = ParseName(hdr);
                char type = static_cast<char>(hdr[156]);
                uint64_t size = ParseOctal(hdr + 124, 12);

                if (type == 'L') {
                    writer.collectingLong_ = true;
                    pendingLong = true;
                }

                if (!writer.Begin(longName, name, type, size, error)) return false;

                uint64_t rounded = (size + kBlock - 1) / kBlock * kBlock;
                if (rounded > 0) {
                    inData = true;
                    dataLeft = rounded;
                } else {
                    std::string outLong;
                    writer.End(error, outLong, onEntry);
                    if (pendingLong) { longName = outLong; pendingLong = false; }
                    else longName.clear();
                }
                continue;
            }

            size_t take = dataLeft < n ? static_cast<size_t>(dataLeft) : n;
            writer.Data(p, take);
            dataLeft -= take;
            p += take;
            n -= take;
            if (dataLeft == 0) {
                // el padding ya está incluido en dataLeft (rounded)
                std::string outLong;
                writer.End(error, outLong, onEntry);
                if (pendingLong) { longName = outLong; pendingLong = false; }
                else longName.clear();
                inData = false;
            }
        }
        return true;
    };

    int ret = Z_OK;
    while (!std::feof(f)) {
        size_t got = std::fread(in.data(), 1, in.size(), f);
        if (got == 0) break;
        zs.next_in = in.data();
        zs.avail_in = static_cast<uInt>(got);
        do {
            zs.next_out = out.data();
            zs.avail_out = static_cast<uInt>(out.size());
            ret = inflate(&zs, Z_NO_FLUSH);
            if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
                error = "inflate error " + std::to_string(ret);
                ok = false;
                break;
            }
            size_t produced = out.size() - zs.avail_out;
            if (produced > 0 && !feed(out.data(), produced)) { ok = false; break; }
        } while (zs.avail_out == 0 && ret != Z_STREAM_END);
        if (!ok) break;
    }

    inflateEnd(&zs);
    std::fclose(f);
    if (ok && !inData && hdrFill == 0) return true;
    if (ok) { error = "tar truncado"; return false; }
    return false;
}

} // namespace ow::archive

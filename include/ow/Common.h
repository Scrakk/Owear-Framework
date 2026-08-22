// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// ow/Common.h — tipos base compartidos por todo el framework.
// Este header es parte del CONTRATO PÚBLICO: cambiarlo debe romper
// la compilación de las 3 plataformas en CI (anti-drift).
//

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <memory>

#define OW_VERSION_MAJOR 0
#define OW_VERSION_MINOR 1
#define OW_VERSION_PATCH 0
#define OW_VERSION_STRING "0.1.0"

namespace ow {

/// Identificador estable de ventana dentro del proceso del kernel.
using WindowId = uint32_t;

/// Bloque de bytes crudos (payload binario del bridge, archivos, etc).
struct OwBytes {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

/// Copia segura de un OwBytes a memoria propia.
inline std::vector<uint8_t> CopyBytes(const OwBytes& b) {
    return std::vector<uint8_t>(b.data, b.data + b.size);
}

/// Resultado simple para APIs nativas (sin excepciones a través de ABI-C).
template <typename T>
class Result {
public:
    static Result Ok(T value) { return Result(std::move(value), {}); }
    static Result Err(std::string message) { return Result({}, std::move(message)); }

    bool IsOk() const { return !error_.has_value(); }
    bool IsErr() const { return error_.has_value(); }
    const T& Value() const { return value_; }
    T& Value() { return value_; }
    const std::string& Error() const { return *error_; }

private:
    Result(T v, std::string err) : value_(std::move(v)) {
        if (!err.empty()) error_ = std::move(err);
    }
    T value_;
    std::optional<std::string> error_;
};

/// Base no copiable para objetos con identidad (Window, backends...).
class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

} // namespace ow

// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// ow/detail/minjson.hpp — JSON minimalista, header-only, sin excepciones.
// Usado por el kernel (bridge/control) y disponible para módulos vía ow/Json.h.
//
// Diseño: Value inmutable tras parse; objetos preservan orden de inserción.
//

#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <optional>

namespace ow::json {

class Value;
using Array  = std::vector<Value>;
using Member = std::pair<std::string, Value>;
using Object = std::vector<Member>;

enum class Type : uint8_t { Null, Bool, Int, Double, String, ArrayT, ObjectT };

class Value {
public:
    Value() : type_(Type::Null) {}
    Value(std::nullptr_t) : type_(Type::Null) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(int v) : type_(Type::Int), int_(v) {}
    Value(int64_t v) : type_(Type::Int), int_(v) {}
    Value(uint32_t v) : type_(Type::Int), int_(v) {}
    Value(double v) : type_(IsIntegral(v) ? Type::Int : Type::Double),
                      int_(static_cast<int64_t>(v)) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(Array a) : type_(Type::ArrayT), arr_(std::move(a)) {}
    Value(Object o) : type_(Type::ObjectT), obj_(std::move(o)) {}

    Type GetType() const { return type_; }
    bool IsNull()   const { return type_ == Type::Null; }
    bool IsBool()   const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Int || type_ == Type::Double; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray()  const { return type_ == Type::ArrayT; }
    bool IsObject() const { return type_ == Type::ObjectT; }

    bool        AsBool(bool def = false) const { return IsBool() ? bool_ : def; }
    int64_t     AsInt(int64_t def = 0) const {
        if (type_ == Type::Int) return int_;
        if (type_ == Type::Double) return static_cast<int64_t>(dbl_);
        return def;
    }
    double      AsDouble(double def = 0) const {
        if (type_ == Type::Double) return dbl_;
        if (type_ == Type::Int) return static_cast<double>(int_);
        return def;
    }
    const std::string& AsString() const { return str_; }

    const Array&  AsArray()  const { return arr_; }
    const Object& AsObject() const { return obj_; }

    /// Acceso a miembro de objeto. nullptr si no existe o no es objeto.
    const Value* Find(std::string_view key) const {
        if (type_ != Type::ObjectT) return nullptr;
        for (const auto& m : obj_)
            if (m.first == key) return &m.second;
        return nullptr;
    }

    std::string Serialize() const {
        std::string out;
        out.reserve(64);
        Write(out);
        return out;
    }

private:
    static bool IsIntegral(double v) {
        return std::isfinite(v) && v >= -9.007199254740992e15 && v <= 9.007199254740992e15
            && v == static_cast<double>(static_cast<int64_t>(v));
    }

    void Write(std::string& out) const {
        switch (type_) {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += bool_ ? "true" : "false"; break;
        case Type::Int: {
            char tmp[24];
            int n = std::snprintf(tmp, sizeof(tmp), "%lld",
                                  static_cast<long long>(int_));
            out.append(tmp, static_cast<size_t>(n));
            break;
        }
        case Type::Double: {
            char tmp[36];
            int n = std::snprintf(tmp, sizeof(tmp), "%.17g", dbl_);
            // recorta precisión redundante con %g corto cuando es exacto
            char tmp2[36];
            int n2 = std::snprintf(tmp2, sizeof(tmp2), "%.15g", dbl_);
            if (n > 0 && n2 > 0 && std::strtod(tmp2, nullptr) == dbl_)
                out.append(tmp2, static_cast<size_t>(n2));
            else
                out.append(tmp, static_cast<size_t>(n));
            break;
        }
        case Type::String:
            WriteEscaped(out, str_);
            break;
        case Type::ArrayT: {
            out += '[';
            bool first = true;
            for (const auto& v : arr_) {
                if (!first) out += ',';
                first = false;
                v.Write(out);
            }
            out += ']';
            break;
        }
        case Type::ObjectT: {
            out += '{';
            bool first = true;
            for (const auto& m : obj_) {
                if (!first) out += ',';
                first = false;
                WriteEscaped(out, m.first);
                out += ':';
                m.second.Write(out);
            }
            out += '}';
            break;
        }
        }
    }

    static void WriteEscaped(std::string& out, std::string_view s) {
        out += '"';
        for (unsigned char c : s) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char tmp[8];
                    std::snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                    out += tmp;
                } else {
                    out += static_cast<char>(c);
                }
            }
        }
        out += '"';
    }

    Type type_;
    bool bool_ = false;
    int64_t int_ = 0;
    double dbl_ = 0;
    std::string str_;
    Array arr_;
    Object obj_;
};

/// Parser recursivo descendente. Devuelve nullopt y `error` en fallo.
struct ParseResult {
    std::optional<Value> value;
    std::string error;
};

inline ParseResult Parse(std::string_view text) {
    size_t pos = 0;
    ParseResult r;

    // Límite de anidamiento (objetos/arrays): evita stack overflow con JSON
    // adversarial profundamente anidado en entradas externas (bridge/control).
    static constexpr int kMaxDepth = 128;

    struct Parser {
        std::string_view t;
        size_t& pos;
        std::string err;
        int depth = 0;

        void SkipWs() {
            while (pos < t.size()) {
                char c = t[pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
                else break;
            }
        }
        bool Fail(std::string msg) { if (err.empty()) err = std::move(msg); return false; }

        bool ParseValue(Value& out) {
            SkipWs();
            if (pos >= t.size()) return Fail("fin inesperado");
            char c = t[pos];
            switch (c) {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': {
                std::string s;
                if (!ParseString(s)) return false;
                out = Value(std::move(s));
                return true;
            }
            case 't':
                return Literal("true") ? (out = Value(true), true) : false;
            case 'f':
                return Literal("false") ? (out = Value(false), true) : false;
            case 'n':
                return Literal("null") ? (out = Value(nullptr), true) : false;
            default:
                return ParseNumber(out);
            }
        }

        bool Literal(std::string_view lit) {
            if (t.compare(pos, lit.size(), lit) != 0)
                return Fail("literal inválido");
            pos += lit.size();
            return true;
        }

        bool ParseNumber(Value& out) {
            size_t start = pos;
            bool isDouble = false;
            if (pos < t.size() && (t[pos] == '-' || t[pos] == '+')) ++pos;
            while (pos < t.size()) {
                char c = t[pos];
                if (c >= '0' && c <= '9') { ++pos; continue; }
                if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                    if (c == '.' || c == 'e' || c == 'E') isDouble = true;
                    ++pos; continue;
                }
                break;
            }
            if (pos == start) return Fail("número inválido");
            std::string num(t.substr(start, pos - start));
            if (isDouble) {
                out = Value(std::strtod(num.c_str(), nullptr));
            } else {
                errno = 0;
                long long v = std::strtoll(num.c_str(), nullptr, 10);
                out = Value(static_cast<int64_t>(v));
            }
            return true;
        }

        bool ParseHex4(unsigned& out) {
            if (pos + 4 > t.size()) return Fail("\\u truncado");
            unsigned v = 0;
            for (int i = 0; i < 4; ++i) {
                char c = t[pos++];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
                else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
                else return Fail("\\u inválido");
            }
            out = v;
            return true;
        }

        void AppendUtf8(std::string& s, unsigned cp) {
            if (cp < 0x80) {
                s += static_cast<char>(cp);
            } else if (cp < 0x800) {
                s += static_cast<char>(0xC0 | (cp >> 6));
                s += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                s += static_cast<char>(0xE0 | (cp >> 12));
                s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                s += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                s += static_cast<char>(0xF0 | (cp >> 18));
                s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                s += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        bool ParseString(std::string& out) {
            ++pos; // comilla inicial ya validada por caller
            out.clear();
            while (pos < t.size()) {
                char c = t[pos++];
                if (c == '"') return true;
                if (c != '\\') { out += c; continue; }
                if (pos >= t.size()) return Fail("escape truncado");
                char e = t[pos++];
                switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    unsigned u;
                    if (!ParseHex4(u)) return false;
                    if (u >= 0xD800 && u <= 0xDBFF &&
                        pos + 1 < t.size() && t[pos] == '\\' && t[pos + 1] == 'u') {
                        pos += 2;
                        unsigned lo;
                        if (!ParseHex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF)
                            u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                        else
                            AppendUtf8(out, u), u = lo; // par inválido: emite ambos
                    }
                    AppendUtf8(out, u);
                    break;
                }
                default: return Fail("escape desconocido");
                }
            }
            return Fail("string sin cerrar");
        }

        bool ParseArray(Value& out) {
            if (++depth > kMaxDepth) return Fail("JSON demasiado anidado");
            ++pos;
            Array arr;
            SkipWs();
            if (pos < t.size() && t[pos] == ']') { ++pos; --depth; out = Value(std::move(arr)); return true; }
            while (true) {
                Value item;
                if (!ParseValue(item)) return false;
                arr.push_back(std::move(item));
                SkipWs();
                if (pos >= t.size()) return Fail("array sin cerrar");
                if (t[pos] == ',') { ++pos; continue; }
                if (t[pos] == ']') { ++pos; break; }
                return Fail("',' o ']' esperado");
            }
            --depth;
            out = Value(std::move(arr));
            return true;
        }

        bool ParseObject(Value& out) {
            if (++depth > kMaxDepth) return Fail("JSON demasiado anidado");
            ++pos;
            Object obj;
            SkipWs();
            if (pos < t.size() && t[pos] == '}') { ++pos; --depth; out = Value(std::move(obj)); return true; }
            while (true) {
                SkipWs();
                if (pos >= t.size() || t[pos] != '"')
                    return Fail("clave string esperada");
                std::string key;
                if (!ParseString(key)) return false;
                SkipWs();
                if (pos >= t.size() || t[pos] != ':')
                    return Fail("':' esperado");
                ++pos;
                Value val;
                if (!ParseValue(val)) return false;
                obj.emplace_back(std::move(key), std::move(val));
                SkipWs();
                if (pos >= t.size()) return Fail("objeto sin cerrar");
                if (t[pos] == ',') { ++pos; continue; }
                if (t[pos] == '}') { ++pos; break; }
                return Fail("',' o '}' esperado");
            }
            --depth;
            out = Value(std::move(obj));
            return true;
        }
    };

    Parser p{text, pos};
    Value root;
    if (!p.ParseValue(root)) {
        r.error = p.err.empty() ? "JSON inválido" : p.err;
        return r;
    }
    r.value = std::move(root);
    return r;
}

/// Escape de un string para incrustarlo como literal JS entre comillas simples.
/// Se usa para inyectar payloads vía evaluate_javascript/postMessage roundtrips.
inline std::string JsLiteral(std::string_view s) {
    std::string out = "'";
    for (char c : s) {
        switch (c) {
        case '\'': out += "\\'"; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char tmp[8];
                std::snprintf(tmp, sizeof(tmp), "\\u%04x", c);
                out += tmp;
            } else {
                out += c;
            }
        }
    }
    out += '\'';
    return out;
}

} // namespace ow::json

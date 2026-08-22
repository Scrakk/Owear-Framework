// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "ow/Bridge/Codec.h"

#include "ow/Base64.h"
#include "ow/detail/minjson.hpp"

namespace ow::bridge {

bool DecodeMessage(std::string_view text, Message& out) {
    auto parsed = json::Parse(text);
    if (!parsed.value || !parsed.value->IsObject()) return false;
    const auto& obj = parsed.value->AsObject();

    const json::Value* t = nullptr;
    for (const auto& m : obj) {
        if (m.first == "t") t = &m.second;
    }
    if (!t || !t->IsString()) return false;
    std::string_view type = t->AsString();

    auto getStr = [&](std::string_view key, std::string& dst) {
        if (const json::Value* v = parsed.value->Find(key); v && v->IsString())
            dst = v->AsString();
    };
    auto getU64 = [&](std::string_view key, uint64_t& dst) {
        if (const json::Value* v = parsed.value->Find(key); v && v->IsNumber())
            dst = static_cast<uint64_t>(v->AsInt());
    };

    out.json.clear();
    out.bin.clear();
    out.module.clear();
    out.method.clear();
    out.name.clear();

    if (type == "invoke") {
        out.type = MsgType::Invoke;
        uint64_t id = 0;
        uint64_t window = 0;
        getU64("id", id);
        getU64("w", window);
        out.id = id;
        out.window = static_cast<WindowId>(window);
        getStr("m", out.module);
        getStr("f", out.method);
        // args: serializa el array tal cual; default "[]"
        if (const json::Value* a = parsed.value->Find("a"); a)
            out.json = a->Serialize();
        else
            out.json = "[]";
        // binario opcional embebido en base64
        if (const json::Value* b = parsed.value->Find("b"); b && b->IsString()) {
            std::vector<uint8_t> decoded;
            if (ow::b64::Decode(b->AsString(), decoded)) out.bin = std::move(decoded);
        }
        return !out.module.empty() && !out.method.empty();
    }

    return false;
}

std::string EncodeInvokeResult(uint64_t id, bool ok,
                               std::string_view resultJson,
                               const uint8_t* bin, size_t binLen) {
    std::string out = "{\"t\":\"result\",\"id\":";
    out += std::to_string(id);
    out += ",\"ok\":";
    out += ok ? "true" : "false";
    out += ",\"r\":";
    out.append(resultJson.empty() ? "null" : resultJson);
    if (bin && binLen > 0) {
        out += ",\"b\":\"";
        out += ow::b64::Encode(bin, binLen);
        out += '"';
    }
    out += '}';
    return out;
}

std::string EncodeEvent(WindowId window, std::string_view name,
                        std::string_view jsonPayload) {
    std::string out = "{\"t\":\"event\",\"w\":";
    out += std::to_string(window);
    out += ",\"n\":";
    out += json::Value(std::string(name)).Serialize();
    out += ",\"p\":";
    out.append(jsonPayload.empty() ? "null" : jsonPayload);
    out += '}';
    return out;
}

} // namespace ow::bridge

// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// ow/Bridge/Codec.h — codificación de mensajes del puente JS ↔ nativo.
//
// Wire format v1 (text frames; los binarios viajan embebidos):
//   invoke:       {"t":"invoke","id":N,"m":"module","f":"fn","a":[...],"w":windowId}
//   invokeResult: {"t":"result","id":N,"ok":true|false,"r":<json>,"b":"<base64>"}
//   event:        {"t":"event","w":windowId,"n":"name","p":<json>}
//
// El codec es el punto único donde F3 sustituirá base64 por handles SHM.
//

#include "ow/Common.h"

namespace ow::bridge {

enum class MsgType : uint8_t { Invoke, InvokeResult, Event };

struct Message {
    MsgType type = MsgType::Invoke;
    uint64_t id = 0;          // correlación invoke/result
    WindowId window = 0;      // ventana invocante
    WindowId to = 0;          // evento dirigido (0 = broadcast)
    std::string module;
    std::string method;
    std::string name;         // evento
    std::string json;         // args (array) | result | payload — texto JSON válido
    std::vector<uint8_t> bin; // binario adjunto opcional (base64 en v1)
};

/// Decodifica un frame de texto proveniente del WebView. false si es inválido.
bool DecodeMessage(std::string_view text, Message& out);

/// Serializa un resultado de invoke para inyectarlo vía evalJS.
/// `resultJson` debe ser JSON válido ("null" permitido).
std::string EncodeInvokeResult(uint64_t id, bool ok,
                               std::string_view resultJson,
                               const uint8_t* bin = nullptr, size_t binLen = 0);

/// Serializa un evento para inyectarlo vía evalJS (batch-friendly).
std::string EncodeEvent(WindowId window, std::string_view name,
                        std::string_view jsonPayload);
/// Evento global (todas las ventanas): window == 0.
inline std::string EncodeBroadcast(std::string_view name, std::string_view json) {
    return EncodeEvent(0, name, json);
}

} // namespace ow::bridge

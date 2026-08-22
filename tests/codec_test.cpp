#include "ow/Base64.h"
#include "ow/Bridge/Codec.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace ow::bridge;
using namespace ow::b64;

static void Run() {
    // base64 roundtrip con tamaños borde
    for (size_t n : {0u, 1u, 2u, 3u, 4u, 57u, 1000u}) {
        std::string raw;
        for (size_t i = 0; i < n; ++i) raw += static_cast<char>((i * 31) & 0xFF);
        auto enc = Encode(raw);
        std::string dec;
        assert(Decode(enc, dec));
        assert(dec == raw);
    }

    Message m;
    assert(DecodeMessage(
        R"({"t":"invoke","id":7,"m":"fs","f":"readText","a":["/tmp/x"],"w":2})", m));
    assert(m.type == MsgType::Invoke && m.id == 7 && m.module == "fs" &&
           m.method == "readText" && m.window == 2);
    assert(m.json == R"(["/tmp/x"])");

    // inválido
    assert(!DecodeMessage("garbage", m));
    assert(!DecodeMessage(R"({"t":"unknown"})", m));

    // result encoding
    std::string res = EncodeInvokeResult(42, true, "\"ok\"");
    assert(res.find("\"id\":42") != std::string::npos);
    assert(res.find("\"ok\":true") != std::string::npos);

    uint8_t bin[] = {1, 2, 3};
    std::string res2 = EncodeInvokeResult(1, false, "null", bin, 3);
    assert(res2.find("\"b\":\"AQID\"") != std::string::npos);

    std::string ev = EncodeEvent(5, "resize", "{\"w\":100}");
    assert(ev.find("\"n\":\"resize\"") != std::string::npos);
}
int main() { Run(); std::puts("codec OK"); return 0; }

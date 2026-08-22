// Test de minjson: parseo, escapes, surrogate pairs, serialización.
#include "ow/detail/minjson.hpp"
#include <cassert>
#include <cstdio>

using namespace ow::json;

static void Run() {
    // básicos
    auto r = Parse(R"({"a":1,"b":[true,false,null],"c":"x\ny","d":-2.5})");
    assert(r.value);
    const Value& v = *r.value;
    assert(v.Find("a")->AsInt() == 1);
    assert(v.Find("b")->AsArray().size() == 3);
    assert(v.Find("b")->AsArray()[0].AsBool());
    assert(v.Find("b")->AsArray()[2].IsNull());
    assert(v.Find("c")->AsString() == "x\ny");
    assert(v.Find("d")->AsDouble() == -2.5);

    // unicode escape + surrogates (😀 = D83D DE00)
    auto r2 = Parse("\"\\u00e9\\ud83d\\ude00\"");
    assert(r2.value);
    assert(r2.value->AsString() == "\xc3\xa9\xf0\x9f\x98\x80");

    // round-trip
    std::string ser = v.Serialize();
    auto r3 = Parse(ser);
    assert(r3.value);
    assert(r3.value->Serialize() == ser);

    // errores
    assert(!Parse("{bad").value);
    assert(!Parse("[1,]").value.has_value());
    assert(!Parse("'no json'").value.has_value());
    assert(!Parse("{\"a\":}").value.has_value());

    // números grandes y exponentes
    auto r4 = Parse("[1e10, 0.25]");
    assert(r4.value && r4.value->AsArray()[0].AsDouble() == 1e10);

    // JsLiteral
    assert(JsLiteral("a'b") == "'a\\'b'");
}
int main() { Run(); std::puts("minjson OK"); return 0; }

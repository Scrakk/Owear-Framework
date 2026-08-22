#include "../src/Runtime/Sha256.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using ow::crypto::Sha256;

static std::string Hex(const std::string& input) {
    Sha256 h;
    h.Update(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    return h.Hex();
}
int main() {
    // vectores oficiales FIPS 180-2 / NIST
    assert(Hex("") ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(Hex("abc") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // 1 millón de 'a'
    Sha256 big;
    std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) big.Update(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size());
    assert(big.Hex() ==
           "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    std::puts("sha256 OK");
    return 0;
}

// F3.1 — roundtrip del registro SHM (put → data → shutdown).
#include "../src/Bridge/Shm.hpp"
#include <cstring>
#include <string>
#include <cassert>
#include <cstdio>

using ow::shm::Put;
using ow::shm::Data;

int main() {
    std::string payload = "OWEAR-SHM-ROUNDTRIP-0123456789";
    const char* id = Put(reinterpret_cast<const uint8_t*>(payload.data()),
                         payload.size());
    assert(id && *id);

    size_t len = 0;
    const uint8_t* data = Data(id, &len);
    assert(data != nullptr);
    assert(len == payload.size());
    assert(std::memcmp(data, payload.data(), len) == 0);

    // id inexistente
    assert(Data("no-existe", nullptr) == nullptr);
    assert(Data("", &len) == nullptr);

    // segunda región con id distinto
    const char* id2 = Put(reinterpret_cast<const uint8_t*>("x"), 1);
    assert(id2 && std::string(id2) != id);

    ow::shm::Shutdown();
    assert(Data(id, &len) == nullptr); // tras shutdown ya no existe

    std::puts("shm OK");
    return 0;
}

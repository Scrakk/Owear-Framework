// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Runtime/Sha256.hpp
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace ow::crypto {

class Sha256 {
public:
    Sha256() { Reset(); }
    void Reset();
    void Update(const uint8_t* data, size_t len);
    void Final(uint8_t out[32]);

    /// Hex final en minúsculas.
    std::string Hex();

private:
    uint32_t state_[8];
    uint64_t bitLen_ = 0;
    uint8_t buffer_[64];
    size_t bufLen_ = 0;
};

} // namespace ow::crypto

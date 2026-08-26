// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.

#pragma once

namespace caecilia::core
{

/// Compile-time semantic version of the Caecilia core library.
struct Version
{
    static constexpr int major = 0;
    static constexpr int minor = 0;
    static constexpr int patch = 1;
};

/// @return A static, null-terminated "major.minor.patch" version string.
const char* versionString() noexcept;

} // namespace caecilia::core

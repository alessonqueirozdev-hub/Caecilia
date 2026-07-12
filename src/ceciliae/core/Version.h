/*
 * Copyright (c) 2026 Alesson Queiroz. All rights reserved.
 * Ceciliae is proprietary and confidential; unauthorized copying,
 * distribution, or use of any part is prohibited. See LICENSE.
 */

#pragma once

namespace ceciliae::core
{

/// Compile-time semantic version of the Ceciliae core library.
struct Version
{
    static constexpr int major = 0;
    static constexpr int minor = 0;
    static constexpr int patch = 1;
};

/// @return A static, null-terminated "major.minor.patch" version string.
const char* versionString() noexcept;

} // namespace ceciliae::core

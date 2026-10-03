#pragma once

namespace LeashFramework {
    // kChanged means a successful first-time or replacement binding, so anything computed for the previous one is stale
    enum class BindResult { kFailed, kUnchanged, kChanged };
}  // namespace LeashFramework

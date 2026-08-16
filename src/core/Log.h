// Internal logging. Formats printf-style, filters by level, forwards to the
// host callback supplied in EngineDesc. Never throws, never allocates on the
// hot path (messages format into a fixed stack buffer and are truncated).
#pragma once

#include <cadgeom/Types.h>

namespace cadgeom::core {

/// Process-wide sink. The engine installs the host's callback at creation and
/// clears it on release, so a stray log after shutdown is dropped rather than
/// calling into freed host state.
class Logger {
public:
    static void SetSink(LogCallback callback, void* userData) noexcept;
    static void SetLevel(LogLevel level) noexcept;
    static LogLevel GetLevel() noexcept;
    static bool IsEnabled(LogLevel level) noexcept;

    /// printf-style. Truncates at 1 KiB.
    static void Write(LogLevel level, const char* fmt, ...) noexcept;
};

} // namespace cadgeom::core

#define CG_LOG(level, ...)                                            \
    do {                                                              \
        if (::cadgeom::core::Logger::IsEnabled(level)) {              \
            ::cadgeom::core::Logger::Write(level, __VA_ARGS__);       \
        }                                                             \
    } while (false)

#define CG_TRACE(...) CG_LOG(::cadgeom::LogLevel::Trace, __VA_ARGS__)
#define CG_DEBUG(...) CG_LOG(::cadgeom::LogLevel::Debug, __VA_ARGS__)
#define CG_INFO(...) CG_LOG(::cadgeom::LogLevel::Info, __VA_ARGS__)
#define CG_WARN(...) CG_LOG(::cadgeom::LogLevel::Warning, __VA_ARGS__)
#define CG_ERROR(...) CG_LOG(::cadgeom::LogLevel::Error, __VA_ARGS__)
#define CG_FATAL(...) CG_LOG(::cadgeom::LogLevel::Fatal, __VA_ARGS__)

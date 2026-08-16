#include "core/Error.h"

#include "core/Log.h"

#include <cstdarg>
#include <cstdio>

namespace cadgeom::core {
namespace {

// Fixed buffers: the error path must not itself be able to fail on allocation.
constexpr size_t kMessageCapacity = 512;

struct ErrorState {
    CgResult result = CgResult::Ok;
    char message[kMessageCapacity] = {0};
};

thread_local ErrorState t_error;

} // namespace

CgResult SetError(CgResult result, const char* fmt, ...) noexcept {
    t_error.result = result;

    if (fmt) {
        va_list args;
        va_start(args, fmt);
        const int written =
            std::vsnprintf(t_error.message, kMessageCapacity, fmt, args);
        va_end(args);
        if (written < 0) {
            t_error.message[0] = '\0';
        }
        t_error.message[kMessageCapacity - 1] = '\0';
    } else {
        t_error.message[0] = '\0';
    }

    if (result != CgResult::Ok) {
        CG_ERROR("%s", t_error.message[0] ? t_error.message : "(unspecified failure)");
    }
    return result;
}

CgResult LastError() noexcept {
    return t_error.result;
}

const char* LastErrorMessage() noexcept {
    return t_error.message;
}

void ClearError() noexcept {
    t_error.result = CgResult::Ok;
    t_error.message[0] = '\0';
}

} // namespace cadgeom::core

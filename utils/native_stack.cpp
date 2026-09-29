#include "native_stack.h"

#include <pthread.h>

namespace perunejs {
    namespace {
        thread_local std::uintptr_t segment_base = 0;
        thread_local std::size_t segment_size = 0;
    }

    std::uintptr_t native_stack_base() {
        if (segment_base != 0) return segment_base;

        return reinterpret_cast<std::uintptr_t>(pthread_get_stackaddr_np(pthread_self()));
    }

    std::uintptr_t native_stack_limit(std::size_t margin_bytes) {
        const std::size_t size = segment_base != 0
            ? segment_size
            : pthread_get_stacksize_np(pthread_self());

        return native_stack_base() - size + margin_bytes;
    }

    NativeStackSegment::NativeStackSegment(const std::uintptr_t base, const std::size_t size)
        : previous_base(segment_base), previous_size(segment_size) {
        segment_base = base;
        segment_size = size;
    }

    NativeStackSegment::~NativeStackSegment() {
        segment_base = previous_base;
        segment_size = previous_size;
    }
}

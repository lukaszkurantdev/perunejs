#ifndef PERUNEJS_NATIVE_STACK_H
#define PERUNEJS_NATIVE_STACK_H

#include <cstddef>
#include <cstdint>

namespace perunejs {
    std::uintptr_t native_stack_base();
    std::uintptr_t native_stack_limit(std::size_t margin_bytes = 256 * 1024);

    inline std::uintptr_t native_stack_pointer() {
        char here = 0;
        return reinterpret_cast<std::uintptr_t>(&here);
    }

    class NativeStackSegment {
        std::uintptr_t previous_base;
        std::size_t previous_size;

    public:
        NativeStackSegment(std::uintptr_t base, std::size_t size);
        ~NativeStackSegment();

        NativeStackSegment(const NativeStackSegment &) = delete;
        NativeStackSegment &operator=(const NativeStackSegment &) = delete;
    };
}


#endif //PERUNEJS_NATIVE_STACK_H

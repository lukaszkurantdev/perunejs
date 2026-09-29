#ifndef PERUNEJS_PERSISTENT_H
#define PERUNEJS_PERSISTENT_H

#include "heap.h"
#include "js_value.h"

namespace perunejs {
    class PersistentValue {
        Heap *owner = nullptr;
        uint32_t slot = 0;

    public:
        PersistentValue() = default;

        PersistentValue(Heap &heap, const JSValue &value)
            : owner(&heap), slot(heap.acquire_handle(value)) {}

        ~PersistentValue() { if (owner != nullptr) owner->release_handle(slot); }

        PersistentValue(const PersistentValue &other) : owner(other.owner), slot(other.slot) {
            if (owner != nullptr) owner->retain_handle(slot);
        }

        PersistentValue &operator=(const PersistentValue &other) {
            if (this == &other) return *this;

            if (other.owner != nullptr) other.owner->retain_handle(other.slot);
            if (owner != nullptr) owner->release_handle(slot);

            owner = other.owner;
            slot = other.slot;

            return *this;
        }

        PersistentValue(PersistentValue &&other) noexcept : owner(other.owner), slot(other.slot) {
            other.owner = nullptr;
        }

        PersistentValue &operator=(PersistentValue &&other) noexcept {
            if (this == &other) return *this;

            if (owner != nullptr) owner->release_handle(slot);

            owner = other.owner;
            slot = other.slot;
            other.owner = nullptr;

            return *this;
        }

        bool empty() const { return owner == nullptr; }

        JSValue get() const { return owner != nullptr ? owner->handle_value(slot) : JSValue::undefined(); }

        void set(const JSValue &value) { if (owner != nullptr) owner->set_handle_value(slot, value); }

        void reset() {
            if (owner == nullptr) return;

            owner->release_handle(slot);
            owner = nullptr;
        }
    };
}

#endif //PERUNEJS_PERSISTENT_H


#include "heap.h"

#include <csetjmp>
#include <vector>

#include "utils/native_stack.h"
#include "cell.h"
#include "js_object.h"

namespace perunejs {
    namespace {
        class MarkingVisitor final : public CellVisitor {
            public:
            std::vector<Cell *> worklist;

            std::vector<Cell *> marked;

            void visit(Cell *cell) override {
                if (cell == nullptr || cell->marked) return;

                cell->marked = true;
                marked.push_back(cell);
                worklist.push_back(cell);
            }
        };
    }

    Heap::Heap() : stress(std::getenv("PERUN_GC_STRESS") != nullptr) {}

    WeakHandle::WeakHandle(Heap &heap, JSObject *target) : owner(&heap), target(target) {
        owner->weak_handles.push_back(this);
    }

    WeakHandle::WeakHandle(const WeakHandle &other) : owner(other.owner), target(other.target) {
        if (owner != nullptr) owner->weak_handles.push_back(this);
    }

    WeakHandle &WeakHandle::operator=(const WeakHandle &other) {
        if (this == &other) return *this;

        if (owner != nullptr) {
            const auto found = std::find(owner->weak_handles.begin(), owner->weak_handles.end(), this);
            if (found != owner->weak_handles.end()) owner->weak_handles.erase(found);
        }

        owner = other.owner;
        target = other.target;

        if (owner != nullptr) owner->weak_handles.push_back(this);

        return *this;
    }

    WeakHandle::~WeakHandle() {
        if (owner == nullptr) return;

        const auto found = std::find(owner->weak_handles.begin(), owner->weak_handles.end(), this);
        if (found != owner->weak_handles.end()) owner->weak_handles.erase(found);
    }

    void Heap::remove_weak_container(WeakContainer *container) {
        const auto found = std::find(weak_containers.begin(), weak_containers.end(), container);
        if (found != weak_containers.end()) weak_containers.erase(found);
    }

    void Heap::clear_dead_weak_handles() {
        for (WeakHandle *handle : weak_handles) {
            if (handle->target != nullptr && !handle->target->marked) handle->target = nullptr;
        }
    }

    uint32_t Heap::acquire_handle(const JSValue &value) {
        if (first_free_handle != NO_SLOT) {
            const uint32_t slot = first_free_handle;
            first_free_handle = handles[slot].next_free;

            handles[slot].value = value;
            handles[slot].references = 1;

            return slot;
        }

        handles.push_back(HandleSlot{value, 1, NO_SLOT});

        return static_cast<uint32_t>(handles.size() - 1);
    }

    void Heap::retain_handle(const uint32_t slot) {
        if (slot < handles.size() && handles[slot].references > 0) ++handles[slot].references;
    }

    void Heap::release_handle(const uint32_t slot) {
        if (slot >= handles.size() || handles[slot].references == 0) return;

        if (--handles[slot].references > 0) return;

        handles[slot].value = JSValue::undefined();
        handles[slot].next_free = first_free_handle;
        first_free_handle = slot;
    }

    JSValue Heap::handle_value(const uint32_t slot) const {
        if (slot >= handles.size() || handles[slot].references == 0) return JSValue::undefined();

        return handles[slot].value;
    }

    void Heap::set_handle_value(const uint32_t slot, const JSValue &value) {
        if (slot < handles.size() && handles[slot].references > 0) handles[slot].value = value;
    }

    std::size_t Heap::live_handles() const {
        std::size_t live = 0;
        for (const HandleSlot &slot : handles) {
            if (slot.references > 0) ++live;
        }

        return live;
    }

    Heap::~Heap() {
        for (Cell *cell :cells) delete cell;
    }

    Cell *Heap::find_cell(std::uintptr_t addr) const {
        auto it = index.upper_bound(addr);
        if (it == index.begin()) return nullptr;

        --it;
        return addr < it->first + it->second.size ? it->second.cell : nullptr;
    }

    __attribute__((noinline, no_sanitize_address))
    void Heap::scan_range(std::uintptr_t begin, std::uintptr_t end, std::size_t step,
                          CellVisitor &visitor) const {
        begin = (begin + step - 1) & ~(step - 1);

        for (std::uintptr_t p = begin; p + sizeof(std::uintptr_t) <= end; p += step) {
            std::uintptr_t word;
            std::memcpy(&word, reinterpret_cast<const void *>(p), sizeof(word));

            if (Cell *cell = find_cell(word)) visitor.visit(cell);
        }
    }

    __attribute__((noinline, no_sanitize_address))
    void Heap::scan_stack(CellVisitor &visitor) const {
        std::jmp_buf registers;
        setjmp(registers);

        const auto registers_begin = reinterpret_cast<std::uintptr_t>(&registers);
        scan_range(registers_begin, registers_begin + sizeof(registers), 4, visitor);

        const auto top = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
        scan_range(top, native_stack_base(), sizeof(std::uintptr_t), visitor);
    }

    void Heap::collect_garbage() {
        MarkingVisitor visitor;

        for (RootSource *source : root_sources) source->trace_roots(visitor);

        for (const HandleSlot &slot : handles) {
            if (slot.references == 0) continue;
            if (JSObject *object = slot.value.as_object()) visitor.visit(object);
        }

        scan_stack(visitor);

        const auto drain = [&visitor] {
            while (!visitor.worklist.empty()) {
                Cell *cell = visitor.worklist.back();
                visitor.worklist.pop_back();
                cell->trace(visitor);
            }
        };

        drain();

        // WeakMap żyje tylko dopóki żyje jej klucz! a sama może być kluczem innego wpisu
        if (!weak_containers.empty()) {
            std::size_t before = visitor.marked.size();

            while (true) {
                for (WeakContainer *container : weak_containers) {
                    container->mark_reachable_values(visitor);
                }

                drain();

                if (visitor.marked.size() == before) break;
                before = visitor.marked.size();
            }
        }

        clear_dead_weak_handles();

        for (WeakContainer *container : weak_containers) container->purge_dead_entries();

        std::size_t kept = 0;
        for (Cell *cell : cells) {
            if (cell->marked) {
                cells[kept++] = cell;
                continue;
            }

            index.erase(reinterpret_cast<std::uintptr_t>(dynamic_cast<void *>(cell)));
            delete cell;
        }
        cells.resize(kept);

        for (Cell *cell : visitor.marked) cell->marked = false;

        ++collections;
        next_collection = std::max(MIN_COLLECTION_THRESHOLD, cells.size() * 2);
    }

}



#ifndef PERUNEJS_HEAP_H
#define PERUNEJS_HEAP_H

#include <cstddef>
#include <map>
#include <type_traits>
#include <utility>
#include <vector>

#include "cell.h"
#include "js_value.h"

namespace perunejs {

    class RootSource {
    public:
        virtual ~RootSource() = default;
        virtual void trace_roots(CellVisitor &visitor) = 0;
    };

    class Heap;

    class WeakContainer {
    public:
        virtual ~WeakContainer() = default;
        virtual void mark_reachable_values(CellVisitor &visitor) = 0;
        virtual void purge_dead_entries() = 0;
    };

    class WeakHandle {
        Heap *owner = nullptr;
        JSObject *target = nullptr;

        friend class Heap;

    public:
        WeakHandle() = default;
        WeakHandle(Heap &heap, JSObject *target);
        ~WeakHandle();

        WeakHandle(const WeakHandle &other);
        WeakHandle &operator=(const WeakHandle &other);

        JSObject *get() const { return target; }
        bool expired() const { return target == nullptr; }
    };

    class Heap {
        struct Entry {
            Cell *cell;
            std::size_t size;
        };

        std::vector<Cell *> cells;
        std::map<std::uintptr_t, Entry> index;
        std::vector<RootSource *> root_sources;
        std::vector<WeakContainer *> weak_containers;
        std::vector<WeakHandle *> weak_handles;

        struct HandleSlot {
            JSValue value;
            uint32_t references = 0;
            uint32_t next_free = 0;
        };

        static constexpr uint32_t NO_SLOT = 0xffffffffu;

        std::vector<HandleSlot> handles;
        uint32_t first_free_handle = NO_SLOT;

        void clear_dead_weak_handles();

        friend class WeakHandle;

        static constexpr std::size_t MIN_COLLECTION_THRESHOLD = 1024;
        std::size_t next_collection = MIN_COLLECTION_THRESHOLD;
        unsigned defer_depth = 0;

        Cell *find_cell(std::uintptr_t addr) const;
        void scan_range(std::uintptr_t start, std::uintptr_t end, std::size_t step, CellVisitor &visitor) const;
        void scan_stack(CellVisitor &visitor) const;

    public:
        bool stress;
        std::size_t collections = 0;

        Heap();
        ~Heap();

        Heap(const Heap &) = delete;
        Heap &operator=(const Heap &) = delete;

        template <typename T, typename... Args>
        T *allocate(Args &&... args) {
            static_assert(std::is_base_of_v<Cell, T>, "only cells accepted");

            if (defer_depth == 0 && (stress || cells.size() >= next_collection)) {
                collect_garbage();
            }

            T *cell = new T(std::forward<Args>(args)...);
            cells.push_back(cell);
            index.emplace(reinterpret_cast<std::uintptr_t>(cell), Entry{cell, sizeof(T)});

            return cell;
        }

        void collect_garbage();
        void add_root_source(RootSource *source) { root_sources.push_back(source); }
        void remove_root_source(RootSource *source) {
            auto it = std::find(root_sources.rbegin(), root_sources.rend(), source);
            if (it != root_sources.rend()) {
                root_sources.erase(std::next(it).base());
            }
        }

        void add_weak_container(WeakContainer *container) { weak_containers.push_back(container); }
        void remove_weak_container(WeakContainer *container);

        uint32_t acquire_handle(const JSValue &value);
        void retain_handle(uint32_t slot);
        void release_handle(uint32_t slot);
        JSValue handle_value(uint32_t slot) const;
        void set_handle_value(uint32_t slot, const JSValue &value);
        std::size_t live_handles() const;

        void defer() {++defer_depth; }
        void undefer() {--defer_depth; }

        std::size_t size() const { return cells.size(); }
    };

    class DeferGC {
        Heap &heap;

    public:
        explicit DeferGC(Heap &heap) : heap(heap) { heap.defer(); }
        ~DeferGC() { heap.undefer(); }

        DeferGC(const DeferGC &) = delete;
        DeferGC &operator=(const DeferGC &) = delete;
    };
}

#endif //PERUNEJS_HEAP_H

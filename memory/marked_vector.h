#ifndef PERUNEJS_MARKED_VECTOR_H
#define PERUNEJS_MARKED_VECTOR_H

#include <vector>

#include "heap.h"
#include "js_object.h"
#include "js_value.h"

namespace perunejs {
    class MarkedVector : public std::vector<JSValue>, public RootSource {
        Heap &heap;

    public:
        explicit MarkedVector(Heap &heap) : heap(heap) { heap.add_root_source(this); }
        ~MarkedVector() override { heap.remove_root_source(this); }

        MarkedVector(const MarkedVector &) = delete;
        MarkedVector &operator=(const MarkedVector &) = delete;

        void trace_roots(CellVisitor &visitor) override {
            for (const JSValue &value : *this) {
                if (JSObject *object = value.as_object()) visitor.visit(object);
            }
        }
    };
}

#endif //PERUNEJS_MARKED_VECTOR_H

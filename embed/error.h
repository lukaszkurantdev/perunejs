
#ifndef PERUNEJS_EMBED_ERROR_H
#define PERUNEJS_EMBED_ERROR_H

#include <stdexcept>
#include <string>
#include <utility>

#include "memory/persistent.h"

namespace perunejs::embed {
    class JSError final : public std::runtime_error {
        PersistentValue thrown;

    public:
        JSError(Heap &heap, const JSValue &value, std::string description)
            : std::runtime_error(std::move(description)), thrown(heap, value) {}

        explicit JSError(std::string description)
            : std::runtime_error(std::move(description)) {}

        JSValue value() const { return thrown.get(); }
    };
}

#endif //PERUNEJS_EMBED_ERROR_H

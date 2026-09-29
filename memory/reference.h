#ifndef PERUNEJS_REFERENCE_H
#define PERUNEJS_REFERENCE_H
#include "environment.h"

namespace perunejs {
    enum class ReferenceKind : uint8_t {
        Unresolvable,
        Variable,
        Property,
    };

    class Reference {
    public:
        std::string name;
        ReferenceKind kind;
        bool is_strict;
        Environment* env = nullptr;
        JSValue value;

        Reference() = default;

        static Reference Unresolvable(std::string name, bool is_strict = false) {
            Reference ref;
            ref.name = std::move(name);
            ref.kind = ReferenceKind::Unresolvable;
            ref.is_strict = is_strict;
            return ref;
        }

        static Reference Variable(Environment* env, std::string name, bool is_strict = false) {
            Reference ref;
            ref.name = std::move(name);
            ref.kind = ReferenceKind::Variable;
            ref.env = env;
            ref.is_strict = is_strict;
            return ref;
        }

        static Reference Property(JSValue value, std::string name, bool is_strict = false) {
            Reference ref;
            ref.name = std::move(name);
            ref.kind = ReferenceKind::Property;
            ref.value = std::move(value);
            ref.is_strict = is_strict;
            return ref;
        }

        ReferenceKind get_kind() {return kind;}
        bool is_unresolvable() {return kind == ReferenceKind::Unresolvable;}
        bool is_variable() {return kind == ReferenceKind::Variable;}
        bool is_property() {return kind == ReferenceKind::Property;}
    };
}
#endif //PERUNEJS_REFERENCE_H

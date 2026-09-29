
#ifndef PERUNEJS_ENVIRONMENT_H
#define PERUNEJS_ENVIRONMENT_H
#include <map>
#include <string>

#include "binding.h"
#include "cell.h"
#include "completion.h"
#include "js_value.h"

namespace perunejs {
    class Evaluator;
    class Environment : public Cell {
        Environment *outer;

    public:
        explicit Environment(Environment *outer = nullptr) : outer(outer) {}

        virtual bool has_binding(const std::string &name) const = 0;

        virtual Completion create_mutable_binding(Evaluator &evaluator,const std::string &name, bool is_deletable) = 0;
        virtual Completion set_binding(Evaluator &evaluator, const std::string &name, const JSValue &value, bool is_strict) = 0;
        virtual Completion get_binding_value(Evaluator &evaluator,const std::string &name, bool is_strict) = 0;
        virtual bool delete_binding(const std::string &name) = 0;

        virtual JSValue implicit_this_value() const = 0;

        Environment *find_environment(const std::string &name);

        void trace(CellVisitor &visitor) const override;
    };

    class DeclarativeEnvironment : public Environment {
        std::map<std::string, Binding> bindings;

    public:
        using Environment::Environment;
        bool has_binding(const std::string &name) const override;
        Completion create_mutable_binding(Evaluator &evaluator, const std::string &name, bool is_deletable) override;
        Completion set_binding(Evaluator &evaluator, const std::string &name, const JSValue &value, bool is_strict) override;
        Completion get_binding_value(Evaluator &evaluator, const std::string &name, bool is_strict) override;
        bool delete_binding(const std::string &name) override;
        JSValue implicit_this_value() const override { return JSValue::undefined(); }

        void create_immutable_binding(const std::string &name);
        JSValue raw_binding_value(const std::string &name) const;
        void raw_set_binding(const std::string &name, const JSValue &value);
        void initialize_immutable_binding(const std::string &name, const JSValue &value);

        void trace(CellVisitor &visitor) const override;
    };

    class ObjectEnvironment : public Environment {
    public:
        JSObject *binding_object = nullptr;
        bool provide_this = false;

        ObjectEnvironment(JSObject *binding_object, Environment *outer = nullptr,
                          bool provide_this = false)
            : Environment(outer), binding_object(binding_object), provide_this(provide_this) {}

        bool has_binding(const std::string &name) const override;

        Completion create_mutable_binding(Evaluator &evaluator, const std::string &name,bool is_deletable) override;
        Completion set_binding(Evaluator &evaluator, const std::string &name,const JSValue &value, bool is_strict) override;
        Completion get_binding_value(Evaluator &evaluator, const std::string &name, bool is_strict) override;

        bool delete_binding(const std::string &name) override;
        JSValue implicit_this_value() const override;

        void trace(CellVisitor &visitor) const override;
    };
}


#endif //PERUNEJS_ENVIRONMENT_H

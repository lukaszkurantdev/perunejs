#ifndef PERUNEJS_JS_SYMBOL_H
#define PERUNEJS_JS_SYMBOL_H

#include <deque>
#include <string>

namespace perunejs {
    struct JSSymbolData {
        std::string uid;
        std::string description;
    };

    class SymbolTable {
        std::deque<JSSymbolData> storage;
        uint64_t next_id = 0;

    public:
        static SymbolTable &instance() {
            static SymbolTable table;
            return table;
        }

        const JSSymbolData *create(const std::string &description) {
            storage.push_back(JSSymbolData{
                "@@sym:" + std::to_string(next_id++) + "(" + description + ")", description});

            return &storage.back();
        }

        const JSSymbolData *well_known(const std::string &name) {
            storage.push_back(JSSymbolData{"@@" + name, "Symbol." + name});

            return &storage.back();
        }

        const JSSymbolData *registered(const std::string &key) {
            storage.push_back(JSSymbolData{"@@for:" + key, key});

            return &storage.back();
        }
    };
}

#endif //PERUNEJS_JS_SYMBOL_H

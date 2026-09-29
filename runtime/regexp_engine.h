#ifndef PERUNEJS_REGEXP_ENGINE_H
#define PERUNEJS_REGEXP_ENGINE_H

#include <memory>
#include <string>
#include <vector>

namespace perunejs {
    struct RegExpMatch {
        std::vector<int> starts;
        std::vector<int> ends;
    };

    class RegExpEngine {
    public:
        RegExpEngine(const std::u16string &pattern, bool ignore_case, bool multiline);
        ~RegExpEngine();
        int group_count() const { return captures; }
        bool match_at(const std::u16string &input, int at, RegExpMatch &out) const;

    private:
        struct Impl;

        std::unique_ptr<Impl> impl;
        int captures = 0;
    };
}

#endif //PERUNEJS_REGEXP_ENGINE_H
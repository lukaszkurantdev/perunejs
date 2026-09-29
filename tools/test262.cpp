#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "evaluator/evaluator.h"
#include "runtime/globals.h"

using namespace perunejs;

namespace {
    struct Metadata {
        bool has_es5id = false;
        bool only_strict = false;
        bool no_strict = false;
        bool raw = false;
        bool unsupported = false;
        bool negative = false;
        std::string negative_type;
        std::vector<std::string> includes;
    };

    std::string read_file(const std::filesystem::path &path) {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();

        return buffer.str();
    }

    std::string trim(std::string text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";

        const auto last = text.find_last_not_of(" \t\r\n");
        return text.substr(first, last - first + 1);
    }

    std::vector<std::string> split_list(const std::string &text) {
        std::vector<std::string> items;
        std::string current;

        for (const char c : text) {
            if (c == ',') { items.push_back(trim(current)); current.clear(); continue; }
            if (c == '[' || c == ']') continue;

            current += c;
        }

        const std::string last = trim(current);
        if (!last.empty()) items.push_back(last);

        return items;
    }

    Metadata read_metadata(const std::string &source) {
        Metadata meta;

        const auto begin = source.find("/*---");
        const auto end = source.find("---*/");
        if (begin == std::string::npos || end == std::string::npos) return meta;

        const std::string block = source.substr(begin + 5, end - begin - 5);
        std::istringstream lines(block);
        std::string line;

        bool in_negative = false;
        bool in_includes = false;

        while (std::getline(lines, line)) {
            const std::string text = trim(line);

            if (text.rfind("es5id:", 0) == 0) meta.has_es5id = true;

            if (text.rfind("flags:", 0) == 0) {
                for (const std::string &flag : split_list(text.substr(6))) {
                    if (flag == "onlyStrict") meta.only_strict = true;
                    if (flag == "noStrict")   meta.no_strict = true;
                    if (flag == "raw")        meta.raw = true;
                    if (flag == "module" || flag == "async" || flag == "CanBlockIsFalse") {
                        meta.unsupported = true;
                    }
                }
            }

            if (text.rfind("includes:", 0) == 0) {
                const std::string rest = trim(text.substr(9));
                if (rest.empty()) { in_includes = true; continue; }

                meta.includes = split_list(rest);
            } else if (in_includes) {
                if (text.rfind("- ", 0) == 0) { meta.includes.push_back(trim(text.substr(2))); continue; }
                in_includes = false;
            }

            if (text.rfind("negative:", 0) == 0) {
                meta.negative = true;

                const std::string rest = trim(text.substr(9));
                if (!rest.empty()) meta.negative_type = rest;
                else in_negative = true;

                continue;
            }

            if (in_negative) {
                if (text.rfind("type:", 0) == 0) { meta.negative_type = trim(text.substr(5)); continue; }
                if (text.rfind("phase:", 0) == 0) continue;

                in_negative = false;
            }
        }

        return meta;
    }

    // Wiele testów z etykietą es5id zostało z czasem przepisanych na składnię
    // ES6. Nie są to porażki naszego silnika, więc trafiają do osobnego worka.
    bool uses_later_syntax(const std::string &source) {
        static const char *markers[] = {
            "=>", "`", "class ", "let ", "const ", "function*", "yield ",
            "...", "for (", "of ", "Symbol", "Proxy", "Reflect", "=> {"
        };

        const auto body = source.find("---*/");
        const std::string code = body == std::string::npos ? source : source.substr(body + 5);

        for (const char *marker : {"=>", "`", "class ", "let ", "const ",
                                   "function*", "yield ", "Symbol.", "Proxy(",
                                   "Reflect.", "...", "${"}) {
            if (code.find(marker) != std::string::npos) return true;
        }

        (void) markers;
        return false;
    }

    struct Outcome {
        bool threw = false;
        std::string text;
    };

    Outcome run_source(const std::string &source) {
        Outcome outcome;

        try {
            Heap heap;
            std::ostringstream sink;
            Evaluator evaluator(heap, sink);
            Environment *global = setup_globals(heap, evaluator);

            const Completion result = evaluator.run_script(global, source);

            if (result.type == COMPLETION_TYPE::THROW) {
                outcome.threw = true;

                const Completion text = evaluator.to_string(result.get_value_or_undefined());
                outcome.text = text.type == COMPLETION_TYPE::NORMAL
                    ? text.get_value_or_undefined().to_string() : "<toString rzucil>";
            }
        } catch (const std::exception &error) {
            outcome.threw = true;
            outcome.text = std::string("<awaria> ") + error.what();
        }

        return outcome;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "uzycie: test262 <katalog test262> [fragment sciezki]\n";
        return 2;
    }

    const std::filesystem::path root(argv[1]);
    const std::string filter = argc > 2 ? argv[2] : "";

    const std::string assert_js = read_file(root / "harness" / "assert.js");
    const std::string sta_js = read_file(root / "harness" / "sta.js");

    if (assert_js.empty() || sta_js.empty()) {
        std::cerr << "nie znaleziono harness/assert.js ani harness/sta.js w " << root << "\n";
        return 2;
    }

    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;
    std::size_t modernised = 0;
    std::vector<std::string> failures;

    std::vector<std::filesystem::path> files;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root / "test")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".js") continue;

        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto &path : files) {
        const std::string name = std::filesystem::relative(path, root).string();

        if (name.find("_FIXTURE") != std::string::npos) continue;
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;

        const std::string source = read_file(path);
        const Metadata meta = read_metadata(source);

        if (!meta.has_es5id || meta.unsupported) { ++skipped; continue; }

        if (!meta.negative && uses_later_syntax(source)) { ++modernised; continue; }

        std::string harness;
        if (!meta.raw) {
            harness = assert_js + "\n" + sta_js + "\n";

            for (const std::string &include : meta.includes) {
                harness += read_file(root / "harness" / include) + "\n";
            }
        }

        std::vector<bool> modes;
        if (meta.raw)              modes = {false};
        else if (meta.only_strict) modes = {true};
        else if (meta.no_strict)   modes = {false};
        else                       modes = {false, true};

        for (const bool strict : modes) {
            const std::string prologue = strict ? "\"use strict\";\n" : "";
            const Outcome outcome = run_source(prologue + harness + source);

            bool ok;
            if (meta.negative) {
                ok = outcome.threw
                  && outcome.text.rfind(meta.negative_type, 0) == 0;
            } else {
                ok = !outcome.threw;
            }

            if (ok) { ++passed; continue; }

            ++failed;
            if (failures.size() < 4000) {
                failures.push_back(name + (strict ? " [strict]" : "") + " -> " + outcome.text);
            }
        }
    }

    for (const std::string &failure : failures) std::cout << "FAIL " << failure << "\n";

    const std::size_t total = passed + failed;
    std::cout << "\nzaliczone " << passed << " / " << total
              << "  (" << (total == 0 ? 0 : passed * 100 / total) << "%)"
              << "\npominiete " << skipped << " (poza ES5.1), przepisane na ES6 "
              << modernised << "\n";

    return failed == 0 ? 0 : 1;
}

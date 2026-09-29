#include <doctest/doctest.h>

#include "evaluator/evaluator.h"
#include "runtime/globals.h"

#include <sstream>
#include <string>

using namespace perunejs;

// ---------------------------------------------------------------------------
// Object.setPrototypeOf (19.1.2.20) i Object.prototype.__proto__ (B.2.2.1).
//
// Poza ES5.1. Potrzebne, bo React Native ustawia prototypy TurboModules przez
// "__proto__", a helpery Babela dla dziedziczenia klas używają obu mechanizmów.
// ---------------------------------------------------------------------------

namespace {
    std::string wynik(const std::string &source) {
        Heap heap;
        std::ostringstream sink;
        Evaluator evaluator(heap, sink);
        Environment *global = setup_globals(heap, evaluator);

        const Completion result = evaluator.run_script(global, source);
        if (result.type == COMPLETION_TYPE::THROW) {
            return "THROW: " + evaluator.to_string(result.get_value_or_undefined())
                                   .get_value_or_undefined().to_string();
        }

        return evaluator.to_string(result.get_value_or_undefined()).get_value_or_undefined().to_string();
    }
}

TEST_CASE("przypisanie __proto__ zmienia prototyp zamiast tworzyc wlasciwosc") {
    CHECK(wynik("var p = {x: 1}; var o = {}; o.__proto__ = p; o.x;") == "1");
    CHECK(wynik("var p = {}; var o = {}; o.__proto__ = p; Object.getPrototypeOf(o) === p;") == "true");
    CHECK(wynik("var o = {}; o.__proto__ = {}; Object.keys(o).length;") == "0");
    CHECK(wynik("var o = {}; o.__proto__ = {}; o.hasOwnProperty('__proto__');") == "false");
}

TEST_CASE("odczyt __proto__ zwraca prototyp") {
    CHECK(wynik("({}).__proto__ === Object.prototype;") == "true");
    CHECK(wynik("[].__proto__ === Array.prototype;") == "true");
    CHECK(wynik("var o = Object.create(null); o.__proto__;") == "undefined");
}

// B.2.2.1.2 kroki 2-4: wartość spoza {Object, null} i prymitywny this są ignorowane.
TEST_CASE("__proto__ ignoruje wartosci, ktore nie moga byc prototypem") {
    CHECK(wynik("var o = {}; o.__proto__ = 5; Object.getPrototypeOf(o) === Object.prototype;") == "true");
    CHECK(wynik("var o = {}; o.__proto__ = null; Object.getPrototypeOf(o);") == "null");
}

// 9.1.2 kroki 5 i 8: obiekt nierozszerzalny i cykl w łańcuchu.
TEST_CASE("__proto__ odrzuca cykl i obiekt nierozszerzalny") {
    CHECK(wynik("var a = {}; var b = Object.create(a); a.__proto__ = b;").rfind("THROW: TypeError", 0) == 0);
    CHECK(wynik("var o = Object.preventExtensions({}); o.__proto__ = {};").rfind("THROW: TypeError", 0) == 0);
    CHECK(wynik("var o = Object.preventExtensions({}); o.__proto__ = Object.prototype; 'ok';") == "ok");
}

TEST_CASE("Object.setPrototypeOf") {
    CHECK(wynik("var p = {y: 2}; Object.setPrototypeOf({}, p).y;") == "2");
    CHECK(wynik("Object.setPrototypeOf(1, null);") == "1");
    CHECK(wynik("Object.setPrototypeOf(null, {});").rfind("THROW: TypeError", 0) == 0);
    CHECK(wynik("Object.setPrototypeOf({}, 1);").rfind("THROW: TypeError", 0) == 0);
    CHECK(wynik("var a = {}; Object.setPrototypeOf(a, Object.create(a));").rfind("THROW: TypeError", 0) == 0);
}

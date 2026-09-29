#include <doctest/doctest.h>

#include "evaluator/evaluator.h"
#include "memory/js_array_buffer.h"
#include "runtime/globals.h"

#include <sstream>
#include <string>

using namespace perunejs;

// ---------------------------------------------------------------------------
// ArrayBuffer i tablice typowane (24.1, 22.2).
//
// Poza ES5.1 — powodem jest jsi::ArrayBuffer, który należy do interfejsu JSI,
// więc bez niego nie da się zaimplementować jsi::Runtime.
// ---------------------------------------------------------------------------

namespace {
    struct Swiat {
        Heap heap;
        std::ostringstream wyjscie;
        Evaluator evaluator{heap, wyjscie};
        Environment *global = setup_globals(heap, evaluator);

        // Zwraca wynik wyrażenia jako tekst — testy opisują zachowanie,
        // a nie reprezentację wewnętrzną.
        std::string tekst(const std::string &source) {
            const Completion result = evaluator.run_script(global, "__wynik = (" + source + ");");
            if (result.type == COMPLETION_TYPE::THROW) {
                const Completion opis = evaluator.to_string(result.get_value_or_undefined());
                return "THROW: " + opis.get_value_or_undefined().to_string();
            }

            const Completion odczyt = evaluator.global_object->get(evaluator, "__wynik");
            const Completion opis = evaluator.to_string(odczyt.get_value_or_undefined());

            return opis.get_value_or_undefined().to_string();
        }
    };
}

TEST_CASE("ArrayBuffer ma dlugosc w bajtach i wymaga new") {
    Swiat s;

    CHECK(s.tekst("new ArrayBuffer(8).byteLength") == "8");
    CHECK(s.tekst("new ArrayBuffer(0).byteLength") == "0");
    CHECK(s.tekst("typeof ArrayBuffer") == "function");
    CHECK(s.tekst("ArrayBuffer(8)").rfind("THROW: TypeError", 0) == 0);
}

TEST_CASE("tablica typowana czyta i pisze przez indeksy") {
    Swiat s;

    CHECK(s.tekst("(function () { var a = new Uint8Array(4); a[0] = 7; a[3] = 255; return a[0] + ',' + a[3]; })()")
          == "7,255");
    CHECK(s.tekst("new Uint8Array(4).length") == "4");
    CHECK(s.tekst("new Int32Array(4).byteLength") == "16");
    CHECK(s.tekst("Uint8Array.BYTES_PER_ELEMENT") == "1");
    CHECK(s.tekst("Float64Array.BYTES_PER_ELEMENT") == "8");
}

// 7.1.5 — wartości spoza zakresu zawijają się modulo, a nie przycinają.
TEST_CASE("zapis do tablicy typowanej zawija wartosci") {
    Swiat s;

    CHECK(s.tekst("(function () { var a = new Uint8Array(1); a[0] = 256; return a[0]; })()") == "0");
    CHECK(s.tekst("(function () { var a = new Uint8Array(1); a[0] = 257; return a[0]; })()") == "1");
    CHECK(s.tekst("(function () { var a = new Int8Array(1); a[0] = 200; return a[0]; })()") == "-56");
    CHECK(s.tekst("(function () { var a = new Uint8Array(1); a[0] = -1; return a[0]; })()") == "255");
}

// 7.1.11 ToUint8Clamp — przycięcie, a przy dokładnej połówce zaokrąglenie
// do wartości parzystej.
TEST_CASE("Uint8ClampedArray przycina zamiast zawijac") {
    Swiat s;

    CHECK(s.tekst("(function () { var a = new Uint8ClampedArray(1); a[0] = 300; return a[0]; })()") == "255");
    CHECK(s.tekst("(function () { var a = new Uint8ClampedArray(1); a[0] = -5; return a[0]; })()") == "0");
    CHECK(s.tekst("(function () { var a = new Uint8ClampedArray(1); a[0] = 2.5; return a[0]; })()") == "2");
    CHECK(s.tekst("(function () { var a = new Uint8ClampedArray(1); a[0] = 3.5; return a[0]; })()") == "4");
}

TEST_CASE("tablice typowane dziela ten sam bufor") {
    Swiat s;

    CHECK(s.tekst(
        "(function () {"
        "  var bufor = new ArrayBuffer(4);"
        "  var bajty = new Uint8Array(bufor);"
        "  var slowa = new Uint32Array(bufor);"
        "  slowa[0] = 0;"
        "  bajty[0] = 1;"
        "  return slowa[0];"
        "})()") == "1");

    CHECK(s.tekst("new Uint8Array(new ArrayBuffer(8), 4).length") == "4");
    CHECK(s.tekst("new Uint8Array(new ArrayBuffer(8), 4).byteOffset") == "4");
    CHECK(s.tekst("new Uint32Array(new ArrayBuffer(8), 2)").rfind("THROW: RangeError", 0) == 0);
}

TEST_CASE("konstruktor przyjmuje obiekt tablicopodobny") {
    Swiat s;

    CHECK(s.tekst("(function () { var a = new Uint8Array([1, 2, 3]); return a.length + ':' + a[1]; })()")
          == "3:2");
    CHECK(s.tekst("(function () { var a = new Float64Array([1.5, 2.5]); return a[0] + ',' + a[1]; })()")
          == "1.5,2.5");
}

TEST_CASE("set, subarray i fill") {
    Swiat s;

    CHECK(s.tekst(
        "(function () {"
        "  var a = new Uint8Array(4);"
        "  a.set([9, 8], 1);"
        "  return a[0] + ',' + a[1] + ',' + a[2] + ',' + a[3];"
        "})()") == "0,9,8,0");

    CHECK(s.tekst(
        "(function () {"
        "  var a = new Uint8Array([1, 2, 3, 4]);"
        "  var w = a.subarray(1, 3);"
        "  w[0] = 99;"
        "  return w.length + ':' + a[1];"
        "})()") == "2:99");

    CHECK(s.tekst("(function () { var a = new Uint8Array(3); a.fill(5); return a[0] + ',' + a[2]; })()")
          == "5,5");
}

TEST_CASE("ArrayBuffer.isView i slice") {
    Swiat s;

    CHECK(s.tekst("ArrayBuffer.isView(new Uint8Array(1))") == "true");
    CHECK(s.tekst("ArrayBuffer.isView(new ArrayBuffer(1))") == "false");
    CHECK(s.tekst("ArrayBuffer.isView({})") == "false");
    CHECK(s.tekst("new ArrayBuffer(8).slice(2, 6).byteLength") == "4");
    CHECK(s.tekst("new ArrayBuffer(8).slice(-2).byteLength") == "2");
}

#include <doctest/doctest.h>

#include "embed/runtime.h"

#include <memory>
#include <sstream>
#include <string>

using namespace perunejs;
using namespace perunejs::embed;

// ---------------------------------------------------------------------------
// Warstwa osadzania: funkcje natywne z domknięciem, obiekty obsługiwane przez
// C++, przenoszenie błędów w obie strony i czas życia wartości.
// ---------------------------------------------------------------------------

namespace {
    // Host object z licznikiem — sprawdza, że dostęp faktycznie trafia do C++,
    // a nie do zwykłej tablicy właściwości.
    struct Licznik final : HostObject {
        int odczyty = 0;
        int zapisy = 0;
        double wartosc = 1;

        JSValue get(Runtime &, const std::string &name) override {
            ++odczyty;

            if (name == "wartosc") return JSValue::number(wartosc);
            if (name == "opis") return JSValue::string("licznik");

            return JSValue::undefined();
        }

        void set(Runtime &, const std::string &name, const JSValue &value) override {
            ++zapisy;
            if (name == "wartosc") wartosc = value.to_number();
        }

        std::vector<std::string> property_names(Runtime &) override {
            return {"wartosc", "opis"};
        }
    };
}

TEST_CASE("funkcja natywna widzi swoje domkniecie") {
    std::ostringstream sink;
    Runtime runtime(sink);

    int wywolania = 0;

    const JSValue dodaj = runtime.create_function("dodaj", 2,
        [&wywolania](Runtime &r, const JSValue &, const std::vector<JSValue> &args) {
            ++wywolania;

            const double a = args.size() > 0 ? r.to_number(args[0]) : 0;
            const double b = args.size() > 1 ? r.to_number(args[1]) : 0;

            return JSValue::number(a + b);
        });

    runtime.set_property(runtime.global(), "dodaj", dodaj);

    CHECK(runtime.to_string(runtime.evaluate("dodaj(2, 3);")) == "5");
    CHECK(runtime.to_string(runtime.evaluate("dodaj(dodaj(1, 1), 8);")) == "10");
    CHECK(wywolania == 3);

    CHECK(runtime.to_string(runtime.evaluate("dodaj.name")) == "dodaj");
    CHECK(runtime.to_string(runtime.evaluate("dodaj.length")) == "2");
    CHECK(runtime.to_string(runtime.evaluate("typeof dodaj")) == "function");
}

TEST_CASE("blad z funkcji natywnej staje sie wyjatkiem JS") {
    std::ostringstream sink;
    Runtime runtime(sink);

    const JSValue psuj = runtime.create_function("psuj", 0,
        [](Runtime &, const JSValue &, const std::vector<JSValue> &) -> JSValue {
            throw JSError("cos poszlo nie tak");
        });

    runtime.set_property(runtime.global(), "psuj", psuj);

    CHECK(runtime.to_string(runtime.evaluate(
        "(function () { try { psuj(); return 'brak bledu'; } catch (e) { return e.message; } })();"))
        == "cos poszlo nie tak");
}

TEST_CASE("wyjatek JS wychodzi do C++ jako JSError") {
    std::ostringstream sink;
    Runtime runtime(sink);

    CHECK_THROWS_AS(runtime.evaluate("throw new TypeError('z JS');"), JSError);

    try {
        runtime.evaluate("null.cokolwiek;");
        FAIL("oczekiwano wyjatku");
    } catch (const JSError &error) {
        const std::string opis = error.what();

        // what() niesie stos, jeśli rzucony obiekt go ma.
        CHECK(opis.find("TypeError") != std::string::npos);
        CHECK(error.value().type() == JSValueType::Object);
    }
}

TEST_CASE("host object obsluguje odczyt i zapis w C++") {
    std::ostringstream sink;
    Runtime runtime(sink);

    auto licznik = std::make_shared<Licznik>();
    runtime.set_property(runtime.global(), "licznik", runtime.create_object(licznik));

    CHECK(runtime.to_string(runtime.evaluate("licznik.wartosc")) == "1");
    CHECK(runtime.to_string(runtime.evaluate("licznik.opis")) == "licznik");

    runtime.evaluate("licznik.wartosc = 41;");
    CHECK(licznik->wartosc == 41);

    runtime.evaluate("licznik.wartosc = licznik.wartosc + 1;");
    CHECK(licznik->wartosc == 42);
    CHECK(runtime.to_string(runtime.evaluate("licznik.wartosc")) == "42");

    CHECK(licznik->zapisy == 2);
    CHECK(licznik->odczyty > 0);

    // Nieznana właściwość schodzi do C++ i wraca jako undefined.
    CHECK(runtime.to_string(runtime.evaluate("typeof licznik.nieznana")) == "undefined");
}

TEST_CASE("host object podaje swoje wlasciwosci i daje sie odzyskac") {
    std::ostringstream sink;
    Runtime runtime(sink);

    auto licznik = std::make_shared<Licznik>();
    const JSValue obiekt = runtime.create_object(licznik);

    CHECK(runtime.is_host_object(obiekt));
    CHECK(runtime.host_object(obiekt) == licznik);

    runtime.set_property(runtime.global(), "licznik", obiekt);

    const std::vector<std::string> klucze = runtime.property_names(obiekt);
    CHECK(klucze.size() == 2);
    CHECK(runtime.to_string(runtime.evaluate("Object.keys(licznik).join(',')")) == "wartosc,opis");
}

TEST_CASE("uchwyt trwaly utrzymuje wartosc miedzy wywolaniami") {
    std::ostringstream sink;
    Runtime runtime(sink);

    // Typowy scenariusz osadzania: moduł natywny trzyma funkcję JS, żeby
    // zawołać ją później. Bez persist() GC by ją zebrał.
    auto zapamietana = std::make_unique<PersistentValue>(
        runtime.persist(runtime.evaluate("(function (x) { return x * 3; });")));

    runtime.evaluate("for (var i = 0; i < 2000; i++) { var junk = {a: {b: {}}}; }");
    runtime.heap().collect_garbage();

    const JSValue wynik = runtime.call(zapamietana->get(), JSValue::undefined(),
                                       {JSValue::number(14)});

    CHECK(runtime.to_number(wynik) == 42);
}

TEST_CASE("bufor tablicowy jest wspoldzielony z kodem natywnym") {
    std::ostringstream sink;
    Runtime runtime(sink);

    const JSValue bufor = runtime.create_array_buffer(4);
    REQUIRE(runtime.array_buffer_size(bufor) == 4);

    uint8_t *bajty = runtime.array_buffer_data(bufor);
    REQUIRE(bajty != nullptr);

    bajty[0] = 7;
    bajty[3] = 9;

    runtime.set_property(runtime.global(), "bufor", bufor);

    CHECK(runtime.to_string(runtime.evaluate("new Uint8Array(bufor)[0]")) == "7");
    CHECK(runtime.to_string(runtime.evaluate("new Uint8Array(bufor)[3]")) == "9");

    // Zapis z JS widać po stronie C++.
    runtime.evaluate("new Uint8Array(bufor)[1] = 5;");
    CHECK(bajty[1] == 5);
}

TEST_CASE("mikrozadania drenuje host") {
    std::ostringstream sink;
    Runtime runtime(sink);

    runtime.evaluate(
        "globalThis.slad = [];"
        "Promise.resolve(1).then(function (v) { slad.push('then:' + v); });"
        "slad.push('synchronicznie');");

    // run_script drenuje kolejkę na końcu skryptu, więc reakcja już przeszła.
    CHECK(runtime.to_string(runtime.evaluate("slad.join(',')")) == "synchronicznie,then:1");

    runtime.evaluate("queueMicrotask(function () { slad.push('recznie'); });");
    runtime.drain_microtasks();

    CHECK(runtime.to_string(runtime.evaluate("slad.join(',')"))
          == "synchronicznie,then:1,recznie");
}

TEST_CASE("tworzenie wartosci i pytania o nie") {
    std::ostringstream sink;
    Runtime runtime(sink);

    const JSValue obiekt = runtime.create_object();
    runtime.set_property(obiekt, "a", JSValue::number(1));

    CHECK(runtime.has_property(obiekt, "a"));
    CHECK_FALSE(runtime.has_property(obiekt, "b"));
    CHECK(runtime.to_number(runtime.get_property(obiekt, "a")) == 1);

    CHECK(runtime.is_array(runtime.create_array(3)));
    CHECK_FALSE(runtime.is_array(obiekt));

    CHECK(runtime.is_callable(runtime.evaluate("(function () {})")));
    CHECK_FALSE(runtime.is_callable(obiekt));

    CHECK(runtime.strict_equals(JSValue::number(1), JSValue::number(1)));
    CHECK_FALSE(runtime.strict_equals(obiekt, runtime.create_object()));

    runtime.set_property(runtime.global(), "tablica", runtime.create_array(0));
    CHECK(runtime.to_string(runtime.evaluate("tablica instanceof Array")) == "true");
}

// ---------------------------------------------------------------------------
// Wzorzec TurboModule z React Native (TurboModuleBinding::getModule):
//
//   auto jsRepresentation = jsi::Object(runtime);
//   jsRepresentation.setProperty(runtime, "__proto__", hostObject);
//
// Metody nie istnieją na obiekcie — lookup przez łańcuch prototypów dochodzi
// do host objectu i dopiero ten je tworzy. Wymaga to dwóch rzeczy naraz:
// akcesora __proto__ (B.2.2.1) i host objectu odpowiadającego na każdą nazwę.
// ---------------------------------------------------------------------------

namespace {
    struct ModulNatywny final : HostObject {
        int pytania = 0;

        // Obiekt reprezentacji, na którym TurboModule zapisuje utworzone metody.
        std::unique_ptr<PersistentValue> reprezentacja;

        JSValue get(Runtime &runtime, const std::string &name) override {
            ++pytania;

            if (name == "getConstants") {
                const JSValue metoda = runtime.create_function("getConstants", 0,
                    [](Runtime &r, const JSValue &, const std::vector<JSValue> &) {
                        const JSValue stale = r.create_object();
                        r.set_property(stale, "BLOB_URI_SCHEME", JSValue::string("content"));
                        return stale;
                    });

                // Tak robi TurboModule::get: zapis do pamięci podręcznej na
                // reprezentacji. Ten zapis przegląda łańcuch prototypów i przed
                // poprawką wracał tu w nieskończoność.
                if (reprezentacja != nullptr) {
                    runtime.set_property(reprezentacja->get(), name, metoda);
                }

                return metoda;
            }

            return JSValue::undefined();
        }

        // Celowo pusta lista: host object ma odpowiadać także na nazwy,
        // których nie deklaruje.
        std::vector<std::string> property_names(Runtime &) override { return {}; }
    };
}

TEST_CASE("host object jako prototyp dziala jak TurboModule w React Native") {
    std::ostringstream sink;
    Runtime runtime(sink);

    auto modul = std::make_shared<ModulNatywny>();

    const JSValue reprezentacja = runtime.create_object();
    modul->reprezentacja = std::make_unique<PersistentValue>(runtime.persist(reprezentacja));

    runtime.set_property(reprezentacja, "__proto__", runtime.create_object(modul));
    runtime.set_property(runtime.global(), "BlobModule", reprezentacja);

    // Ustawienie __proto__ zmienia prototyp, a nie tworzy właściwości.
    CHECK(runtime.to_string(runtime.evaluate("Object.keys(BlobModule).length")) == "0");

    CHECK(runtime.to_string(runtime.evaluate("typeof BlobModule.getConstants")) == "function");
    CHECK(runtime.to_string(runtime.evaluate("BlobModule.getConstants().BLOB_URI_SCHEME")) == "content");

    // Po pierwszym odczycie metoda jest już własną właściwością reprezentacji,
    // więc kolejne odczyty nie schodzą do C++.
    CHECK(runtime.to_string(runtime.evaluate("BlobModule.hasOwnProperty('getConstants')")) == "true");
    const int po_zapisie = modul->pytania;
    runtime.evaluate("BlobModule.getConstants; BlobModule.getConstants;");
    CHECK(modul->pytania == po_zapisie);

    CHECK(runtime.to_string(runtime.evaluate("typeof BlobModule.nieistniejaca")) == "undefined");

    // Nazwy nieznane hostowi idą dalej w górę łańcucha, do Object.prototype.
    CHECK(runtime.to_string(runtime.evaluate("typeof BlobModule.hasOwnProperty")) == "function");

    modul->reprezentacja.reset();
}

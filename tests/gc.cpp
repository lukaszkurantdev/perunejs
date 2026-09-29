#include <doctest/doctest.h>

#include "lexer/lexer.h"
#include "parser.h"
#include "evaluator/evaluator.h"
#include "memory/heap.h"
#include "memory/js_object.h"
#include "memory/js_map.h"
#include "memory/persistent.h"
#include "utils/native_stack.h"
#include "runtime/globals.h"

#include <memory>
#include <string>
#include <vector>

using namespace perunejs;

// ---------------------------------------------------------------------------
// Helpery
//
// Uwaga o skanowaniu konserwatywnym: przypadkowa wartość na stosie C++ może
// zatrzymać pojedynczy martwy obiekt. Testy „zostało zebrane” sprawdzają więc
// rząd wielkości, a nie dokładną liczbę komórek.
//
// Część testów przechodzi w zwykłym buildzie nawet przy brakującym korzeniu,
// bo wskaźnik przypadkiem zostaje na stosie. Rozstrzygające są dopiero
// PERUN_GC_STRESS=1 i build z AddressSanitizerem.
// ---------------------------------------------------------------------------

namespace {
    // Sterta, ewaluator i globalne środowisko, w którym można wykonać kilka
    // programów po kolei i wymusić GC pomiędzy nimi.
    struct World {
        Heap heap;
        Evaluator evaluator{heap};
        Environment *global;

        // Rozmiar sterty tuż po instalacji obiektów wbudowanych. Testy porównują
        // się do tej wartości, a nie do stałej — inaczej każde nowe wbudowane
        // (Promise, Symbol, kolekcje, tablice typowane) psuje próg.
        std::size_t po_instalacji = 0;

        World() : global(setup_globals(heap, evaluator)) { po_instalacji = heap.size(); }

        // run_script sam dba o to, żeby drzewo żyło tak długo, jak długo
        // odwołują się do niego funkcje — nie trzeba go tu trzymać osobno.
        Completion run(const std::string &source) {
            return evaluator.run_script(global, source);
        }

        JSValue value_of(const std::string &source) {
            Completion completion = run(source);
            REQUIRE(completion.type == COMPLETION_TYPE::NORMAL);
            return completion.get_value_or_undefined();
        }

        // Środowisko globalne jest teraz korzeniem GC przez Evaluator::trace_roots.
        void collect() { heap.collect_garbage(); }
    };

    // Źródło korzeni do testów samej sterty, bez ewaluatora.
    struct TestRoots : RootSource {
        std::vector<Cell *> cells;

        void trace_roots(CellVisitor &visitor) override {
            for (Cell *cell : cells) visitor.visit(cell);
        }
    };

    // noinline: wskaźniki do tworzonych tu obiektów nie zostają w ramce testu.
    [[gnu::noinline]] void allocate_garbage(Heap &heap, int count) {
        for (int i = 0; i < count; ++i) heap.allocate<JSObject>();
    }

    // parent → child → child.prototype; zwraca tylko rodzica.
    [[gnu::noinline]] JSObject *allocate_family(Heap &heap) {
        JSObject *parent = heap.allocate<JSObject>();
        JSObject *child  = heap.allocate<JSObject>();
        child->prototype = heap.allocate<JSObject>();
        parent->define_own_property("child", PropertyDescriptor::data(JSValue::object(child), true, true, true));
        return parent;
    }

    [[gnu::noinline]] void bind_fresh_object(Evaluator &evaluator, Heap &heap, Environment &env,
                                             const std::string &name) {
        env.create_mutable_binding(evaluator, name, false);
        env.set_binding(evaluator, name, JSValue::object(heap.allocate<JSObject>()), false);
    }

    // Wykonuje `setup`, produkuje śmieci, wymusza GC i zwraca wynik `check`.
    // Zwraca tylko prymitywy — świat ginie razem z tą funkcją.
    JSValue after_gc(const std::string &setup, const std::string &check) {
        World world;
        world.run(setup);
        world.run("for (var i = 0; i < 500; i++) { var junk = {a: {}}; }");
        world.collect();
        return world.value_of(check);
    }
}

// ---------------------------------------------------------------------------
// Sterta: kiedy zbiera i co zwalnia
// ---------------------------------------------------------------------------

TEST_CASE("GC nie rusza sterty poniżej progu, a po jego przekroczeniu zbiera") {
    Heap heap;
    heap.stress = false;   // test dotyczy progu, tryb stress by go zamazał

    allocate_garbage(heap, 1024);
    CHECK(heap.collections == 0);
    CHECK(heap.size() == 1024);

    allocate_garbage(heap, 1);
    CHECK(heap.collections == 1);
    CHECK(heap.size() < 100);
}

TEST_CASE("obiekt osiągalny z korzenia przeżywa GC razem ze wszystkim, co z niego wychodzi") {
    Heap heap;
    heap.stress = false;

    TestRoots roots;
    heap.add_root_source(&roots);
    roots.cells.push_back(allocate_family(heap));   // 3 komórki: rodzic, dziecko, prototyp dziecka

    allocate_garbage(heap, 500);
    heap.collect_garbage();

    CHECK(heap.size() >= 3);
    CHECK(heap.size() < 100);

    const auto *parent = static_cast<JSObject *>(roots.cells[0]);
    const PropertyDescriptor *child = parent->get_own_property("child");
    REQUIRE(child != nullptr);
    CHECK(child->get_value().as_object()->prototype != nullptr);

    heap.remove_root_source(&roots);
}

TEST_CASE("DeferGC wstrzymuje zbieranie nawet w trybie stress") {
    Heap heap;
    heap.stress = true;

    {
        DeferGC defer(heap);
        allocate_garbage(heap, 10);
        CHECK(heap.collections == 0);
    }

    allocate_garbage(heap, 1);
    CHECK(heap.collections == 1);
}

TEST_CASE("DeferGC się zagnieżdża") {
    Heap heap;
    heap.stress = true;

    {
        DeferGC outer(heap);
        {
            DeferGC inner(heap);
            allocate_garbage(heap, 1);
        }
        allocate_garbage(heap, 1);   // wciąż wewnątrz zewnętrznego DeferGC
        CHECK(heap.collections == 0);
    }
}

TEST_CASE("komórka spoza sterty ma czyszczone marked, więc kolejne GC nadal śledzi jej krawędzie") {
    // Środowisko programu żyje zwykle na stosie C++. Sweep go nie przegląda —
    // gdyby marked nie było czyszczone z listy oznaczonych, drugie GC uznałoby
    // środowisko za przetworzone i zwolniło to, co jest osiągalne tylko przez nie.
    Heap heap;
    heap.stress = false;
    Evaluator evaluator(heap);

    DeclarativeEnvironment env;
    TestRoots roots;
    roots.cells.push_back(&env);
    heap.add_root_source(&roots);

    bind_fresh_object(evaluator, heap, env, "x");

    heap.collect_garbage();
    CHECK_FALSE(env.marked);

    heap.collect_garbage();
    CHECK(heap.size() == 1);

    heap.remove_root_source(&roots);
}

// ---------------------------------------------------------------------------
// Program JS: śmieci znikają
// ---------------------------------------------------------------------------

TEST_CASE("śmieci tworzone w pętli są zbierane w trakcie wykonania") {
    World world;
    world.heap.stress = false;   // 20 tys. pełnych GC w trybie stress trwałoby za długo

    CHECK(world.value_of("for (var i = 0; i < 20000; i++) { var o = {a: {b: {}}}; } 1;").to_number() == 1);
    CHECK(world.heap.collections > 0);

    world.collect();
    CHECK(world.heap.size() < world.po_instalacji + 300);
}

TEST_CASE("cykle bez zewnętrznych referencji są zbierane") {
    World world;
    world.heap.stress = false;

    world.run("for (var i = 0; i < 20000; i++) { var a = {}, b = {}; a.b = b; b.a = a; }");
    world.collect();

    CHECK(world.heap.size() < world.po_instalacji + 300);
}

// ---------------------------------------------------------------------------
// Program JS: żywe obiekty zostają
// ---------------------------------------------------------------------------

TEST_CASE("element tablicy przeżywa GC") {
    CHECK(after_gc("var a = [{v: 1}, {v: 2}];", "a[1].v;").to_number() == 2);
}

TEST_CASE("obiekt osiągalny tylko przez łańcuch prototypów przeżywa GC") {
    CHECK(after_gc("function F() {} F.prototype.shared = {v: 3}; var o = new F(); F = null;",
                   "o.shared.v;").to_number() == 3);
}

TEST_CASE("obiekt osiągalny tylko przez domknięcie przeżywa GC") {
    CHECK(after_gc("var f = (function () { var captured = {v: 5}; return function () { return captured.v; }; })();",
                   "f();").to_number() == 5);
}

TEST_CASE("obiekt osiągalny tylko przez getter przeżywa GC") {
    CHECK(after_gc("var o = (function () { var hidden = {v: 4}; return {get x() { return hidden.v; }}; })();",
                   "o.x;").to_number() == 4);
}

TEST_CASE("złapany wyjątek przeżywa GC") {
    CHECK(after_gc("var caught; try { throw new TypeError(\"t\"); } catch (e) { caught = e; }",
                   "caught instanceof TypeError && caught.message === \"t\";").to_boolean());
}

TEST_CASE("intrinsics przeżywają GC, choć nie wiszą pod żadną zmienną") {
    // array_prototype i regexp_prototype są osiągalne tylko z pól ewaluatora.
    World world;
    world.collect();

    CHECK(world.value_of("typeof [].hasOwnProperty;").to_string() == "function");
    CHECK(world.value_of("\"\" + /a/g;").to_string() == "/a/g");
}

TEST_CASE("tysiące domknięć przeżywają GC uruchamiane w trakcie ich tworzenia") {
    World world;
    world.heap.stress = false;

    CHECK(world.value_of(
        "var fs = [];"
        "for (var i = 0; i < 3000; i++) { fs[i] = (function (j) { return function () { return j; }; })(i); }"
        "fs[2999]() + fs[0]();").to_number() == 2999);
    CHECK(world.heap.collections > 0);

    world.collect();
    CHECK(world.heap.size() > 3000);
}

TEST_CASE("długi łańcuch obiektów nie przepełnia stosu przy znakowaniu") {
    // Rekurencyjne znakowanie zeszłoby 200 tys. poziomów w głąb stosu C++.
    World world;
    world.heap.stress = false;

    CHECK(world.value_of(
        "var head = null;"
        "for (var i = 0; i < 200000; i++) head = {next: head};"
        "var n = 0; while (head) { n++; head = head.next; } n;").to_number() == 200000);
}

// ---------------------------------------------------------------------------
// GC przy każdej alokacji — korzenie, które widać tylko pod presją
// ---------------------------------------------------------------------------

TEST_CASE("argumenty wywołania przeżywają GC podczas obliczania kolejnych argumentów") {
    // Pierwszy argument żyje wtedy już tylko w MarkedVector args.
    World world;
    world.heap.stress = true;

    CHECK(world.value_of(
        "function f(a, b) { return a.v + b.length; }"
        "f({v: 5}, (function () { var t = []; for (var i = 0; i < 20; i++) t[i] = {}; return t; })());")
          .to_number() == 25);

    CHECK(world.value_of(
        "function G(a, b) { this.s = a.v + b.v; }"
        "new G({v: 1}, {v: 2}).s;").to_number() == 3);
}

TEST_CASE("this przeżywa GC w trakcie wykonywania metody") {
    World world;
    world.heap.stress = true;

    CHECK(world.value_of(
        "({v: 3, m: function () { for (var i = 0; i < 20; i++) { var junk = {}; } return this.v; }}).m();")
          .to_number() == 3);
}

TEST_CASE("wbudowane obiekty przeżywają GC przy każdej alokacji już w setup_globals") {
    Heap heap;
    heap.stress = true;
    Evaluator evaluator(heap);
    Environment *global = setup_globals(heap, evaluator);

    Lexer lexer("typeof print === \"function\" && Error.prototype.constructor === Error"
                " && new RangeError(\"r\") instanceof Error;");
    Parser parser(lexer.scan_tokens());
    const auto program = parser.parse();

    Completion completion = evaluator.eval_program(global, *program);
    REQUIRE(completion.type == COMPLETION_TYPE::NORMAL);
    CHECK(completion.get_value_or_undefined().to_boolean());
}

TEST_CASE("złożony program działa z GC przy każdej alokacji") {
    World world;
    world.heap.stress = true;

    CHECK(world.value_of(
        "function Counter(start) { this.n = start; }"
        "Counter.prototype.inc = function () { this.n++; return this; };"
        "var log = [];"
        "var c = new Counter(1);"
        "for (var i = 0; i < 5; i++) { c.inc(); log[log.length] = {step: i, n: c.n}; }"
        "var sum = 0;"
        "for (var k in log) { sum += log[k].n; }"
        "try { null.x; } catch (e) { sum += e instanceof TypeError ? 100 : 0; }"
        "sum;").to_number() == 120);
}

TEST_CASE("arguments zatrzymane w domknięciu przeżywa GC") {
    // Obiekt arguments trzyma wskaźnik na środowisko wywołania — jego trace()
    // musi je odwiedzić, inaczej mapowanie parametrów pokazywałoby na zwolnioną pamięć.
    CHECK(after_gc("var keep = (function (a) { var args = arguments;"
                   "                          return function () { return args[0] + a; }; })(7);",
                   "keep();").to_number() == 14);
}

TEST_CASE("funkcja utworzona przez eval przeżywa GC") {
    // AST kodu z eval jest własnością ewaluatora (scripts), a funkcja z niego
    // powstała wskazuje na jego węzły — musi przeżyć razem z nią.
    CHECK(after_gc("eval(\"function fromEval(a) { return a + 1; }\");", "fromEval(41);").to_number() == 42);
}

TEST_CASE("obiekt z with zatrzymany w domknięciu przeżywa GC") {
    // Domknięcie trzyma ObjectEnvironment, a ten swój obiekt — trace() musi je odwiedzić.
    CHECK(after_gc("var keep; with ({v: 5}) { keep = function () { return v; }; }", "keep();")
              .to_number() == 5);
}

TEST_CASE("wspólna funkcja [[ThrowTypeError]] przeżywa GC") {
    // Nie wskazuje na nią nic z JavaScriptu — trzyma ją wyłącznie korzeń
    // w Evaluator::trace_roots.
    CHECK(after_gc("\"use strict\"; function f() {}",
                   "try { f.caller; } catch (e) { e instanceof TypeError; }").to_boolean());
}

TEST_CASE("opakowania prymitywów są zbierane") {
    // Każdy odczyt właściwości łańcucha tworzy nowy obiekt JSString.
    World world;
    world.heap.stress = false;

    CHECK(world.value_of("var n = 0; for (var i = 0; i < 20000; i++) { n += \"abc\".length; } n;")
              .to_number() == 60000);
    CHECK(world.heap.collections > 0);

    world.collect();
    CHECK(world.heap.size() < world.po_instalacji + 300);
}

TEST_CASE("funkcja związana trzyma cel i argumenty przy życiu") {
    // target, bound_this i bound_args leżą poza mapą właściwości — widzi je
    // wyłącznie JSBoundFunction::trace.
    CHECK(after_gc("var keep = (function () {"
                   "  function f(a, b) { return this.v + a + b; }"
                   "  return f.bind({v: 100}, 20);"
                   "})();",
                   "keep(3);").to_number() == 123);
}

// ---------------------------------------------------------------------------
// Własność drzewa składniowego
// ---------------------------------------------------------------------------

TEST_CASE("drzewo żyje tak długo, jak funkcje, które z niego pochodzą") {
    Heap heap;
    Evaluator evaluator{heap};
    Environment *global = setup_globals(heap, evaluator);

    std::weak_ptr<Program> tree;

    {
        std::shared_ptr<Program> program;
        REQUIRE_FALSE(evaluator.compile("var kept = function () { return 1; };", false, program)
                          .is_abrupt());
        tree = program;

        const Evaluator::ScriptScope owner(evaluator, program);
        REQUIRE_FALSE(evaluator.eval_program(global, *program).is_abrupt());
    }

    // Lokalny wskaźnik zniknął, ale funkcja siedzi w zmiennej globalnej
    // i trzyma współwłasność drzewa.
    heap.collect_garbage();
    CHECK_FALSE(tree.expired());
    CHECK(evaluator.run_script(global, "kept();").get_value_or_undefined().to_number() == 1);

    // Gdy funkcja przestaje być osiągalna, drzewo znika razem z nią.
    REQUIRE_FALSE(evaluator.run_script(global, "kept = undefined;").is_abrupt());
    heap.collect_garbage();
    CHECK(tree.expired());
}

TEST_CASE("drzewo bez żadnej funkcji znika od razu po wykonaniu") {
    Heap heap;
    Evaluator evaluator{heap};
    Environment *global = setup_globals(heap, evaluator);

    std::weak_ptr<Program> tree;

    {
        std::shared_ptr<Program> program;
        REQUIRE_FALSE(evaluator.compile("var x = 1 + 2;", false, program).is_abrupt());
        tree = program;

        const Evaluator::ScriptScope owner(evaluator, program);
        REQUIRE_FALSE(evaluator.eval_program(global, *program).is_abrupt());
    }

    CHECK(tree.expired());
}

TEST_CASE("eval i Function nie odkładają drzew bez końca") {
    World world;

    // Każde wywołanie zostawiało dawniej drzewo w wektorze, którego nic
    // nigdy nie czyściło. Teraz drzewo ginie razem z funkcją.
    world.run("for (var i = 0; i < 200; i++) { eval(\"(function () { return i; })\"); }");
    world.run("for (var i = 0; i < 200; i++) { new Function(\"return 1;\"); }");
    world.collect();

    CHECK(world.value_of("typeof eval(\"1 + 1\");").to_string() == "number");
    CHECK(world.value_of("new Function(\"return 7;\")();").to_number() == 7);
}

TEST_CASE("błąd składni to wartość SyntaxError, nie wyjątek C++") {
    World world;

    const Completion broken = world.evaluator.run_script(world.global, "var = ;");

    REQUIRE(broken.type == COMPLETION_TYPE::THROW);
    CHECK(world.value_of("try { eval(\"var = ;\"); } catch (e) { e instanceof SyntaxError; }")
              .to_boolean());
    CHECK(world.value_of("try { eval(\"'use strict'; 010;\"); } catch (e) { e instanceof SyntaxError; }")
              .to_boolean());

    // Świat po złapanym błędzie nadal działa.
    CHECK(world.value_of("1 + 1;").to_number() == 2);
}

// ---------------------------------------------------------------------------
// Uchwyty trwałe i słabe referencje.
//
// Konserwatywny skan stosu widzi tylko wartości leżące na stosie natywnym.
// Kod osadzający trzyma je w polach obiektów na stercie — stąd oba mechanizmy.
// ---------------------------------------------------------------------------

TEST_CASE("uchwyt trwaly chroni obiekt niewidoczny ze stosu") {
    Heap heap;

    // Celowo na stercie: gdyby to był lokalny JSObject*, skan stosu znalazłby
    // go sam i test nie sprawdzałby niczego.
    auto trzymacz = std::make_unique<PersistentValue>();

    {
        auto *obiekt = heap.allocate<JSObject>();
        obiekt->define_own_property("znacznik",
            PropertyDescriptor::data(JSValue::number(42), true, true, true));

        *trzymacz = PersistentValue(heap, JSValue::object(obiekt));
    }

    heap.collect_garbage();
    heap.collect_garbage();

    const JSValue przetrwal = trzymacz->get();
    REQUIRE(przetrwal.type() == JSValueType::Object);

    const PropertyDescriptor *znacznik = przetrwal.as_object()->get_own_property("znacznik");
    REQUIRE(znacznik != nullptr);
    CHECK(znacznik->get_value().to_number() == 42);

    CHECK(heap.live_handles() == 1);

    trzymacz.reset();
    CHECK(heap.live_handles() == 0);
}

TEST_CASE("uchwyt trwaly zlicza referencje") {
    Heap heap;

    auto pierwszy = std::make_unique<PersistentValue>(heap, JSValue::number(7));
    auto drugi = std::make_unique<PersistentValue>(*pierwszy);

    CHECK(heap.live_handles() == 1);

    pierwszy.reset();
    CHECK(heap.live_handles() == 1);
    CHECK(drugi->get().to_number() == 7);

    drugi.reset();
    CHECK(heap.live_handles() == 0);
}

// Zamazuje ramki stosu po wcześniejszych wywołaniach. Bez tego konserwatywny
// skan wciąż widzi nieaktualne wskaźniki i utrzymuje obiekty przy życiu.
// Rekurencja z duzą liczbą żywych lokalnych wymusza wypchnięcie rejestrów
// zachowywanych przez wywołania na stos i nadpisanie starych ramek. Samo
// zapisanie tablicy nie wystarcza: setjmp w GC czyta też rejestry.
[[gnu::noinline]] static std::uintptr_t mieszaj(std::uintptr_t ziarno, const int glebokosc) {
    volatile std::uintptr_t bufor[256];
    for (int i = 0; i < 256; ++i) bufor[i] = ziarno + static_cast<std::uintptr_t>(i);

    if (glebokosc <= 0) return bufor[255];

    return mieszaj(ziarno ^ bufor[glebokosc % 256] ^ 0x9E3779B9u, glebokosc - 1);
}

[[gnu::noinline]] static void zamaz_stos() {
    volatile std::uintptr_t wynik = mieszaj(0xD15EA5Eu, 64);
    (void) wynik;
}

// Helpery noinline: wskaźnik do świeżego obiektu nie może zostać w ramce
// testu, bo konserwatywny skan stosu znalazłby go i utrzymał przy życiu.
// Wynik przez parametr wyjsciowy, nie przez wartosc: gdyby WeakHandle wracal
// przez return, jego tymczasowy egzemplarz (a w nim wskaznik do celu) wyladowalby
// w ramce SAMEGO testu, ktorej zamaz_stos nie jest w stanie nadpisac, bo dziala
// glebiej na stosie.
[[gnu::noinline]] static void ustaw_slaby_do_swiezego(Heap &heap, WeakHandle &wynik) {
    wynik = WeakHandle(heap, heap.allocate<JSObject>());
}

[[gnu::noinline]] static void zwiaz_mocno_i_slabo(Heap &heap, PersistentValue &mocny, WeakHandle &slaby) {
    auto *obiekt = heap.allocate<JSObject>();

    mocny = PersistentValue(heap, JSValue::object(obiekt));
    slaby = WeakHandle(heap, obiekt);
}

TEST_CASE("slaba referencja gasnie, gdy nie ma innej drogi do obiektu") {
    Heap heap;

    auto slaby = std::make_unique<WeakHandle>();
    ustaw_slaby_do_swiezego(heap, *slaby);
    CHECK_FALSE(slaby->expired());

    zamaz_stos();
    allocate_garbage(heap, 64);
    zamaz_stos();
    heap.collect_garbage();

    CHECK(slaby->expired());
}

TEST_CASE("slaba referencja przezywa, dopoki obiekt jest osiagalny") {
    Heap heap;

    auto slaby = std::make_unique<WeakHandle>();
    auto mocny = std::make_unique<PersistentValue>();

    zwiaz_mocno_i_slabo(heap, *mocny, *slaby);

    allocate_garbage(heap, 64);
    heap.collect_garbage();
    CHECK_FALSE(slaby->expired());

    mocny->reset();

    zamaz_stos();
    allocate_garbage(heap, 64);
    heap.collect_garbage();
    CHECK(slaby->expired());
}

// Ten sam scenariusz na poziomie kolekcji: klucz tworzony w helperze
// noinline i nietrzymany nigdzie indziej.
[[gnu::noinline]] static void wstaw_ulotny_klucz(Heap &heap, JSCollection *kolekcja) {
    auto *klucz = heap.allocate<JSObject>();
    kolekcja->set(JSValue::object(klucz), JSValue::number(1));
}

// Caly scenariusz wykonujemy glebiej na stosie niz ramka testu, zeby wskaznik
// do klucza nie mial gdzie przetrwac. Ramki testu zamaz_stos nie dosiega, bo
// dziala ponizej niej.
[[gnu::noinline]] static std::size_t rozmiar_po_gc(Heap &heap, JSCollection *kolekcja) {
    wstaw_ulotny_klucz(heap, kolekcja);

    zamaz_stos();
    allocate_garbage(heap, 256);
    zamaz_stos();
    heap.collect_garbage();

    return kolekcja->size();
}

TEST_CASE("WeakMap nie trzyma klucza przy zyciu") {
    Heap heap;

    auto *mapa = heap.allocate<JSCollection>(heap, std::string("WeakMap"));
    const PersistentValue trzymaj(heap, JSValue::object(mapa));

    CHECK(rozmiar_po_gc(heap, mapa) == 0);
}

TEST_CASE("Map trzyma klucz przy zyciu") {
    Heap heap;

    auto *mapa = heap.allocate<JSCollection>(heap, std::string("Map"));
    const PersistentValue trzymaj(heap, JSValue::object(mapa));

    CHECK(rozmiar_po_gc(heap, mapa) == 1);
}

// ---------------------------------------------------------------------------
// Segment stosu zgłoszony przez kod osadzający. Adapter JSI przełącza silnik
// na własny stos; kontrola przepełnienia, parser i GC muszą widzieć jego
// granice, a nie granice stosu wątku.
// ---------------------------------------------------------------------------

TEST_CASE("NativeStackSegment nadpisuje granice stosu i przywraca poprzednie") {
    const std::uintptr_t watku = native_stack_base();

    {
        const NativeStackSegment zewnetrzny(0x40000000u, 0x100000u);
        CHECK(native_stack_base() == 0x40000000u);
        CHECK(native_stack_limit(0) == 0x40000000u - 0x100000u);
        CHECK(native_stack_limit(0x1000) == 0x40000000u - 0x100000u + 0x1000);

        {
            const NativeStackSegment wewnetrzny(0x80000000u, 0x2000u);
            CHECK(native_stack_base() == 0x80000000u);
        }

        CHECK(native_stack_base() == 0x40000000u);
    }

    CHECK(native_stack_base() == watku);
}

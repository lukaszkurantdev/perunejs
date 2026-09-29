#include <doctest/doctest.h>

#include "lexer/lexer.h"
#include "parser.h"
#include "parser/syntax_error.h"
#include "evaluator/evaluator.h"
#include "runtime/globals.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace perunejs;

// ---------------------------------------------------------------------------
// Harness na realny bundle React Native (main.jsbundle).
//
// Cel: sprawdzić, czy lekser i parser domykają się na produkcyjnym,
// zminifikowanym kodzie sprowadzonym Babelem do ES5.1 — zanim w ogóle
// zaczniemy mówić o wykonaniu. Jeśli pliku nie ma, testy się nie wykonują
// (MESSAGE + return), żeby nie blokować zwykłego przebiegu suite'y.
//
// Ścieżkę można wskazać zmienną środowiskową PERUNEJS_BUNDLE.
// ---------------------------------------------------------------------------

namespace {
    std::filesystem::path find_bundle() {
        if (const char *from_env = std::getenv("PERUNEJS_BUNDLE")) {
            return std::filesystem::path(from_env);
        }

        const std::filesystem::path candidates[] = {
            "main.jsbundle",
            "../main.jsbundle",
            "../../main.jsbundle",
        };

        for (const auto &candidate : candidates) {
            if (std::filesystem::exists(candidate)) return candidate;
        }

        return {};
    }

    std::string read_file(const std::filesystem::path &path) {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();

        return buffer.str();
    }

    // Offset z SyntaxError -> czytelna pozycja plus wycinek źródła dookoła,
    // bo w zminifikowanym kodzie sam numer linii nic nie mówi.
    std::string describe_position(const std::string &source, uint32_t offset) {
        const uint32_t safe = offset <= source.size() ? offset : static_cast<uint32_t>(source.size());

        uint32_t line = 1;
        uint32_t column = 1;
        for (uint32_t i = 0; i < safe; ++i) {
            if (source[i] == '\n') { ++line; column = 1; continue; }
            ++column;
        }

        const uint32_t window = 100;
        const uint32_t from = safe > window ? safe - window : 0;
        const uint32_t to = std::min<uint32_t>(safe + window, static_cast<uint32_t>(source.size()));

        std::ostringstream out;
        out << "offset " << safe << " (linia " << line << ", kolumna " << column << ")\n"
            << "  ...przed: " << source.substr(from, safe - from) << "\n"
            << "  >>>HERE<<< " << source.substr(safe, to - safe);

        return out.str();
    }

    double milliseconds_since(const std::chrono::steady_clock::time_point &start) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        return std::chrono::duration<double, std::milli>(elapsed).count();
    }
}

TEST_CASE("bundle React Native przechodzi przez lekser") {
    const std::filesystem::path path = find_bundle();
    if (path.empty()) {
        MESSAGE("brak main.jsbundle - test pominiety (ustaw PERUNEJS_BUNDLE)");
        return;
    }

    const std::string source = read_file(path);
    REQUIRE(source.size() > 0);
    MESSAGE("bundle: " << path.string() << ", " << source.size() << " bajtow");

    const auto started = std::chrono::steady_clock::now();

    try {
        Lexer lexer(source);
        const std::vector<Token *> tokens = lexer.scan_tokens();

        MESSAGE("lekser: " << tokens.size() << " tokenow w " << milliseconds_since(started) << " ms");
        CHECK(tokens.size() > 0);
    } catch (const SyntaxError &error) {
        FAIL("lekser przerwal: " << std::string(error.what()) << "\n" << describe_position(source, error.get_offset()));
    } catch (const std::exception &error) {
        FAIL("lekser rzucil wyjatek: " << std::string(error.what()));
    }
}

TEST_CASE("bundle React Native przechodzi przez parser") {
    const std::filesystem::path path = find_bundle();
    if (path.empty()) {
        MESSAGE("brak main.jsbundle - test pominiety (ustaw PERUNEJS_BUNDLE)");
        return;
    }

    const std::string source = read_file(path);
    REQUIRE(source.size() > 0);

    try {
        Lexer lexer(source);
        std::vector<Token *> tokens = lexer.scan_tokens();

        const auto started = std::chrono::steady_clock::now();
        Parser parser(std::move(tokens));
        const std::unique_ptr<Program> program = parser.parse();

        MESSAGE("parser: " << program->body.size() << " instrukcji najwyzszego poziomu w "
                           << milliseconds_since(started) << " ms");
        CHECK(program->body.size() > 0);
    } catch (const SyntaxError &error) {
        FAIL("parser przerwal: " << std::string(error.what()) << "\n" << describe_position(source, error.get_offset()));
    } catch (const std::exception &error) {
        FAIL("parser rzucil wyjatek: " << std::string(error.what()));
    }
}

// ---------------------------------------------------------------------------
// Proba wykonania. Prelude udaje hosta wylacznie srodkami ES5 + print(),
// zeby test odslanial luki SILNIKA, a nie brak zaslepek RN.
// ---------------------------------------------------------------------------

namespace {
    const char *HOST_PRELUDE = R"JS(
globalThis.global = this;

globalThis.console = {
    log:   function () { print(Array.prototype.join.call(arguments, ' ')); },
    info:  function () { print(Array.prototype.join.call(arguments, ' ')); },
    warn:  function () { print(Array.prototype.join.call(arguments, ' ')); },
    error: function () { print(Array.prototype.join.call(arguments, ' ')); },
    debug: function () {}, trace: function () {},
    group: function () {}, groupEnd: function () {}, table: function () {}
};

globalThis.nativeLoggingHook   = function (message) { print(message); };
globalThis.nativePerformanceNow = function () { return Date.now(); };

var __queue = [];
globalThis.setTimeout  = function (fn) { __queue.push(fn); return __queue.length; };
globalThis.setInterval = function (fn) { return 0; };
globalThis.clearTimeout = function () {};
globalThis.clearInterval = function () {};
globalThis.setImmediate = function (fn) { __queue.push(fn); return __queue.length; };
globalThis.clearImmediate = function () {};
globalThis.queueMicrotask = function (fn) { __queue.push(fn); };
globalThis.requestAnimationFrame = function (fn) { __queue.push(fn); return 0; };
globalThis.cancelAnimationFrame = function () {};


globalThis.__fbBatchedBridgeConfig = { remoteModuleConfig: [] };

// Rejestr modulow wolanych z natywnego (tryb bridgeless).
var __callableModules = {};
globalThis.RN$registerCallableModule = function (name, factory) { __callableModules[name] = factory; };
// Zaslepka Fabrica. Na urzadzeniu instaluje ja natywny C++ RN przez JSI;
// tutaj zapisuje wywolania, zebysmy zobaczyli drzewo widokow.
var __wezly = {};
var __drzewo = [];
globalThis.nativeFabricUIManager = {
    createNode: function (tag, nazwa, rootTag, props, uchwyt) {
        __wezly[tag] = { tag: tag, nazwa: nazwa, props: props, dzieci: [], uchwyt: uchwyt };
        return __wezly[tag];
    },
    cloneNode: function (w) { return w; },
    cloneNodeWithNewChildren: function (w) { return { tag: w.tag, nazwa: w.nazwa, props: w.props, dzieci: [] }; },
    cloneNodeWithNewProps: function (w, props) { return { tag: w.tag, nazwa: w.nazwa, props: props, dzieci: w.dzieci }; },
    cloneNodeWithNewChildrenAndProps: function (w, props) { return { tag: w.tag, nazwa: w.nazwa, props: props, dzieci: [] }; },
    createChildSet: function () { return []; },
    appendChild: function (rodzic, dziecko) { rodzic.dzieci.push(dziecko); return rodzic; },
    appendChildToSet: function (zbior, dziecko) { zbior.push(dziecko); },
    completeRoot: function (rootTag, zbior) { __drzewo = zbior; },
    // Kanal zdarzen natywne -> JS. Na urzadzeniu Fabric wola ta funkcje,
    // gdy natywny widok cos zglasza; tutaj zapamietujemy ja, zeby moc
    // zdarzenie wstrzyknac recznie.
    registerEventHandler: function (fn) { globalThis.__obslugaZdarzen = fn; },
    measure: function () {}, measureInWindow: function () {}, measureLayout: function () {},
    setNativeProps: function () {}, dispatchCommand: function () {},
    sendAccessibilityEvent: function () {}, getRelativeLayoutMetrics: function () { return null; },
    unstable_DefaultEventPriority: 0, unstable_DiscreteEventPriority: 1,
    unstable_getCurrentEventPriority: function () { return 0; }
};

globalThis.__wypiszDrzewo = function () {
    var linie = [];
    function chodz(w, wciecie) {
        if (!w) return;
        var opis = wciecie + (w.nazwa || '?');
        if (w.props && typeof w.props.text === 'string') opis += ' "' + w.props.text + '"';
        linie.push(opis);
        for (var i = 0; i < (w.dzieci || []).length; i++) chodz(w.dzieci[i], wciecie + '  ');
    }
    for (var i = 0; i < __drzewo.length; i++) chodz(__drzewo[i], '');
    return linie.join('\n');
};

// Migawka tozsamosci metod wbudowanych. Po bundlu porownamy ja i zobaczymy,
// ktore metody core-js uznal za niezgodne i podmienil. Ta lista to dokladna
// miara niezgodnosci silnika.
globalThis.__migawka = {};
(function () {
    var cele = {
        'Object': Object, 'Object.prototype': Object.prototype,
        'Array': Array, 'Array.prototype': Array.prototype,
        'String': String, 'String.prototype': String.prototype,
        'Number': Number, 'Number.prototype': Number.prototype,
        'Function.prototype': Function.prototype,
        'Date': Date, 'Date.prototype': Date.prototype,
        'RegExp.prototype': RegExp.prototype,
        'Math': Math, 'JSON': JSON, 'Symbol': Symbol,
        'Error.prototype': Error.prototype
    };
    for (var nazwa in cele) {
        if (!Object.prototype.hasOwnProperty.call(cele, nazwa)) continue;
        var cel = cele[nazwa];
        if (!cel) continue;
        var klucze = Object.getOwnPropertyNames(cel);
        for (var i = 0; i < klucze.length; i++) {
            try {
                var d = Object.getOwnPropertyDescriptor(cel, klucze[i]);
                if (d && typeof d.value === 'function') {
                    globalThis.__migawka[nazwa + '.' + klucze[i]] = d.value;
                }
            } catch (e) {}
        }
    }
})();

globalThis.__porownajMigawke = function () {
    var cele = {
        'Object': Object, 'Object.prototype': Object.prototype,
        'Array': Array, 'Array.prototype': Array.prototype,
        'String': String, 'String.prototype': String.prototype,
        'Number': Number, 'Number.prototype': Number.prototype,
        'Function.prototype': Function.prototype,
        'Date': Date, 'Date.prototype': Date.prototype,
        'RegExp.prototype': RegExp.prototype,
        'Math': Math, 'JSON': JSON, 'Symbol': Symbol,
        'Error.prototype': Error.prototype
    };
    var podmienione = [];
    var dodane = [];
    for (var nazwa in cele) {
        if (!Object.prototype.hasOwnProperty.call(cele, nazwa)) continue;
        var cel = cele[nazwa];
        if (!cel) continue;
        var klucze = Object.getOwnPropertyNames(cel);
        for (var i = 0; i < klucze.length; i++) {
            var pelna = nazwa + '.' + klucze[i];
            try {
                var d = Object.getOwnPropertyDescriptor(cel, klucze[i]);
                if (!d || typeof d.value !== 'function') continue;
                if (!(pelna in globalThis.__migawka)) { dodane.push(pelna); continue; }
                if (globalThis.__migawka[pelna] !== d.value) podmienione.push(pelna);
            } catch (e) {}
        }
    }
    return { podmienione: podmienione.sort(), dodane: dodane.sort() };
};

globalThis.RN$Bridgeless = true;
// Generyczna zaslepka modulu natywnego. Bez Proxy nie da sie zrobic
// naprawde generycznej, wiec dajemy zestaw metod, ktorych RN uzywa przy starcie.
globalThis.__turboModuleProxy = function (name) {
    var noop = function () { return undefined; };
    return {
        getConstants: function () {
            var ekran = { width: 390, height: 844, scale: 3, fontScale: 1 };
            return {
                Dimensions: { window: ekran, screen: ekran },
                isTesting: true,
                reactNativeVersion: { major: 0, minor: 0, patch: 0 },
                forceTouchAvailable: false,
                osVersion: '17.0', systemName: 'iOS', interfaceIdiom: 'phone',
                isRTL: false, doLeftAndRightSwapInRTL: true,
                localeIdentifier: 'pl_PL',
                // react-native-safe-area-context czyta to z modulu natywnego
                // i dopiero wtedy renderuje dzieci.
                initialWindowMetrics: {
                    insets: { top: 47, left: 0, bottom: 34, right: 0 },
                    frame: { x: 0, y: 0, width: 390, height: 844 }
                }
            };
        },
        addListener: noop, removeListeners: noop, removeListener: noop,
        reportException: noop, reportFatalException: noop, reportSoftException: noop,
        updateExceptionMessage: noop, dismissRedbox: noop,
        createTimer: noop, deleteTimer: noop, setSendIdleEvents: noop,
        getScriptText: noop, setGlobalHandler: noop, getGlobalHandler: noop,
        setExtraData: noop, log: noop, emit: noop,
        getColorScheme: function () { return 'light'; },
        setColorScheme: noop,
        getInitialURL: function () { return null; },
        getCurrentAppState: noop, getString: noop, setString: noop,
        addEventListener: noop, removeEventListener: noop,

        // NativeDOMCxx — API drzewa DOM po stronie natywnej (RN 0.7x).
        linkRootNode: function (rootTag, instanceHandle) {
            return { tag: rootTag, nazwa: 'RootView', props: {}, dzieci: [], instanceHandle: instanceHandle };
        },
        getParentNode: function () { return null; },
        getChildNodes: function () { return []; },
        isConnected: function () { return true; },
        compareDocumentPosition: function () { return 0; },
        getTextContent: function () { return ''; },
        getBoundingClientRect: function () { return [0, 0, 0, 0]; },
        getOffset: function () { return [0, 0, 0, 0]; },
        getScrollPosition: function () { return [0, 0]; },
        getScrollSize: function () { return [0, 0]; },
        getInnerSize: function () { return [0, 0]; },
        getBorderSize: function () { return [0, 0, 0, 0]; },
        getTagName: function () { return 'RN:View'; },
        hasPointerCapture: function () { return false; },
        setPointerCapture: noop, releasePointerCapture: noop
    };
};
globalThis.nativeCallSyncHook = function () { return undefined; };
globalThis.nativeFlushQueueImmediate = function () {};
)JS";
}

TEST_CASE("bundle React Native startuje w ewaluatorze") {
    const std::filesystem::path path = find_bundle();
    if (path.empty()) {
        MESSAGE("brak main.jsbundle - test pominiety (ustaw PERUNEJS_BUNDLE)");
        return;
    }

    const std::string source = read_file(path);
    REQUIRE(source.size() > 0);

    Heap heap;
    std::ostringstream sink;
    Evaluator evaluator(heap, sink);
    Environment *global = setup_globals(heap, evaluator);

    const Completion prelude = evaluator.run_script(global, HOST_PRELUDE);
    REQUIRE(prelude.type != COMPLETION_TYPE::THROW);

    // Metro konczy bundle wywolaniami __r(<rdzen RN>) i __r(<entry apki>).
    // Rozdzielamy je, zeby wiedziec, ktory etap pada.
    std::string registration = source;
    std::vector<std::string> entries;

    if (const std::size_t last = source.rfind("\n__r("); last != std::string::npos) {
        if (const std::size_t first = source.rfind("\n__r(", last - 1); first != std::string::npos) {
            registration = source.substr(0, first);
            entries.push_back(source.substr(first, last - first));
            entries.push_back(source.substr(last));
        }
    }

    const auto started = std::chrono::steady_clock::now();

    try {
        Completion result = evaluator.run_script(global, registration);
        MESSAGE("rejestracja modulow: " << milliseconds_since(started) << " ms, GC: " << heap.collections);

        // Metro oddaje bledy modulow do ErrorUtils, ktory RN instaluje dopiero
        // w InitializeCore. Bez tego podpiecia awaria entry apki jest niema.
        const char *UNMASK = R"JS(
// Rdzen RN juz wystartowal i wzial, czego potrzebowal. Oddajemy pole
// core-js: jego wlasny polyfill Symbol jest kompletny, a dwa rownolegle
// swiaty symboli powoduja rekurencje w jego kontrolach wewnetrznych.
// RN w InitializeCore podmienia timery na wlasne, oparte o natywny modul
// Timing, ktorego tu nie ma. Przejmujemy je z powrotem na prosta kolejke,
// ktora pompujemy recznie z C++ - to wystarczy, zeby Promise i scheduler
// Reacta mialy gdzie odlozyc prace.
globalThis.setTimeout  = function (fn) { __queue.push(fn); return __queue.length; };
globalThis.setImmediate = function (fn) { __queue.push(fn); return __queue.length; };
globalThis.requestAnimationFrame = function (fn) { __queue.push(fn); return __queue.length; };
globalThis.clearTimeout = function () {};
globalThis.clearImmediate = function () {};
globalThis.cancelAnimationFrame = function () {};

// Pompa makrozadan. Limit jest bezpiecznikiem, nie polityka.
globalThis.__pompuj = function (limit) {
    var wykonane = 0;
    while (__queue.length > 0 && wykonane < limit) {
        var zadanie = __queue.shift();
        wykonane++;
        try { zadanie(); } catch (e) { print('zadanie rzucilo: ' + (e && e.message)); }
    }
    return wykonane;
};

if (globalThis.ErrorUtils) {
    globalThis.ErrorUtils.reportFatalError = function (e) {
        print('FATAL ' + (e && e.message) + '\n' + (e && e.stack));
    };
}
)JS";

        for (std::size_t i = 0; i < entries.size() && result.type != COMPLETION_TYPE::THROW; ++i) {
            if (i > 0) evaluator.run_script(global, UNMASK);

            const auto entry_started = std::chrono::steady_clock::now();
            result = evaluator.run_script(global, entries[i]);
            MESSAGE("etap" << (i + 1) << " " << entries[i].substr(1, 12) << " -> "
                    << std::string(result.type == COMPLETION_TYPE::THROW ? "THROW" : "ok")
                    << " po " << milliseconds_since(entry_started) << " ms");
        }

        const double took = milliseconds_since(started);

        if (result.type == COMPLETION_TYPE::THROW) {
            const JSValue thrown = result.get_value_or_undefined();

            std::string text = "<toString rzucil>";
            if (const Completion described = evaluator.to_string(thrown);
                described.type == COMPLETION_TYPE::NORMAL) {
                text = described.get_value_or_undefined().to_string();
            }

            if (thrown.type() == JSValueType::Object) {
                if (const Completion stack = thrown.as_object()->get(evaluator, "stack");
                    stack.type == COMPLETION_TYPE::NORMAL
                    && stack.get_value_or_undefined().type() == JSValueType::String) {
                    text += "\n" + stack.get_value_or_undefined().to_string();
                }
            }

            MESSAGE("wyjscie bundla:\n" << sink.str());
            FAIL("bundle rzucil po " << took << " ms: " << text);
        }

        MESSAGE("bundle wykonany w " << took << " ms, kolekcji GC: " << heap.collections);

        // Dowód, ze bundle nie tylko sie wykonal, ale zrobil to, po co istnieje:
        // AppRegistry ma znac nazwe aplikacji z index.js.
        const char *VERIFY = R"JS(
print('moduly wolane z natywnego: ' + Object.keys(__callableModules).sort().join(', '));
var registry = __callableModules.AppRegistry();
var roznica = __porownajMigawke();
print('PODMIENIONE przez core-js (' + roznica.podmienione.length + '):');
print('  ' + roznica.podmienione.join('\n  '));
print('DODANE przez core-js (' + roznica.dodane.length + '):');
print('  ' + roznica.dodane.join(', '));

var apps = registry.getAppKeys();
print('zarejestrowane aplikacje: ' + apps.join(', '));

// Wlasciwy cel: entry z index.js ma zarejestrowac komponent aplikacji.
// Dopoki tego nie ma, bundle wykonal sie, ale nie zrobil swojej roboty.
if (apps.indexOf('MojaApka') < 0) {
    throw new Error('aplikacja nie zarejestrowana; widoczne tylko: ' + apps.join(', '));
}

registry.runApplication('MojaApka', { rootTag: 1, initialProps: {} });
var tury = 0;
while (__pompuj(5000) > 0 && tury < 20) { tury++; }

// RNCSafeAreaProvider renderuje dzieci dopiero po zdarzeniu onInsetsChange
// z natywnego widoku. Na urzadzeniu wysyla je natywny komponent; tutaj
// wysylamy je sami przez kanal zdarzen Fabrica.
if (globalThis.__obslugaZdarzen) {
    for (var tag in __wezly) {
        var w = __wezly[tag];
        if (!w || w.nazwa !== 'RNCSafeAreaProvider' || !w.uchwyt) continue;
        try {
            __obslugaZdarzen(w.uchwyt, 'topInsetsChange', {
                insets: { top: 47, left: 0, bottom: 34, right: 0 },
                frame: { x: 0, y: 0, width: 390, height: 844 }
            });
        } catch (e) { print('zdarzenie rzucilo: ' + (e && e.message)); }
    }
    while (__pompuj(5000) > 0 && tury < 40) { tury++; }
}
print('tury pompowania: ' + tury);
print('utworzone wezly: ' + Object.keys(__wezly).length);
var drzewo = __wypiszDrzewo();
print('--- drzewo widokow ---');
print(drzewo);

// Wlasciwy dowod: React wygenerowal natywne operacje widoku.
if (!drzewo || drzewo.indexOf('RCTRawText') < 0) {
    throw new Error('React nie wyprodukowal drzewa z trescia tekstowa');
}
)JS";
        const Completion verified = evaluator.run_script(global, VERIFY);
        if (verified.type == COMPLETION_TYPE::THROW) {
            const JSValue thrown = verified.get_value_or_undefined();

            std::string text;
            if (const Completion described = evaluator.to_string(thrown);
                described.type == COMPLETION_TYPE::NORMAL) {
                text = described.get_value_or_undefined().to_string();
            }

            if (thrown.type() == JSValueType::Object) {
                if (const Completion stack = thrown.as_object()->get(evaluator, "stack");
                    stack.type == COMPLETION_TYPE::NORMAL
                    && stack.get_value_or_undefined().type() == JSValueType::String) {
                    text += "\n" + stack.get_value_or_undefined().to_string();
                }
            }

            MESSAGE("wyjscie bundla:\n" << sink.str());
            FAIL("weryfikacja rzucila: " << text);
        }
        MESSAGE("wyjscie bundla:\n" << sink.str());
    } catch (const std::exception &error) {
        FAIL("awaria natywna po " << milliseconds_since(started) << " ms: "
             << std::string(error.what()));
    }
}

// ---------------------------------------------------------------------------
// Test wykrywania natywnych symboli — dokładnie te warunki sprawdza core-js
// (internals/symbol-constructor-detection). Jeśli którykolwiek jest fałszywy,
// core-js instaluje swój shim i podmienia Object.defineProperty oraz
// getOwnPropertyNames, co u nas kończy się nieskończoną rekurencją.
// ---------------------------------------------------------------------------
TEST_CASE("silnik przechodzi test natywnych symboli z core-js") {
    Heap heap;
    std::ostringstream sink;
    Evaluator evaluator(heap, sink);
    Environment *global = setup_globals(heap, evaluator);

    const char *CHECK = R"JS(
var wyniki = [];
function sprawdz(nazwa, wartosc) { wyniki.push(nazwa + '=' + wartosc); }

sprawdz('typeof Symbol', typeof Symbol);
sprawdz('typeof Symbol()', typeof Symbol());
sprawdz('getOwnPropertySymbols', typeof Object.getOwnPropertySymbols);
sprawdz('typeof Symbol.iterator', typeof Symbol.iterator);

var s = Symbol('symbol detection');
sprawdz('String(s)', String(s));
sprawdz('Object(s) instanceof Symbol', Object(s) instanceof Symbol);
sprawdz('Symbol.sham', typeof Symbol.sham);
sprawdz('s === s', s === s);
sprawdz('Symbol() === Symbol()', Symbol() === Symbol());
sprawdz('Symbol.for(x) === Symbol.for(x)', Symbol.for('x') === Symbol.for('x'));

var o = {};
o[s] = 7;
sprawdz('o[s]', o[s]);
sprawdz('Object.keys(o).length', Object.keys(o).length);

print(wyniki.join('\n'));
)JS";

    const Completion result = evaluator.run_script(global, CHECK);
    REQUIRE(result.type != COMPLETION_TYPE::THROW);
    MESSAGE("\n" << sink.str());
}

// ---------------------------------------------------------------------------
// Wzorzec "uncurryThis" z core-js: przechwycenie metody wbudowanej do zmiennej
// PRZED jej podmianą musi izolować od podmiany. Na tym stoi cały mechanizm
// polyfillowania — jeśli tu jest późne wiązanie, powstaje nieskończona
// rekurencja (inspectSource <-> Function.prototype.toString).
// ---------------------------------------------------------------------------
TEST_CASE("przechwycona metoda wbudowana jest odporna na pozniejsza podmiane") {
    Heap heap;
    std::ostringstream sink;
    Evaluator evaluator(heap, sink);
    Environment *global = setup_globals(heap, evaluator);

    const char *CHECK = R"JS(
function probka() { return 1; }

var oryginal = Function.prototype.toString;
var przezCall = Function.prototype.call.bind(oryginal);
var przezBind = oryginal.call.bind(oryginal);

Function.prototype.toString = function () { return 'PODMIENIONE'; };

print('bezposrednio      = ' + (oryginal.call(probka) === 'PODMIENIONE' ? 'PODMIENIONE' : 'oryginal'));
print('przez call.bind   = ' + (przezCall(probka) === 'PODMIENIONE' ? 'PODMIENIONE' : 'oryginal'));
print('przez bind        = ' + (przezBind(probka) === 'PODMIENIONE' ? 'PODMIENIONE' : 'oryginal'));
print('przez apply       = ' + (oryginal.apply(probka, []) === 'PODMIENIONE' ? 'PODMIENIONE' : 'oryginal'));
print('metoda na obiekcie= ' + probka.toString());
)JS";

    const Completion result = evaluator.run_script(global, CHECK);
    REQUIRE(result.type != COMPLETION_TYPE::THROW);
    MESSAGE("\n" << sink.str());
}

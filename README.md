# perunejs

A JavaScript engine written from scratch in C++20 — an AST-walking interpreter targeting
**ECMA-262 5.1**, with its own garbage collector and a JSI-compatible embedding layer that
lets it run a **React Native** app in place of Hermes.

> **Not for production use.** perunejs is an educational project, built to understand how
> a JavaScript engine's lexer, parser, AST, evaluator and memory management really work.
> It is slow compared to real engines, has known gaps in spec coverage, and has not been
> hardened for security or stability. Do not ship it.

## Status

- **test262** (ES5.1 subset): 14,470 / 15,167 — **95%**
- **Unit tests**: 837 cases, 5,120 assertions
- **React Native 0.87** (iOS, New Architecture): a production bundle (1.2 MB) executes
  in ~2 s and renders the app template's start screen

## Architecture

```
source ─▶ Lexer ─▶ Parser ─▶ AST ─▶ Evaluator ─▶ Completion
                                       │
                         Heap (GC) ◀───┴───▶ standard library
```

- **Lexer** — UTF-8 input, Unicode identifiers, regexp literals, line-terminator tracking for ASI.
- **Parser** — recursive descent covering the full ES5.1 grammar (ASI, strict mode, `with`,
  getters/setters).
- **Evaluator** — tree-walking; every step returns a `Completion` record (§8.9), so JS
  exceptions flow as values rather than C++ exceptions. References (§8.7), lexical
  environments (§10.2), hoisting (§10.5), `eval`, and stack-overflow protection (`RangeError`).
- **Standard library** — all of chapter 15, including a hand-written backtracking regular
  expression engine.

### Garbage collector

Mark & sweep, stop-the-world, non-moving, with **conservative stack scanning** (registers via
`setjmp` plus the native stack, including interior pointers). Precise roots come from the
evaluator, `PersistentValue` handles serve embedders, and weak references plus
`WeakMap`/`WeakSet` are implemented as ephemerons. `PERUN_GC_STRESS=1` forces a collection
on every allocation.

### Beyond ES5.1

Added where React Native or the JSI interface required it: `globalThis`, `Symbol`,
`Promise` with a microtask queue, `Map`/`Set`/`WeakMap`/`WeakSet`, `ArrayBuffer` and typed
arrays, `Object.setPrototypeOf` / `__proto__`, and a selection of ES2015+ methods.

## React Native

The [`embed/`](embed/) layer exposes an API modelled on `facebook::jsi::Runtime` (host
functions, host objects, errors in both directions, persistent and weak handles). A
`jsi::Runtime` adapter built on top of it (outside this repository, as a CocoaPods pod):

- runs the engine on its own 16 MB stack switched in assembly (the RN JS thread has 1 MB,
  too little for a tree-walking interpreter),
- is plugged into the app with a single `createJSRuntimeFactory()` override in `AppDelegate`.

The bundle must be built with `USE_HERMES=false` and transpiled to ES5 with Babel, using
core-js as the polyfill.

## Limitations

- ES5.1 syntax only — newer syntax relies on transpilation; `let`/`const` behave like `var`,
- no `Proxy`, `Reflect`, `Intl`, `BigInt`, `WeakRef`,
- no bytecode, JIT or debugger,
- iOS only on the React Native side.

## Building

```bash
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug --target tests && ./cmake-build-debug/tests
cmake --build cmake-build-debug --target test262 && ./cmake-build-debug/test262 ../test262
```

`-DPERUNEJS_ASAN=ON` builds with AddressSanitizer. The real React Native bundle test runs
when `main.jsbundle` is present in the working directory (or pointed to by `PERUNEJS_BUNDLE`).

## Layout

| Directory | Contents |
|---|---|
| `lexer/`, `parser.*`, `nodes.*` | source → tokens → AST |
| `evaluator/` | interpreter |
| `memory/` | values, objects, environments, heap and GC |
| `runtime/` | global object and standard library |
| `embed/` | engine embedding API |
| `tests/` | doctest suites |
| `tools/test262.cpp` | standards conformance runner |



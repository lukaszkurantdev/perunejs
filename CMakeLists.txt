cmake_minimum_required(VERSION 3.20)
project(perunejs CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror=return-type -Werror=switch)

option(PERUNEJS_ASAN "Build with AddressSanitizer" OFF)
if (PERUNEJS_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# ---------- biblioteka z całą logiką ----------
add_library(perunejs_core
        lexer/lexer.cpp
        lexer/token.cpp
        parser.cpp
        nodes.cpp
        ast_printer.cpp
        memory/js_value.cpp
        evaluator/evaluator.cpp
        utils/hash_string.cpp
        memory/completion.h
        memory/environment.cpp
        memory/environment.h
        memory/binding.h
        memory/cell.h
        memory/heap.h
        memory/js_object.h
        memory/js_function.h
        memory/js_function.cpp
        memory/js_native_function.h
        runtime/globals.cpp
        runtime/globals.h
        memory/reference.h
        memory/js_array.h
        memory/js_object.cpp
        memory/js_native_function.cpp
        memory/js_regexp.h
        memory/heap.cpp
        memory/marked_vector.h
        utils/native_stack.cpp
        utils/native_stack.h
        memory/js_arguments.h
        memory/js_arguments.h
        memory/js_wrapper.h
        memory/js_string.h
        utils/utf.cpp
        utils/utf.h
        memory/js_bound_function.cpp
        memory/js_bound_function.h
        runtime/builtins.h
        runtime/builtins.cpp
        runtime/builtins/function.cpp
        runtime/builtins/object.cpp
        runtime/builtins/error.cpp
        runtime/builtins/regexp.cpp
        runtime/builtins/global_functions.cpp
        runtime/builtins/array.cpp
        runtime/builtins/string.cpp
        runtime/builtins/number.cpp
        runtime/builtins/boolean.cpp
        runtime/builtins/math.cpp
        runtime/builtins/uri.cpp
        utils/unicode_id.h
        utils/unicode_id.cpp
        utils/unicode_case.h
        utils/unicode_case.cpp
        runtime/builtins/date.cpp
        memory/js_date.h
        runtime/builtins/date.cpp
        memory/js_date.h
        runtime/builtins/json.cpp
        runtime/builtins/collections.cpp
        runtime/builtins/es2015.cpp
        runtime/builtins/symbol.cpp
        runtime/builtins/promise.cpp
        memory/js_promise.h
        memory/js_symbol.h
        memory/js_array_buffer.h
        memory/persistent.h
        runtime/builtins/array_buffer.cpp
        embed/error.h
        embed/host_function.h
        embed/host_function.cpp
        embed/host_object.h
        embed/host_object.cpp
        embed/runtime.h
        embed/runtime.cpp
        memory/js_map.h
        runtime/regexp_engine.cpp
        runtime/regexp_engine.h
)
target_include_directories(perunejs_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---------- program ----------
add_executable(perunejs main.cpp
        memory/js_value.h
        evaluator/evaluator.h
        utils/hash_string.cpp
        utils/hash_string.h
        parser/depth_guard.h
        parser/syntax_error.h
)
target_link_libraries(perunejs PRIVATE perunejs_core)

# ---------- testy ----------
# ---------- testy ----------
set(DOCTEST_DIR ${CMAKE_BINARY_DIR}/third_party)
file(DOWNLOAD
        https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
        ${DOCTEST_DIR}/doctest/doctest.h)

enable_testing()

add_executable(tests
        tests/test_main.cpp
        tests/lexer.cpp
        tests/js_value.cpp
        tests/parser.cpp
        tests/js_object.cpp
        tests/evaluator.cpp
        tests/environment.cpp
        tests/gc.cpp
        tests/bundle.cpp
        tests/array_buffer.cpp
        tests/embed.cpp
        tests/prototype.cpp
)
target_link_libraries(tests PRIVATE perunejs_core)

if (PERUNEJS_ASAN AND APPLE)
    target_link_options(tests PRIVATE -Wl,-stack_size,0x20000000)
endif()

target_include_directories(tests PRIVATE ${DOCTEST_DIR})

add_test(NAME tests COMMAND tests)

# ---------- zgodność ze standardem: test262 ----------
add_executable(test262 tools/test262.cpp)
target_link_libraries(test262 PRIVATE perunejs_core)
target_include_directories(test262 PRIVATE ${CMAKE_SOURCE_DIR})

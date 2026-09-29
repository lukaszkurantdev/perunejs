#include <iostream>
#include <string>

#include "evaluator/evaluator.h"
#include "memory/js_value.h"
#include "runtime/globals.h"

using namespace std;

int main() {
    const string source = "var x = 10 + 3; print(x);";

    perunejs::Heap heap;
    perunejs::Evaluator evaluator(heap);
    perunejs::Environment *global = perunejs::setup_globals(heap, evaluator);

    const perunejs::Completion result = evaluator.run_script(global, source);

    if (result.type == perunejs::COMPLETION_TYPE::THROW) {
        const perunejs::Completion text = evaluator.to_string(result.get_value_or_undefined());

        cerr << "Uncaught "
             << (text.type == perunejs::COMPLETION_TYPE::THROW
                     ? "error" : text.get_value_or_undefined().to_string())
             << endl;

        return 1;
    }

    return 0;

    // // TODO:
    // // Trzeb aprzeprowadzić testy leksera –aby pozbyć się błędów
    // // Potem testy parsera dopiero.
    // for (auto token : tokens) {
    //     cout << token->type << " ";
    //
    //     cout << token->start << " ";
    //
    //     if (token->type == STRING) {
    //         cout << get<string>(token->value);
    //     }
    //     if (token->type == IDENTIFIER) {
    //         cout << get<string>(token->value);
    //     }
    //     if (token->type == NUMBER) {
    //         cout << get<double>(token->value);
    //     }
    //
    //     cout << endl;
    // }
    return 0;
}
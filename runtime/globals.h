#ifndef PERUNEJS_GLOBALS_H
#define PERUNEJS_GLOBALS_H
#include "evaluator/evaluator.h"
#include "memory/environment.h"
#include "memory/heap.h"

namespace perunejs {
    Environment* setup_globals(Heap &heap, Evaluator &evaluator);
}

#endif //PERUNEJS_GLOBALS_H

#ifndef PERUNEJS_AST_PRINTER_H
#define PERUNEJS_AST_PRINTER_H

#include <ostream>

#include "nodes.h"

namespace perunejs {
    class AstPrinter {
    public:
        static void dump(Node *node, std::ostream &os, std::size_t indent);
    };
}

#endif //PERUNEJS_AST_PRINTER_H

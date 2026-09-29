
#ifndef PERUNEJS_CELL_H
#define PERUNEJS_CELL_H

namespace perunejs {
    class Cell;

    class CellVisitor {
    public:
        virtual ~CellVisitor() = default;
        virtual void visit(Cell *cell) = 0;
    };

    class Cell {
    public:
        bool marked = false;

        Cell() = default;
        virtual ~Cell() = default;

        Cell(const Cell &) = delete;
        Cell &operator=(const Cell &) = delete;

        virtual void trace(CellVisitor &visitor) const = 0;
    };
}

#endif //PERUNEJS_CELL_H

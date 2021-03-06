#include "carray.h"
#include "cgl.h"
#ifdef QT_VERSION
#include <QDataStream>
#include <cmath>
#endif // QT_VERSION

CArray::CArray(int size, double val, Orientation orienataion)
  : std::vector<double>(size, val)
  , __orientation(orienataion){ }


CArray::CArray(double* data, int size, Orientation orienataion)
  : std::vector<double>(size)
  , __orientation(orienataion) {
    memcpy(this->data(), data, size * sizeof(double));
}

CArray::Orientation CArray::getOrientation() const { return __orientation; }
void CArray::setOrientation(Orientation o) { __orientation = o; }
void CArray::transpose() { __orientation = __orientation == Horizontal ? Vertical : Horizontal; }

IndexRange CArray::estimateRangeIndex() const {
    IndexRange domain(0);
    RealRange range(std::numeric_limits<double>::infinity(),
                   -std::numeric_limits<double>::infinity());
    int i(0);
    for(const_iterator it(this->begin()), end(this->end()); it != end; ++it, ++i) {
        if (*it == *it && fabs(*it) != std::numeric_limits<double>::infinity()) {
            if (range.getMin() > *it) {
                range.setMin(*it);
                domain.setMin(i);
            } else if (range.getMax() < *it) {
                range.setMax(*it);
                domain.setMax(i);
            }
        }
    }
    return domain;
}
RealRange CArray::estimateRange() const {
    RealRange domain(std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity());
    for(const_iterator it(this->begin()), end(this->end()); it != end; ++it) {
        if (*it == *it && fabs(*it) != std::numeric_limits<double>::infinity()) {
            domain.include(*it);
        }
    }
    return domain;
}


void CArray::grade(int count) {
    RealRange range(estimateRange());
    double h(range.range());
    double step = h / count;
    for (CArray::iterator i(this->begin()), end(this->end()); i != end; ++i) {
        int bin((*i - range.getMin()) / step);
        if (bin == count) {
            --bin;
        }
        *i = range.getMin() + step / 2. + bin * step;
    }
}



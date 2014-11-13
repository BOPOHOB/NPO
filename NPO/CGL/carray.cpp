#include "carray.h"
#include "cgl.h"
#ifdef QT_VERSION
#include <QDataStream>
#endif // QT_VERSION
namespace CGL {

CArray::CArray(int size, double val) : std::vector<double>(size, val) { }


CArray::CArray(double* data, int size) : std::vector<double>(size) {
    memcpy(this->data(), data, size * sizeof(double));
}



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



}

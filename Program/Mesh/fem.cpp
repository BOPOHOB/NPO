#include "fem.h"
#include <iostream>
#include "cparse.h"
#include <algorithm>
#include "elements/quad.h"
#include "elements/tetra.h"
#include "elements/tria.h"
#include "elements/lines.h"
#include "elements/hexa.h"
#include "cgl.h"
#include "conio.h"
#include <QFile>
#include <QTextStream>
#include <sstream>
#include <cassert>
#include "eigenmode.h"
#include <QApplication>

const int FEM::LOW_POLYGON = 300;
const unsigned char FEM::CONST_BLACK[] = { 0x00, 0x00, 0x00 };
bool FEM::usePyParser = false;

FEM::FEM() : modelType(Undefined) {}

FEM::FEM(const QString& fileName) : modelType(Undefined) {
    read(fileName);
}

FEM::FEM(const FEM& g)
    : modelType(g.modelType)
    , vertexes(g.vertexes)
    , trace(g.trace.size())
    , sqre(g.sqre)
    , isTraced(g.isTraced)
    , file(g.file)
    , name(g.name)
    , shells(g.shells)
    , materials(g.materials)
    , UNVTransformations(g.UNVTransformations)
    , colors(g.colors)
    , measurment(g.measurment)
    , modes(g.modes)
    , linksPoint(g.linksPoint)
    , linksSolution(g.linksSolution)
    , constraints(g.constraints)
    , superElementsId(g.superElementsId)
{
    Trace::const_iterator s(g.trace.begin());
    Trace::iterator d(trace.begin());
    Trace::iterator end(trace.end());
    while (d != end) {
        *d = *s ? (*s)->clone() : 0;
        ++d;
        ++s;
    }
    CoordinateSystems::const_iterator i(g.systems.begin());
    CoordinateSystems::const_iterator end2(g.systems.end());
    while (i != end2) {
        systems.insert(i.key(), i.value()->clone());
        ++i;
    }
}

bool FEM::read(const QString &fileName) {
    const QString format(fileName.split('.').last());
    if (!format.compare("BDF", Qt::CaseInsensitive) || !format.compare("dat", Qt::CaseInsensitive)) {
        this->readBDF(fileName);
        return true;
    } else if (!format.compare("UNV", Qt::CaseInsensitive)) {
        this->readUNV(fileName);
        return true;
    } else if (!format.compare("txt", Qt::CaseInsensitive)) {
        modes.readTXT(fileName);
        modes.estimateDefoultMagnitudes();
        return true;
    } else if (!format.compare("f06", Qt::CaseInsensitive)) {
        modes.readF06(fileName);
        for (const CoordinateLink& l : linksSolution) {
            if (systems.contains(l.first)) {
                CGL::RectangularCoordinateSystem* s(systems[l.first]);
                for (EigenMode& m : modes) {
                    try {
                        s->toGlobal(m.form()(l.second));
                        s->toGlobal(m.band()(l.second));
                    } catch (std::out_of_range&) { qWarning() << "form or band of any mode haven't node " + l.second; }
                };
            } else {
                qDebug() << "doesn't exist" << l.first << "system!";
            }
        }
        modes.estimateDefoultMagnitudes();
        return true;
    }

    return false;
}

FEM::~FEM() {
    qDeleteAll(trace);
    qDeleteAll(systems);
}

QString FEM::getName() const
{
    return name;
}
void FEM::setName(const QString & n)
{
    name = n;
}

void FEM::estimateTraced()
{
    isTraced.fill(false, static_cast<int>(vertexes.length()));
    for (Trace::const_iterator i(trace.begin()), end(trace.end()); i != end; ++i) {
        if (*i) {
            (*i)->fillTraced(isTraced);
        }
    }
}

void FEM::estimateBox()
{
    sqre = CParallelepiped();
    int i(isTraced.size());
    while (i) {
        --i;
        if (isTraced.testBit(i)) {
            sqre.include(vertexes.data() + i * 3);
        }
    }
}

void FEM::coordinateSystemImplimentation() {
    for (CoordinateLinks::const_iterator l(linksPoint.begin()), end(linksPoint.end()); l != end; ++l) {
        if (systems.contains(l->first)) {
            for (EigenModes::iterator i(modes.begin()); i != modes.end(); ++i) {
                systems[l->first]->toGlobal(i->form()(l->second));
            }
        }
    }
}

void FEM::scarfUp(PyParse::BDFEntity& entity) {
#ifndef QT_NO_DEBUG
    qDebug() << "\tPyNastran parser";
#endif
    qint32 size;
    Q_ASSERT(entity.read(static_cast<char*>(static_cast<void*>(&size)), sizeof(size)) == sizeof(size));
    vertexes.resize(size, 0.0f);
    entity.waitForReadyRead();
    Q_ASSERT(entity.read(static_cast<char*>(static_cast<void*>(vertexes.data())), size * sizeof(float))
             == size * sizeof(float));

    //import the vertexes data
    entity.waitForReadyRead();
    Q_ASSERT(entity.read(static_cast<char*>(static_cast<void*>(&size)), sizeof(size)) == sizeof(size));
    trace.resize(size + 1);
    entity.waitForReadyRead();
    Q_ASSERT(entity.read(static_cast<char*>(static_cast<void*>(&size)), sizeof(size)) == sizeof(size));
    //import the trace information
    for (int i(0); i != size; ++i) {
        int id;
        entity.waitForReadyRead();
        Q_ASSERT(entity.read(static_cast<char*>(static_cast<void*>(&id)), sizeof(id)) == sizeof(id));\
        entity.waitForReadyRead();
        trace[id] = FinitElement::load(entity);\
    }
    /// @todo add import of shell and matherials data
}

bool FEM::readBDF(const QString &fileName)
{
#ifndef QT_NO_DEBUG
    qDebug() << "BDF parsing";
    QTime loop(QTime::currentTime());
#endif
    if (!QFile(fileName).open(QFile::ReadOnly)) {
        throw QFileDevice::OpenError;
    }

    vertexes.clear();

    qDeleteAll(trace);
    trace.clear();
    UNVTransformations.clear();

    if (usePyParser) {
        scarfUp(PyParse::BDFEntity(fileName, QApplication::topLevelWidgets().first()));
    } else {
        nativeBDFParser(fileName);
    }

    name = (file = fileName).split('/').last();
    estimateTraced();
    estimateBox();
    modelType = Theory;
#ifndef QT_NO_DEBUG
    qDebug() << '\t' << loop.msecsTo(QTime::currentTime()) / 1e3 << " sec parsing pelay for .bdf model with " << vertexes.length() << " nodes";
#endif
    return true;
}

int FEM::arriveKnownUNVBlock(CGL::CParse& f) {
    while (*f && !(f.testPrew("    -1\n    15\n") ||
                   f.testPrew("    -1\n    82\n") ||
                   f.testPrew("    -1\n  2420\n") ||
                   f.testPrew("    -1\n  2411\n")))
        ++f;
    f.skipRow();
    if        (f.testPrew("    15\n")) {
        f.skipRow();
        return 15;
    } else if (f.testPrew("    82\n")) {
        f.skipRow();
        return 82;
    } else if (f.testPrew("  2420\n")) {
        f.skipRow();
        return 2420;
    } else if (f.testPrew("  2411\n")) {
        f.skipRow();
        return 2411;
    }
    return *f ? -1 : 0;
}

bool FEM::readUNV(const QString &fileName)
{
#ifndef QT_NO_DEBUG
    qDebug() << "UNV parsing";
#endif
    QTime loop(QTime::currentTime());
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw QFileDevice::OpenError;
    }
    this->file = fileName;
    if (name.isEmpty()) {
        name = fileName.split('/').last();
    }
    CGL::CParse f(CGL::Parse::parseFile(fileName.toStdString()));
    char* memory(f.data());
    f.UNIXRowSymbol();

    vertexes.resize(0);
    if (!trace.empty()) {
        qDeleteAll(trace);
        trace.clear();
    }
    UNVTransformations.clear();

    core::Lines* elem(new core::Lines);
    trace.push_back(elem);
    int blockId;
    while (blockId = arriveKnownUNVBlock(f)) {
        switch (blockId) {
        case 15:
        {
            //it's set of vertexes
            while (!f.testPrew("    -1")) {
                f.integer();
                f.integer();
                f.integer();
                f.integer();
                vertexes.push_back(f.real());
                vertexes.push_back(f.real());
                vertexes.push_back(f.real());
                ++f;
            }
#ifndef QT_NO_DEBUG
            qDebug() << '\t' << vertexes.length() << " vertexes in geometryr";
#endif
        } break;
        case 82: {
            //it's the trace of experemental nodes
            if (*f != ' ' || f[1] != '\n') {
                f.skipRow();
                f.skipRow();
            }
            while (!f.testPrew("\n    -1")) {
                unsigned n(f.integer() - 1);
                if (n < unsigned(-1)) {
                    elem->addNode(n);
                } else {
                    trace.push_back(elem = new core::Lines);
                }
            }
            ++f;
            f.skipRow();
        } break;
        case 2411: {
            //it's enother way to declare a vertexes
            while (!f.testPrew("    -1\n")) {
                f.skipRow();
                vertexes.push_back(f.real());
                vertexes.push_back(f.real());
                vertexes.push_back(f.real());
                f.skipRow();
            }
        } break;
        case 2420: {
            f.skipRow();
            f.skipRow();
            //it's a transformation matrices
            while (!f.testPrew("    -1\n")) {
                const int i(f.integer() - 1);
                if (UNVTransformations.size() <= i) {
                    UNVTransformations.resize(i + 1);
                }
                f.skipRow();
                f.skipRow();
                QMatrix3x3& m(UNVTransformations[i]);
                m(0,0) = f.real();
                m(0,1) = f.real();
                m(0,2) = f.real();

                m(1,0) = f.real();
                m(1,1) = f.real();
                m(1,2) = f.real();

                m(2,0) = f.real();
                m(2,1) = f.real();
                m(2,2) = f.real();
                f.skipRow();
                f.skipRow();
            }
        } break;
        case -1:
        default:
            f -= 6;
#ifndef QT_NO_DEBUG
            qDebug() << "\tinknown UNV block (" << f.integer() << "). try to skip. . .";
#endif
            while (!f.testPrew("    -1\n")) {
                f.skipRow();
            }
        }
    }
    delete memory;
    estimateTraced();
    estimateBox();
    modelType = Practic;
#ifndef QT_NO_DEBUG
    qDebug() << '\t' << loop.msecsTo(QTime::currentTime()) / 1e3 << "s parsing pelay";
#endif
    return true;
}

void FEM::UNVTransformation(EigenModes &f) const {
    for (EigenModes::iterator it(f.begin()); it != f.end(); ++it) {
        if (it->form().length() == UNVTransformations.size()) {
            std::vector<QMatrix3x3>::const_iterator m(UNVTransformations.begin());
            CGL::CVertexes& v(it->form());
            for (int i(0); i != v.length(); ++i) {
                v(i) = QVector3D(v(i).x() * ((*m)(0,0) + (*m)(1,0) + (*m)(2,0)),
                                 v(i).y() * ((*m)(0,1) + (*m)(1,1) + (*m)(2,1)),
                                 v(i).z() * ((*m)(0,2) + (*m)(1,2) + (*m)(2,2)));
                ++m;
            }
        }
    }
}

bool FEM::colorized() const {
    return !colors.empty();
}

bool FEM::colorizedElements() const {
    return colors.size() == trace.size() * 3;
}


void FEM::render() const {
    if (colorized()) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(3, GL_UNSIGNED_BYTE, 0, colors.data());
    } else {
        glDisableClientState(GL_COLOR_ARRAY);
        glColor3ub(0x00,0xFF,0x88);
    }


    if (colorizedElements()) {
        const unsigned char* color(colors.data());
        for (Trace::const_iterator it(trace.begin()), end(trace.end()); it != end; ++it, color += 3) {
            glColor3ubv(color);
            if (*it) {
                (*it)->render();
            }
        }
    } else {
        for (Trace::const_iterator it(trace.begin()), end(trace.end()); it != end; ++it) {
            if (*it) {
                (*it)->render();
            }
        }
    }

    //display nodes to low-nodes model
    if (vertexes.size() < LOW_POLYGON) {
        std::vector<GLuint> indexes;
        indexes.reserve(isTraced.size());
        for (int i(0); i != isTraced.size(); ++i) {
            if (isTraced.testBit(i)) {
                indexes.push_back(i);
            }
        }
        glDisableClientState(GL_COLOR_ARRAY);
        glColor3ub(0x22,0x22,0x22);
        glDrawElements(GL_POINTS, static_cast<GLsizei>(indexes.size()), GL_UNSIGNED_INT, indexes.data());
    }
}

void FEM::renderNet() const {
    glDisableClientState(GL_COLOR_ARRAY);
    glColor3ub(0x00,0x88,0x88);
    for (Trace::const_iterator it(trace.begin()), end(trace.end()); it != end; ++it) {
        if (*it) {
            (*it)->renderNet();
        }
    }
}

void FEM::renderSelectLabel(int vertex) const {
    glBegin(GL_POINTS);
    glColor3ub(0xFF,0x00,0xFF);
    glBegin(GL_POINTS);
    glVertex3fv(vertexes.data() + vertex * 3);
    glEnd();
}

void FEM::colorize(const CArray &v, const QString& mes) const
{
    if (static_cast<int>(v.size()) != vertexes.length()) {
        colors.resize(0);
        return;
    }
    measurment = mes;

    colorizeFromArray(v);
}

void FEM::colorizeFromArray(const CArray& v) const
{

    RealRange range(v.estimateRange());

    range.flatProof();

    const float minV(range.getMin());
    const float height(range.range());
    const float heightp2(height / 2.0f);
    const float heightp4(height / 4.0f);
    const float height3p4(height / 4.0f * 3.0f);

    colors.resize(v.size() * 3);
    CGL::Colors::iterator i(colors.begin());
    for (CArray::const_iterator it(v.begin()), end(v.end()); it != end; ++it)
    {
        const float z(*it - minV);
        unsigned char r, g, b;
        if (z != z) {
            r = g = b = 0;
        } else if (z <= 0) {
            r = g = 0;
            b = 255;
        } else if (z >= height) {
            r = 255;
            g = b = 0;
        } else {
            b = z <= heightp4 ? 255 : z < heightp2 ? (heightp2 - z ) * 1024.0f / height : 0;
            g = z < heightp4 ? z * 1024.0f / height : z <= height3p4 ? 255 : ( height - z ) * 1024.0f / height;
            r = z < heightp2 ? 0 : z < height3p4 ? ( z - heightp2 ) * 1024.0f / height : 255;
        }
        *i = r;
        ++i;
        *i = g;
        ++i;
        *i = b;
        ++i;
    }
}

void FEM::colorizeElements(const CArray &v, const QString& mes) const
{
    if (v.size() != static_cast<size_t>(trace.size())) {
        return;
    }
    measurment = mes;

    colorizeFromArray(v);
}
CArray FEM::extractElasticityModulus() {
    CArray elasticyModulus(static_cast<int>(trace.size()));

    for (int i = 0; i != elasticyModulus.size(); ++i) {
        if (trace.at(i)) {
            if (trace[i]->getShell() >= shells.size()) {
            } else if (materials.size() <= shells[trace[i]->getShell()].getMatId()) {
            } else {
                elasticyModulus[i] = materials[shells[trace[i]->getShell()].getMatId()][Material::MAT1_E];
            }
        }
    }
    return elasticyModulus;
}

void FEM::colorize(const CGL::CVertexes &v, const QString& mes) const
{
    measurment = mes;

    RealRange range(v.estimateRange().flatProof());

    const float height(range.range());
    const float heightp2(height / 2.0f);
    const float heightp4(height / 4.0f);
    const float height3p4(height / 4.0f * 3.0f);

    colors.resize(std::min(v.size(), vertexes.size()));
    CGL::Colors::iterator i(colors.begin());
    for (int j(0), length(static_cast<int>(colors.size() / 3)); j != length; ++j)
    {
        const float z(v(j).length());
        unsigned char r, g, b;
        if(z <= 0) {
            r = g = 0;
            b = 255;
        }
        else if(z >= height) {
            r = 255;
            g = b = 0;
        }
        else
        {
            b = z <= heightp4 ? 255 : z < heightp2 ? (heightp2 - z ) * 1024.0f / height : 0;
            g = z < heightp4 ? z * 1024.0f / height : z <= height3p4 ? 255 : ( height - z ) * 1024.0f / height;
            r = z < heightp2 ? 0 : z < height3p4 ? ( z - heightp2 ) * 1024.0f / height : 255;
        }
        *i = r;
        ++i;
        *i = g;
        ++i;
        *i = b;
        ++i;
    }
}

bool operator==(const FEM &l, const FEM &r)
{
#ifndef QT_NO_DEBUG
    qDebug() << "meshes comparation (is" << &l << "==" << &r << ")";
#endif
    if (l.trace.size() != r.trace.size()){
        return false;
    }

    for (size_t i = 0; i < l.trace.size(); ++i){
        if (!l.trace[i]) continue;

        size_t leftSize = l.trace[i]->nodesCount();
        size_t rightSize = r.trace[i]->nodesCount();

        if (leftSize != rightSize) {
#ifndef QT_NO_DEBUG
            qDebug() << '\t' << i << " " << leftSize << " " << rightSize;
#endif
            return false;
        }

        quint32* left = l.trace[i]->nodes();
        quint32* right = r.trace[i]->nodes();

        for (size_t j = 0; j < leftSize; ++j) {
            if (left[j] != right[j]) {
#ifndef QT_NO_DEBUG
                qDebug() << '\t' << i << " " << left[j] << " " << right[j];
#endif
                return false;
            }
        }
    }

    if (l.systems.size() != r.systems.size()){
#ifndef QT_NO_DEBUG
        qDebug() << "\tsystems size " << l.systems.size() << " " << r.systems.size();
#endif
        return false;
    }

    auto systemLeft = l.systems.begin();
    auto systemRight = r.systems.begin();
    for (size_t i = 0; i < l.systems.size(); ++i) {
        if (systemLeft.key() != systemRight.key()){
#ifndef QT_NO_DEBUG
            qDebug() << "\tmaterials key " << i << " " << systemLeft.key() << " " << systemRight.key();
#endif
            return false;
        }

        if (!(systemLeft.value()->operator==(*systemRight.value()))) {
#ifndef QT_NO_DEBUG
             qDebug() << "\tmaterials value " << i;
#endif
            return false;
        }

        ++systemLeft;
        ++systemRight;
    }

    if (l.shells.size() != r.shells.size()){
#ifndef QT_NO_DEBUG
        qDebug() << "\tshells size " << l.shells.size() << " " << r.shells.size();
#endif
        return false;
    }
    for (size_t i = 0; i < l.shells.size(); ++i) {
        if (!(l.shells[i] == r.shells[i])) {
#ifndef QT_NO_DEBUG
            qDebug() << "\tshells i " << i;
#endif
            return false;
        }
    }

    if (l.materials.size() != r.materials.size()){
#ifndef QT_NO_DEBUG
        qDebug() << "\tmaterials size " << l.materials.size() << " " << r.materials.size();
#endif
        return false;
    }
    for (size_t i = 0; i < l.materials.size(); ++i) {
        if (!(l.materials[i] == r.materials[i])) {
#ifndef QT_NO_DEBUG
            qDebug() << "\tmaterials i " << i;
#endif
            return false;
        }
    }

    if (l.linksPoint.size() != r.linksPoint.size()){
#ifndef QT_NO_DEBUG
        qDebug() << "\tlinks size " << l.linksPoint.size() << " " << r.linksPoint.size();
#endif
        return false;
    }
    for (size_t i = 0; i < l.linksPoint.size(); ++i) {
        if ((l.linksPoint[i] != r.linksPoint[i])) {
#ifndef QT_NO_DEBUG
            qDebug() << "\tlinks i " << i;
#endif
            return false;
        }
    }
    if ((l.sqre == r.sqre) && (l.isTraced == r.isTraced) &&
            (l.measurment == r.measurment) && (l.file == r.file) && (l.modelType == r.modelType) &&
            (l.vertexes == r.vertexes) && (l.colors == r.colors)) {
#ifndef QT_NO_DEBUG
            qDebug() << "meses is equal";
#endif
        return true;
    }
#ifndef QT_NO_DEBUG
            qDebug() << "some of meshes isn't identity";
#endif
    return false;
}

QDataStream& operator << (QDataStream& out, const FEM& g) {
#ifndef QT_NO_DEBUG
    QTime loop(QTime::currentTime());
#endif
    out << g.sqre << g.isTraced << g.measurment << g.file << static_cast<int>(g.modelType);
    out << g.name;
    out << g.vertexes << g.colors;

    size_t shellsSize;
    shellsSize = g.shells.size();
    out << shellsSize;
    for (size_t i = 0; i < shellsSize; ++i) {
        out << g.shells[i];
    }

    size_t materialsSize;
    materialsSize = g.materials.size();
    out << materialsSize;
    for (size_t i = 0; i < materialsSize; ++i) {
        out << g.materials[i];
    }

    size_t linksSize;
    linksSize = g.linksPoint.size();
    out << linksSize;
    for (size_t i = 0; i < linksSize; ++i) {
        out << g.linksPoint[i].first;
        out << g.linksPoint[i].second;
    }

    out << g.trace.size();
    size_t realCount(0);
    for (FEM::Trace::const_iterator it(g.trace.begin()), end(g.trace.end()); it != end; ++it) {
        realCount += *it != nullptr;
    }
    out << realCount;

    for (size_t i(0); i != g.trace.size(); ++i) {
        if (g.trace[i] != nullptr) {
            out << i;
            out << *g.trace[i];
        }
    }

    out << g.systems.size();
    for (FEM::CoordinateSystems::const_iterator i(g.systems.begin()); i != g.systems.end(); ++i) {
        out << i.key();
        i.value()->save(out);
    }

    out << g.modes;

#ifndef QT_NO_DEBUG
    qDebug() << "\tmesh with name" << g.name <<
             "was finaly saved (" << loop.msecsTo(QTime::currentTime()) << "ms)";
#endif

    return out;
}
QDataStream& operator >> (QDataStream& in, FEM& g)
{
#ifndef QT_NO_DEBUG
    QTime loop(QTime::currentTime());
#endif
    int modelType;
    in >> g.sqre >> g.isTraced >> g.measurment >> g.file >> modelType;
    in >> g.name;
    g.modelType = static_cast<FEM::ModelType>(modelType);
    in >> g.vertexes >> g.colors;

    size_t shellsSize;
    in >> shellsSize;
    g.shells.resize(shellsSize);
    for (size_t i = 0; i < shellsSize; ++i) {
        in >> g.shells[i];
    }

    size_t materialsSize;
    in >> materialsSize;
    g.materials.resize(materialsSize);
    for (size_t i = 0; i < materialsSize; ++i) {
        in >> g.materials[i];
    }

    size_t linksSize;
    in >> linksSize;
    g.linksPoint.resize(linksSize);
    for (size_t i = 0; i < linksSize; ++i) {
        in >> g.linksPoint[i].first;
        in >> g.linksPoint[i].second;
    }


    size_t size;
    in >> size;

    size_t realCount;
    in >> realCount;

    g.trace.resize(size, 0);
    for (size_t i(0); i != realCount; ++i) {
        size_t id;
        in >> id;
        g.trace[id] = FinitElement::load(in);
        Q_ASSERT(g.trace[id] && "type element fail");
    }

    FEM::CoordinateSystems::size_type s;
    in >> s;
    int key;
    CGL::RectangularCoordinateSystem* val;
    while (s--) {
        in >> key;
        val = CGL::RectangularCoordinateSystem::load(in);
        g.systems.insert(key, val);
    }

    in >> g.modes;
#ifndef QT_NO_DEBUG
    qDebug() << "\tmesh with name" << g.name <<
             "was finaly uploaded (" << loop.msecsTo(QTime::currentTime()) << "ms)";
#endif
    return in;
}

void FEM::alignZero() {
    QVector3D center(this->box().center());
    if (center.lengthSquared() == 0 || vertexes.empty()) {
        return;
    }
    int i(static_cast<int>(vertexes.length()));
    do {
        --i;
        vertexes(i) -= center;
    } while (i);
    this->box().move(-center);
}

void FEM::scaleTo(double v) {
    if (box().size() == 0.0) {
        return;
    }
    double k(v / box().size());
    for(CGL::CVertexes::iterator i(vertexes.begin()), end(vertexes.end()); i != end; ++i) {
        *i *= k;
    }
    box().multX(k);
    box().multY(k);
    box().multZ(k);
}


void FEM::layToBDF(const QString& source, const QString& dest, const CArray& dE, const int difference)
{
    Materials newMat;
    newMat.resize(difference, materials.at(1));

    std::map<CArray::value_type, int> find;

    int k(0);
    for (int i(0); i != dE.size(); ++i) {
        if (find.find(dE.at(i)) == find.end()) {
            find[dE.at(i)] = k;
            newMat[k][Material::MAT1_E] += dE.at(i);
            newMat[k][Material::MAT1_E] = fabs(newMat[k][Material::MAT1_E]);
            newMat[k][Material::MAT1_G] = newMat[k][Material::MAT1_E] / 2.0 / (1.0 + newMat[k][Material::MAT1_NU]);
            ++k;
        }
    }

    Shells newShell(shells.size() * newMat.size());
    for (int i(0); i != newMat.size(); ++i) {
        for (int j(0); j != shells.size(); ++j) {
            newShell[i * shells.size() + j] = shells[j];
            newShell[i * shells.size() + j].setMatId(i);
        }
    }
    QBitArray isUsed(static_cast<int>(newShell.size()));


    QFile base(source);
    if (!base.open(QFile::ReadOnly | QFile::Text)) {
        return;
    }
    QByteArray data(base.readAll().append('\0'));
    CGL::CParse f(data.data());
    char* begin(f.data());


    std::vector<std::string> shellStrings(shells.size());
    std::vector<int> usedId;


    while (!f.testPrew("ENDDATA")) {
        std::string type(f.word());
        if (type == "CQUAD4"
         || type == "CTRIA3") {
            char* write(f.data() + 10 + (type == "CHEXA"));
            int i(f.integer());
            int shellId(static_cast<int>(trace.at(i)->getShell() + shells.size() * find[dE.at(i)]));
            if (std::find(usedId.begin(), usedId.end(), shellId) == usedId.end()) {
                usedId.push_back(shellId);
                isUsed.setBit(shellId);
            }
            std::stringstream convertor;
            convertor << shellId + shells.size();
            std::string buf(convertor.str());
            const char* m(buf.c_str());
            while (*m) {
                *write = *m;
                ++m;
                ++write;
            }
        } else if (type == "PSHELL") {
            int id(CGL::Parse(f).integer());
            shellStrings[id] = type + ' ' + f.string() + '\n';
        } else {
            ++f;
            f.skipRow();
        }
    }
    QString newString;
    for (int i(0); i != newMat.size(); ++i) {
        std::stringstream buf;
        buf << newMat[i][Material::MAT1_E];
        std::string s1("MAT1*    1              2.+11           7.69231+10      .3\n");
        static const std::string s2("*       7800.\n");
        static const int where(24);
        std::string num(buf.str());
        for (int j(0); j != num.size(); ++j) {
            s1[j + where] = num[j];
        }
        static const int whereG(40);
        std::stringstream bufG;
        bufG << newMat[i][Material::MAT1_G];
        std::string numG(bufG.str());
        for (int j(0); j != numG.size(); ++j) {
            s1[j + whereG] = numG[j];
        }
        static const int whereId(9);
        std::stringstream bufId;
        bufId << i + materials.size() + 1;
        std::string numId(bufId.str());
        for (int j(0); j != numId.size(); ++j) {
            s1[j + whereId] = numId[j];
        }
        newString += QString::fromStdString(s1);
        newString += QString::fromStdString(s2);
    }
    for (int it(0); it != usedId.size(); ++it) {
        int matrixId(usedId[it]);
        int i(matrixId / static_cast<int>(shells.size()));
        int j(matrixId % shells.size());

        int id(static_cast<int>(usedId[it] + shells.size()));
        std::string data = shellStrings[j];
        if (!data.empty()) {
            std::stringstream bufId;
            bufId << id;
            std::string idNum(bufId.str());
            static const int whereId(9);
            for (int k(0); k != idNum.size(); ++k) {
                data[k + whereId] = idNum[k];
            }
            std::stringstream buf;
            buf << i + 1;
            std::string num(buf.str());
            static const int where(17);
            for (int k(0); k != num.size(); ++k) {
                data[k + where] = num[k];
            }
            newString = newString + QString::fromStdString(data);
        } else {
        }
    }

    data.insert(f - data.data(), newString);



    QFile result(dest);
    if (!result.open(QFile::WriteOnly | QFile::Text)) {
        return;
    }
    result.write(begin);
}

std::vector<int> FEM::truncationIndexVector(const FEM& a, const FEM& b)
{
    const CGL::Vertexes& am(a.vertexes);
    const CGL::Vertexes& bm(b.vertexes);
    int aDimension(static_cast<int>(am.length()));
    int bDimension(static_cast<int>(bm.length()));
    CGL::Matrix dest(aDimension, bDimension);

    for (int i(0); i != bDimension; ++i) {
        for (int j = 0; j != aDimension; ++j) {
            dest[i][j] = (am(j) - bm(i)).lengthSquared();
        }
    }

    std::vector<int> numbers(bDimension, 0);
    for (int i(0); i != bDimension; ++i) {
        for (int j(0); j != aDimension; ++j) {
            if (dest[i][j] < dest[i][numbers.at(i)] && a.isTraced.testBit(j)) {
                numbers[i] = j;
            }
        }
    }
    return numbers;
}

FEM* FEM::truncation(const FEM& a, const FEM& b) {
    //first step consist of solution of one question. We must to deside whitch of models must be truncated.
    //Hi-vertexes model must be truncated to low-vertexes model obvuisly.
    const FEM& forTruncation(a.vertexes.size() > b.vertexes.size() ? b : a);
    const FEM& base         (a.vertexes.size() > b.vertexes.size() ? a : b);
    //Afer thet we may be shure in one statement: forTruncation.vertexes.size() < base.vertexes.size()
    //make copy from the base geometry.
    FEM* result = new FEM(base);
    //then set up the name and filename no signe the model as a result of truncation
    result->name = "truncated(" + a.name + " x " + b.name + ")";
    result->file = "truncated";
    result->modes.setFileName(result->file);

    if (result->modes.empty()) {
        //if it's haven't modes no reason to do any thing.
        return result;
    }
    //select vertexes in  the first mech form which will be associate with the second mech form
    std::vector<int> interrelations(FEM::truncationIndexVector(a, b));
    //and now just copy the form values from theory
    // a.form.size()
    result->modes.resize(a.modes.size());
    EigenModes::iterator receiver(result->modes.begin());
    for (EigenModes::const_iterator source(a.modes.begin()), end(a.modes.end()); source != end; ++source, ++receiver) {
        receiver->setFrequency(source->frequency());
        const CGL::CVertexes& theoryForm(source->form());
        CGL::CVertexes& truncatedForm(receiver->form());
        truncatedForm.resize(interrelations.size() * 3);
        int i(0);
        for (std::vector<int>::const_iterator interrelation(interrelations.begin()), end(interrelations.end()); interrelation != end; ++interrelation, ++i) {
            truncatedForm(i) = theoryForm(*interrelation);
        }
        receiver->updateExtremums();
        receiver->updatePreMac();
    }

    result->modes.estimateAutoMAC();

    return result;
}

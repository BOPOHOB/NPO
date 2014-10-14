#ifndef FORM_H
#define FORM_H
#include "CGL/cgl.h"
#include "CGL/cvertexes.h"

class Form
{
    float first;
    CGL::CVertexes second;
    CGL::CArray third;
public:
    Form();
    Form(float, const CGL::CVertexes&);
    Form(float, size_t);

    float getFrequency() const { return first; }
    void  setFrequency(float v) { first = v; }

    float& frequency() { return first; }
    float frequency() const { return first; }
    CGL::CVertexes& form() { return second; }
    const CGL::CVertexes& form() const { return second; }

    CGL::CArray& power() { return third; }
    const CGL::CArray& power() const { return third; }
};
typedef std::vector<Form> Forms;

#endif // FORM_H

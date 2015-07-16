#include "var.h"
#include "common.h"
#include "varinfo.h"

#if PY_MAJOR_VERSION >= 3
    #define PyInt_Check PyLong_Check
    #define PyInt_FromLong PyLong_FromLong
    #define PyInt_AsLong PyLong_AsLong
#endif

using namespace std;
using namespace wreport::python;
using namespace wreport;

extern "C" {

static _Varinfo dummy_var;

static PyObject* dpy_Var_code(dpy_Var* self, void* closure)
{
    return wrpy_varcode_format(self->var.code());
}
static PyObject* dpy_Var_isset(dpy_Var* self, void* closure) {
    if (self->var.isset())
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}
static PyObject* dpy_Var_info(dpy_Var* self, void* closure) {
    return (PyObject*)varinfo_create(self->var.info());
}

static PyGetSetDef dpy_Var_getsetters[] = {
    {"code", (getter)dpy_Var_code, NULL, "variable code", NULL },
    {"isset", (getter)dpy_Var_isset, NULL, "true if the value is set", NULL },
    {"info", (getter)dpy_Var_info, NULL, "Varinfo for this variable", NULL },
    {NULL}
};

static PyObject* dpy_Var_enqi(dpy_Var* self)
{
    try {
        return PyInt_FromLong(self->var.enqi());
    } WREPORT_CATCH_RETURN_PYO
}

static PyObject* dpy_Var_enqd(dpy_Var* self)
{
    try {
        return PyFloat_FromDouble(self->var.enqd());
    } WREPORT_CATCH_RETURN_PYO
}

static PyObject* dpy_Var_enqc(dpy_Var* self)
{
    try {
        return PyUnicode_FromString(self->var.enqc());
    } WREPORT_CATCH_RETURN_PYO
}

static PyObject* dpy_Var_enq(dpy_Var* self)
{
    return var_value_to_python(self->var);
}

static PyObject* dpy_Var_get(dpy_Var* self, PyObject* args, PyObject* kw)
{
    static char* kwlist[] = { "default", NULL };
    PyObject* def = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "|O", kwlist, &def))
        return NULL;
    if (self->var.isset())
        return var_value_to_python(self->var);
    else
    {
        Py_INCREF(def);
        return def;
    }
}

static PyObject* dpy_Var_format(dpy_Var* self, PyObject* args, PyObject* kw)
{
    static char* kwlist[] = { "default", NULL };
    const char* def = "";
    if (!PyArg_ParseTupleAndKeywords(args, kw, "|s", kwlist, &def))
        return NULL;
    std::string f = self->var.format(def);
    return PyUnicode_FromString(f.c_str());
}

static PyMethodDef dpy_Var_methods[] = {
    {"enqi", (PyCFunction)dpy_Var_enqi, METH_NOARGS, R"(
        enqi() -> long

        get the value of the variable, as an int
    )" },
    {"enqd", (PyCFunction)dpy_Var_enqd, METH_NOARGS, R"(
        enqd() -> float

        get the value of the variable, as a float
    )" },
    {"enqc", (PyCFunction)dpy_Var_enqc, METH_NOARGS, R"(
        enqc() -> str

        get the value of the variable, as a str
    )" },
    {"enq", (PyCFunction)dpy_Var_enq, METH_NOARGS, R"(
        enq() -> str|float|long

        get the value of the variable, as int, float or str according the variable definition
    )" },
    {"get", (PyCFunction)dpy_Var_get, METH_VARARGS | METH_KEYWORDS, R"(
        get(default=None) -> str|float|long|default

        get the value of the variable, with a default if it is unset
    )" },
    {"format", (PyCFunction)dpy_Var_format, METH_VARARGS | METH_KEYWORDS, R"(
        format(default="") -> str

        return a string with the formatted value of the variable
    )" },
    {NULL}
};

static int dpy_Var_init(dpy_Var* self, PyObject* args, PyObject* kw)
{
    PyObject* varinfo_or_var = nullptr;
    PyObject* val = nullptr;
    if (!PyArg_ParseTuple(args, "O|O", &varinfo_or_var, &val))
        return -1;

    try {
        if (dpy_Varinfo_Check(varinfo_or_var))
        {
            if (val == nullptr)
            {
                new (&self->var) Var(((const dpy_Varinfo*)varinfo_or_var)->info);
                return 0;
            }
            else
            {
                new (&self->var) Var(((const dpy_Varinfo*)varinfo_or_var)->info);
                return var_value_from_python(val, self->var);
            }
        }
        else if (dpy_Var_Check(varinfo_or_var))
        {
            new (&self->var) Var(((const dpy_Var*)varinfo_or_var)->var);
            return 0;
        }
        else
        {
            new (&self->var) Var(&dummy_var);
            PyErr_SetString(PyExc_ValueError, "First argument to wreport.Var should be wreport.Varinfo or wreport.Var");
            return -1;
        }
    } WREPORT_CATCH_RETURN_INT
}

static void dpy_Var_dealloc(dpy_Var* self)
{
    // Explicitly call destructor
    self->var.~Var();
}

static PyObject* dpy_Var_str(dpy_Var* self)
{
    std::string f = self->var.format("None");
    return PyUnicode_FromString(f.c_str());
}

static PyObject* dpy_Var_repr(dpy_Var* self)
{
    string res = "Var('";
    res += varcode_format(self->var.code());
    res += "', ";
    if (self->var.isset())
        switch (self->var.info()->type)
        {
            case Vartype::String:
            case Vartype::Binary:
                res += "'" + self->var.format() + "'";
                break;
            case Vartype::Integer:
            case Vartype::Decimal:
                res += self->var.format();
                break;
        }
    else
        res += "None";
    res += ")";
    return PyUnicode_FromString(res.c_str());
}

static PyObject* dpy_Var_richcompare(dpy_Var* a, dpy_Var* b, int op)
{
    PyObject *result;
    bool cmp;

    // Make sure both arguments are Vars.
    if (!(dpy_Var_Check(a) && dpy_Var_Check(b))) {
        result = Py_NotImplemented;
        goto out;
    }

    switch (op) {
        case Py_EQ: cmp = a->var == b->var; break;
        case Py_NE: cmp = a->var != b->var; break;
        default:
            result = Py_NotImplemented;
            goto out;
    }
    result = cmp ? Py_True : Py_False;

out:
    Py_INCREF(result);
    return result;
}


PyTypeObject dpy_Var_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "wreport.Var",              // tp_name
    sizeof(dpy_Var),           // tp_basicsize
    0,                         // tp_itemsize
    (destructor)dpy_Var_dealloc, // tp_dealloc
    0,                         // tp_print
    0,                         // tp_getattr
    0,                         // tp_setattr
    0,                         // tp_compare
    (reprfunc)dpy_Var_repr,    // tp_repr
    0,                         // tp_as_number
    0,                         // tp_as_sequence
    0,                         // tp_as_mapping
    0,                         // tp_hash
    0,                         // tp_call
    (reprfunc)dpy_Var_str,     // tp_str
    0,                         // tp_getattro
    0,                         // tp_setattro
    0,                         // tp_as_buffer
    Py_TPFLAGS_DEFAULT,        // tp_flags
    R"(
        Var holds a measured value, which can be integer, float or string, and
        a `wreport.Varinfo`_ with all available information (description, unit,
        precision, ...) related to it.

        Var objects can be created from a `wreport.Varinfo`_ object, and an
        optional value. Omitting the value creates an unset variable.

        Examples::

            v = wreport.Var(table["B12101"], 32.5)
            # v.info returns detailed informations about the variable in a Varinfo object.
            print("%s: %s %s %s" % (v.code, str(v), v.info.unit, v.info.desc))
    )",                        // tp_doc
    0,                         // tp_traverse
    0,                         // tp_clear
    (richcmpfunc)dpy_Var_richcompare, // tp_richcompare
    0,                         // tp_weaklistoffset
    0,                         // tp_iter
    0,                         // tp_iternext
    dpy_Var_methods,           // tp_methods
    0,                         // tp_members
    dpy_Var_getsetters,        // tp_getset
    0,                         // tp_base
    0,                         // tp_dict
    0,                         // tp_descr_get
    0,                         // tp_descr_set
    0,                         // tp_dictoffset
    (initproc)dpy_Var_init,    // tp_init
    0,                         // tp_alloc
    0,                         // tp_new
};

}

namespace wreport {
namespace python {

PyObject* var_value_to_python(const wreport::Var& v)
{
    try {
        switch (v.info()->type)
        {
            case Vartype::String:
                return PyUnicode_FromString(v.enqc());
            case Vartype::Binary:
                return PyBytes_FromString(v.enqc());
            case Vartype::Integer:
                return PyInt_FromLong(v.enqi());
            case Vartype::Decimal:
                return PyFloat_FromDouble(v.enqd());
        }
        Py_RETURN_TRUE;
    } WREPORT_CATCH_RETURN_PYO
}

int var_value_from_python(PyObject* o, wreport::Var& var)
{
    try {
        if (PyInt_Check(o))
        {
            var.seti(PyInt_AsLong(o));
        } else if (PyFloat_Check(o)) {
            var.setd(PyFloat_AsDouble(o));
        } else if (PyBytes_Check(o)) {
            var.setc(PyBytes_AsString(o));
        } else if (PyUnicode_Check(o)) {
            string val;
            if (string_from_python(o, val))
                return -1;
            var.sets(val);
        } else {
            string repr;
            if (object_repr(o, repr))
                return -1;
            string type_repr;
            if (object_repr((PyObject*)o->ob_type, type_repr))
                return -1;
            string errmsg = "Value " + repr + " must be an instance of int, long, float, str, bytes, or unicode, instead of " + type_repr;
            PyErr_SetString(PyExc_TypeError, errmsg.c_str());
            return -1;
        }
        return 0;
    } WREPORT_CATCH_RETURN_INT
}

dpy_Var* var_create(const wreport::Varinfo& v)
{
    dpy_Var* result = PyObject_New(dpy_Var, &dpy_Var_Type);
    if (!result) return NULL;
    new (&result->var) Var(v);
    return result;
}

dpy_Var* var_create(const wreport::Varinfo& v, int val)
{
    dpy_Var* result = PyObject_New(dpy_Var, &dpy_Var_Type);
    if (!result) return NULL;
    new (&result->var) Var(v, val);
    return result;
}

dpy_Var* var_create(const wreport::Varinfo& v, double val)
{
    dpy_Var* result = PyObject_New(dpy_Var, &dpy_Var_Type);
    if (!result) return NULL;
    new (&result->var) Var(v, val);
    return result;
}

dpy_Var* var_create(const wreport::Varinfo& v, const char* val)
{
    dpy_Var* result = PyObject_New(dpy_Var, &dpy_Var_Type);
    if (!result) return NULL;
    new (&result->var) Var(v, val);
    return result;
}

dpy_Var* var_create(const wreport::Var& v)
{
    dpy_Var* result = PyObject_New(dpy_Var, &dpy_Var_Type);
    if (!result) return NULL;
    new (&result->var) Var(v);
    return result;
}

void register_var(PyObject* m)
{
    dummy_var.set_bufr(0, "Invalid variable", "?", 0, 1, 0, 1);

    dpy_Var_Type.tp_new = PyType_GenericNew;
    if (PyType_Ready(&dpy_Var_Type) < 0)
        return;

    Py_INCREF(&dpy_Var_Type);
    PyModule_AddObject(m, "Var", (PyObject*)&dpy_Var_Type);
}

}
}

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <upcxx/upcxx.hpp>
#include "../util.hpp"
#include <dlfcn.h>

#define STR(s) #s
#define XSTR(s) STR(s)

void upcxx_test2();

extern "C"
{

static PyObject*
upcxx_py_test(PyObject *self, PyObject *args)
{
  upcxx_test2();
  Py_RETURN_NONE;
}

static PyObject*
upcxx_py_finalize(PyObject *self, PyObject *args)
{
  upcxx::finalize();
  Py_RETURN_NONE;
}

static PyMethodDef upcxxtest_methods[] = {
    {"test",  upcxx_py_test, METH_VARARGS,
     "Test UPC++ use in Python module."},
    {"finalize",  upcxx_py_finalize, METH_VARARGS,
     "Calls upcxx::finalize()."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef upcxxtest_mod = {
    PyModuleDef_HEAD_INIT,
    "upxxtest", /* name of module */
    NULL,       /* module documentation, may be NULL */
    -1,         /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
    upcxxtest_methods
};

PyMODINIT_FUNC
PyInit_upcxxpytest(void)
{
  upcxx::init();
  return PyModule_Create(&upcxxtest_mod);
}

}

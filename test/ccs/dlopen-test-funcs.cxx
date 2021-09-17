#include <cstdio>

extern "C" {
int dlopen_function() { return 3; }
void dlopen_function2() {}
}

int dlopen_cpp_function() { return 4; }

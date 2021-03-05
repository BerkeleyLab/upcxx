#ifndef _881e6514_0d2d_4981_a00b_da28e7b913ae
#define _881e6514_0d2d_4981_a00b_da28e7b913ae

#define UPCXX_CONCAT_(a, b) a ## b
#define UPCXX_CONCAT(a, b) UPCXX_CONCAT_(a, b)

// Macro for members that are intended to be private.
#define UPCXX_INTERNAL_ONLY(name) UPCXX_CONCAT(private_do_not_use_, name)

#endif

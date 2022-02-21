// Simple UPC++ program that calls UPC
#include <iostream>

#include <upcxx/upcxx.hpp>

#include "test.h"

int main(int argc, char **argv) {

  bupc_init(&argc, &argv); // optional, but recommended

  int val = 42;
  if (argc > 1) val = atoi(argv[1]);

  upcxx::init();

  int rank_me = upcxx::rank_me();

  for (int i=0; i < 5; i++) {
    val++;

    upcxx::barrier();

    int v1 = test_upc(val); // call a UPC test routine
    UPCXX_ASSERT_ALWAYS(v1 == val);

    upcxx::barrier();

    int v2 = test_upcxx(val); // call a UPC++ test routine
    UPCXX_ASSERT_ALWAYS(v2 == val);

  }

  upcxx::barrier();
  if (!rank_me) std::cout << "SUCCESS" << std::endl;

  upcxx::finalize();

  return 0;
}

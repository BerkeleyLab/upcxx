#include <iostream>
#include <upcxx/upcxx.hpp>
#include <cassert>

using namespace std;

int main(int argc, char *argv[])
{
  // setup UPC++ runtime
  upcxx::init();

//SNIPPET
std::vector<int> buffer;
size_t elem_count{};
if (upcxx::rank_me() == 0) {
  elem_count = 42; // initially only rank 0 knows the element count
}
// launch a scalar broadcast of element count from rank 0
upcxx::future<size_t> fut = upcxx::broadcast(elem_count, 0);

// do some overlapped work like preparing buffer for next broadcast
if (upcxx::rank_me() == 0) {
  buffer.reserve(elem_count);
  for (size_t i = 0; i < elem_count; i++)
    buffer[i] = (int)i;
}

elem_count = fut.wait(); // complete first broadcast
buffer.reserve(elem_count); // non-zero ranks allocate vector space

// launch a bulk broadcast of element data from rank 0
upcxx::future<> fut_bulk = upcxx::broadcast( buffer.data(), elem_count, 0); 

// wait until the second broadcast is complete
fut_bulk.wait();

// consume elements
for (size_t i = 0; i < buffer.size(); i++)
  UPCXX_ASSERT_ALWAYS(buffer[i] == (int)i);
//SNIPPET


  upcxx::barrier();
  if (!upcxx::rank_me()) cout << "SUCCESS" << endl;
  // close down UPC++ runtime
  upcxx::finalize();
  return 0;
} 

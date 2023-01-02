#include <iostream>
#include <upcxx/upcxx.hpp>
#include <cassert>

using namespace std;

int main(int argc, char *argv[])
{
  // setup UPC++ runtime
  upcxx::init();

//SNIPPET

int my_rank = upcxx::rank_me();
// launch a first broadcast operation from rank 0
upcxx::future<int> fut = upcxx::broadcast(my_rank, 0);

// do some overlapped work like preparing a buffer for another broadcast
std::vector<int> buffer(10);
if ( upcxx::rank_me() == 0 )
  for (int i = 0; i< buffer.size(); i++)
    buffer[i] = i;

// launch a second broadcast operation from rank 0
upcxx::future<> fut_bulk = upcxx::broadcast( buffer.data(), buffer.size(), 0); 

// wait for the result from the first broadcast
int bcast_rank = fut.wait();
assert(bcast_rank == 0);

// wait until the second broadcast is complete
fut_bulk.wait();
for (int i = 0; i< buffer.size(); i++)
  assert(buffer[i] == i);

//SNIPPET

  upcxx::barrier();
  if (!upcxx::rank_me()) cout << "SUCCESS" << endl;
  // close down UPC++ runtime
  upcxx::finalize();
  return 0;
} 

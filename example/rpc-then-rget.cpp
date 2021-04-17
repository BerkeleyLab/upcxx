// This example shows how one can send an RPC where the callback pulls data to
// the target using RMA get and schedules a completion to process the data.
// This is one way to send large data along with an RPC that ensures minimal
// copying of the data payload; on an RDMA-capable network the payload will flow
// directly from the source to the destination with zero intermediate copies.
// This pattern relies on the source memory residing in the shared heap,
// but the destination can be in shared or private heap (latter shown here).

#include <upcxx/upcxx.hpp>
#include <iostream>

using namespace upcxx;

int main() {
  upcxx::init();
 
  if (rank_me() == 0) {
    // stash some data in shared memory on this rank:
    global_ptr<char> shared_buffer = new_array<char>(1024);
    std::strcpy(shared_buffer.local(), "Welcome to the jungle, baby!");

    int target = (rank_me() + 1) % rank_n();
    std::cout << "Rank 0 sending the RPC to Rank " << target << std::endl;
    future<> f = rpc(target, [](global_ptr<char> src, size_t sz) {
      char *my_priv_buf = new char[sz]; // allocate some private memory as destination

      std::cout << "Rank " << rank_me() << " got the RPC, starting rget" << std::endl;
      // start async get from initiator's shared buf to target's private buffer:
      future<> fg = upcxx::rget(src, my_priv_buf, sz); 

      // schedule a local completion on the rget:
      future<> done = fg.then([=]() {
        std::cout << "Rank " << rank_me() << " completed the rget. "
                  << "The secret message is: " << my_priv_buf << std::endl;
        delete [] my_priv_buf;
      });

      // return the future to delay the RPC acknowledgment until the rget completes:
      return done;
    }, 
    shared_buffer, strlen(shared_buffer.local())+1); // rpc arguments

    f.wait(); // wait for RPC acknowledgment
    std::cout << "Rank 0 completed the operation." << std::endl;
    delete_array(shared_buffer); // wait above guarantees the source is safe to free
  }

  barrier();

  if (!rank_me()) std::cout<<"SUCCESS"<<std::endl;
  upcxx::finalize();
  return 0;
}

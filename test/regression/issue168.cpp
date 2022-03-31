#include <upcxx/upcxx.hpp>
#include <thread>
#include "../util.hpp"

int main (int argc, char ** argv) {
  upcxx::init();
  print_test_header();

  upcxx::persona lpc_persona;
  int done = 0;

  auto t1 = std::thread(
      [&lpc_persona,&done](){
        upcxx::intrank_t nghb = ( upcxx::rank_me() + 1 ) % upcxx::rank_n();
        upcxx::intrank_t sender = upcxx::rank_me();
        upcxx::rpc(nghb,upcxx::operation_cx::as_lpc(lpc_persona,[nghb,sender,&done](){
              /*Body of LPC*/
              assert(sender == upcxx::rank_me() );
              std::stringstream ss; 
              ss<<"This is the LPC executing on "<<upcxx::rank_me()<<" and tracking RPC executing on "<<nghb<<"\n";
              std::cout<<ss.str()<<std::flush;
              done = 1;
              }), 
            [sender,nghb](){
              /*body of RPC*/
              assert(nghb == upcxx::rank_me() );
              std::stringstream ss; 
              ss<<"This is the RPC executing on "<<upcxx::rank_me()<<" and issued by "<<sender<<"\n";
              std::cout<<ss.str()<<std::flush;
            }); 
        upcxx::discharge();
        assert(!upcxx::progress_required());
        #if STALL
          while (!done) upcxx::progress();
        #endif
      }   
  );  

  {
    upcxx::persona_scope ps(lpc_persona);
    while (!done) upcxx::progress();
  }

  upcxx::barrier();

  t1.join();

  print_test_success();
  upcxx::finalize();
  return 0;
}

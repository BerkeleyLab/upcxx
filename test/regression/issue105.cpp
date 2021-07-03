#include <upcxx/upcxx.hpp>
#include <iostream>
#include <unistd.h>
#include "../util.hpp"

using namespace std;

int main() {
  upcxx::init();

  #if UPCXX_CODEMODE
  if(upcxx::rank_me() == 0) {
    say("")<<"This test will likely deadlock. Build it in debug codemode "
             "so that it asserts before deadlocking.";
  }
  #endif
  
  UPCXX_ASSERT_ALWAYS(upcxx::rank_n() % 2 == 0);
  say()<<"sending outermost RPC.";
  upcxx::rpc(upcxx::rank_me() ^ 1,[]() {
    say()<<"in outermost RPC";
    sleep(1);
    say()<<"sending inner RPC";
    auto f = upcxx::rpc(upcxx::rank_me() ^ 1,[]() {
      say()<<"in inner RPC";
    });
    say()<<"waiting for inner RPC";
    f.wait(); // deadlock here
    say()<<"something else";
  }).wait();

  say()<<"done";

  upcxx::finalize();
  return 0;
}

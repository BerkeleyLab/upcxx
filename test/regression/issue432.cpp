#include <thread>
#include <unistd.h>

#include <upcxx/upcxx.hpp>
#include "../util.hpp"

#ifndef DEVICE
#error This test requires GPU support
#endif

#if !UPCXX_THREADMODE
  #error This test may only be compiled in PAR threadmode
#endif

using namespace upcxx;

int done = 0;

int main(int argc, char *argv[]) {
  upcxx::init();
  print_test_header();

  int me = upcxx::rank_me();
  int ranks = upcxx::rank_n();
  int peer = (me + 1)%ranks;
{
  #define SZ 1024
  Device gpu(0);
  device_allocator<Device> da(gpu,10*SZ);
  using gp_d = global_ptr<char,Device::kind>;
  gp_d gp = da.allocate<char>(SZ*2);
  gp_d gp2 = gp+SZ;
  global_ptr<char> sp = upcxx::new_array<char>(SZ);
  char *lp = new char[SZ];

  dist_object<gp_d> all_gps(gp);
  gp_d peer_gp = all_gps.fetch(peer).wait();
  gp_d peer_gp2 = peer_gp + SZ;
  upcxx::barrier();
#define SAY(stuff)  do { say() << stuff << ", line=" << __LINE__; } while (0)
#if 1
{ // --------------------------------------------------
  SAY("test: get(remote GPU -> private host, remote_cx::as_rpc)");

  std::thread th1{ [&]() {
    SAY("launching get..");
    upcxx::copy<char>(peer_gp, lp, SZ, 
      remote_cx::as_rpc([]() {
        SAY("remote CX");
        done = 1;
      })
    );
    upcxx::discharge();
    SAY("discharge complete");
  }};
  while (!done) upcxx::progress();
  SAY("wait done");
  done = 0;
    
  th1.join();
  SAY("thread joined");

  upcxx::barrier();
}
#endif
#if 1
{ // --------------------------------------------------
  SAY("test: get(remote GPU -> private host, operation_cx::as_lpc)");

  std::thread th1{ [&]() {
    SAY("launching get");
    upcxx::copy<char>(peer_gp, lp, SZ, 
      operation_cx::as_lpc(upcxx::master_persona(), []() {
        SAY("lpc CX");
        done = 1;
      })
    );
    upcxx::discharge();
    SAY("discharge complete");
  }};
  while (!done) upcxx::progress();
  SAY("wait done");
  done = 0;
    
  th1.join();
  SAY("thread joined");

  upcxx::barrier();
}
#endif
#if 1
{ // --------------------------------------------------
  SAY("test: copy(remote GPU -> remote GPU, remote_cx::as_rpc)");

  std::thread th1{ [&]() {
    persona *p = new persona();
    { persona_scope ps(*p);
    SAY("launching copy");
    upcxx::copy<char>(peer_gp, peer_gp2, SZ, 
      remote_cx::as_rpc([]() {
        SAY("remote CX");
        done = 1;
      })
    );
    upcxx::discharge();
    } // persona_scope
    delete p;
    SAY("discharge complete");
  }};
  while (!done) upcxx::progress();
  SAY("wait done");
  done = 0;
    
  th1.join();
  SAY("thread joined");

  upcxx::barrier();
}
#endif
#if 1
{ // --------------------------------------------------
  SAY("test: copy(local GPU -> local GPU, remote_cx::as_rpc)");

  std::thread th1{ [&]() {
    persona *p = new persona();
    { persona_scope ps(*p);
    SAY("launching copy");
    upcxx::copy<char>(gp, gp2, SZ, 
      remote_cx::as_rpc([]() {
        SAY("remote CX");
        done = 1;
      })
    );
    upcxx::discharge();
    } // persona_scope
    delete p;
    SAY("discharge complete");
  }};
  while (!done) upcxx::progress();
  SAY("wait done");
  done = 0;
    
  th1.join();
  SAY("thread joined");

  upcxx::barrier();
}
#endif
#if 1
{ // --------------------------------------------------
  SAY("test: copy(local shared -> remote GPU, remote_cx::as_rpc)");

  std::thread th1{ [&]() {
    persona *p = new persona();
    { persona_scope ps(*p);
    SAY("launching copy");
    upcxx::copy<char>(sp, peer_gp, SZ, 
      remote_cx::as_rpc([]() {
        SAY("remote CX");
        done = 1;
      })
    );
    upcxx::discharge();
    } // persona_scope
    delete p;
    SAY("discharge complete");
  }};
  while (!done) upcxx::progress();
  SAY("wait done");
  done = 0;
    
  th1.join();
  SAY("thread joined");

  upcxx::barrier();
}
#endif
#if 1
{ // --------------------------------------------------
  SAY("test: copy(local priv -> remote GPU, remote_cx::as_rpc)");

  std::thread th1{ [&]() {
    persona *p = new persona();
    { persona_scope ps(*p);
    SAY("launching copy");
    upcxx::copy<char>(lp, peer_gp, SZ, 
      remote_cx::as_rpc([]() {
        SAY("remote CX");
        done = 1;
      })
    );
    upcxx::discharge();
    } // persona_scope
    delete p;
    SAY("discharge complete");
  }};
  while (!done) upcxx::progress();
  SAY("wait done");
  done = 0;
    
  th1.join();
  SAY("thread joined");

  upcxx::barrier();
}
#endif
#if 1
{ // --------------------------------------------------
  SAY("test: copy(local priv -> remote GPU, remote_cx::as_rpc|source_cx::as_future)");

  std::thread th1{ [&]() {
    persona *p = new persona();
    { persona_scope ps(*p);
    SAY("launching copy");
    upcxx::copy<char>(lp, peer_gp, SZ, 
      remote_cx::as_rpc([]() {
        SAY("remote CX");
        done = 1;
      })
      | source_cx::as_future()
    ).wait();
    upcxx::discharge();
    } // persona_scope
    delete p;
    SAY("discharge complete");
  }};
  while (!done) upcxx::progress();
  SAY("wait done");
  done = 0;
    
  th1.join();
  SAY("thread joined");

  upcxx::barrier();
}
#endif
// --------------------------------------------------

  sleep(1); // ensure we've drained internal AMs
  upcxx::progress();
  upcxx::barrier();

  // cleanup
  delete [] lp;
  upcxx::delete_array(sp);
  da.deallocate(gp);
  gpu.destroy();
} // ensure all objects are destroyed before finalize, to improve diagnostics
    
  UPCXX_ASSERT_ALWAYS(&upcxx::current_persona() == &upcxx::master_persona());
  print_test_success();
  
  upcxx::finalize();
  return 0;
}

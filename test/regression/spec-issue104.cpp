#include <upcxx/upcxx.hpp>
#include <iostream>
#include <assert.h>
#include <unistd.h>
#include <string>
#include <memory>

using namespace upcxx;


int main() {
  upcxx::init();

  if (!rank_me()) std::cout << "Starting..." << std::endl;

  global_ptr<int> gp = rank_me() ? nullptr : new_<int>(42);
  gp = broadcast(gp, 0).wait();
  static int myval = rank_me();
  int peer = (rank_me()+1)%rank_n();
  barrier();

  // simulation of tutorial ex4 DHT find() (drmap.hpp)
  future<int, int> f =
  rpc(peer, [=](global_ptr<int> g) -> future<int, int> {
    return when_all(rget(g), myval);
  }, gp);
  f.wait();
  assert(f.result<0>() == 42);
  assert(f.result<1>() == peer);

  // an idiomatic future chain with an empty future and separate support variables named in the loop
  future<> fchain = when_all();
  int accum = 0;
  for (int i=0; i<5; i++) {
    fchain = when_all(fchain, 
                      rget(gp).then([&accum](int v) { accum += v; }));
  }
  fchain.wait();
  assert(accum == 42*5);
  accum = 0;

  // an equivalent communication keeping the relevant references inside in the future chain
  //future<global_ptr<int>,int &> chainP = when_all<global_ptr<int>&,int &>(gp, accum); // broken due to issue #396
  future<global_ptr<int>,int &> chainP = when_all(gp, make_future<int&>(accum));
  {
     future<> tmpf = when_all();
     for (int i=0; i<5; i++) {
       tmpf = when_all(tmpf, 
                      rget(chainP.result<0>()).then([chainP](int v) { 
                         chainP.result<1>() += v; 
                      }));
     }
     chainP = when_all(chainP, tmpf);
  }
  chainP.wait();
  assert(accum == 42*5);

  // misc tests
  int v = 0;
  future<int> bar = when_all(7);
  assert(bar.is_ready());
  assert(bar.result() == 7);
  future<int> foo = when_all(v); v++;
  assert(foo.is_ready());
  assert(foo.result() == 0);
  std::string s("s");
  future<std::string> baz = when_all(std::move(s));
  assert(baz.is_ready());
  assert(baz.result() == "s");
  std::unique_ptr<int> up(new int(17));
  future<std::unique_ptr<int>> boof = when_all(std::move(up));
  assert(boof.is_ready());
  assert(*boof.result_reference() == 17);

  barrier();
  if (!rank_me()) {
    delete_(gp);
    std::cout << "SUCCESS" << std::endl;
  }

  upcxx::finalize();
}

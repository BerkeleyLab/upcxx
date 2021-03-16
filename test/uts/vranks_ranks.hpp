#ifndef _9d56dedd_fa3c_4193_bb74_ed270f331282
#define _9d56dedd_fa3c_4193_bb74_ed270f331282

#include <upcxx/upcxx.hpp>
#include "../util.hpp"

#define VRANKS_IMPL "ranks"
#define VRANK_LOCAL /*empty*/

namespace vranks {
  template<typename Fn>
  void send(int vrank, Fn msg) {
    upcxx::rpc_ff(vrank, std::move(msg));
  }
  
  inline void progress() {
    upcxx::progress();
  }
  
  template<typename Fn>
  void spawn(Fn fn) {
    upcxx::init();
    
    fn(upcxx::rank_me(), upcxx::rank_n());
    
    upcxx::finalize();
  }
}
#endif

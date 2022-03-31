#include <upcxx/backend/gasnet/noise_log.hpp>
#include <upcxx/backend/gasnet/runtime_internal.hpp>

using upcxx::backend::gasnet::noise_log;

void noise_log::show() {
  if(fn_name == nullptr)
    return; // disabled
  
  struct reduced {
    unsigned rank_least;
    int rank_n;
  };
  reduced r;
  r.rank_least = empty ? ~0u : upcxx::rank_me();
  r.rank_n = empty ? 0 : 1;

  gex_Event_Wait(gex_Coll_ReduceToAllNB(
    gasnet::handle_of(upcxx::world()),
    &r, &r,
    GEX_DT_USER, sizeof(reduced), 1,
    GEX_OP_USER,
    (gex_Coll_ReduceFn_t)[](const void *arg1, void *arg2_out, std::size_t n, const void*) {
      reduced const *in = (reduced const*)arg1;
      reduced *acc = (reduced*)arg2_out;
      UPCXX_ASSERT(n >= 1);
      while (n--) {
        acc->rank_least = std::min(acc->rank_least, in->rank_least);
        acc->rank_n += in->rank_n;
        acc++; in++;
      }
    },
    nullptr, 0
  ));
  
  if((unsigned)upcxx::rank_me() == r.rank_least) {
    std::stringstream ss1;
    ss1 << std::string(50,'/') << '\n';
    ss1 << fn_name;
    if(r.rank_n != upcxx::rank_n())
      ss1 << " on rank "<<r.rank_least<<" (among "<<r.rank_n<<" total):\n";
    else
      ss1 << ":\n";
    ss1 << ss.str();
    ss1 << std::string(50,'/') << '\n';
    std::cerr << ss1.str() << std::flush;
  }

  reset();
}

std::string noise_log::size(std::size_t x) {
  char buf[80];
  return std::string(gasnett_format_number(x, buf, sizeof(buf), 1));
}

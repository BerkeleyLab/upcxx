#include <upcxx/upcxx.hpp>
#include <iostream>
#include "../util.hpp"


using namespace std;
using namespace upcxx;

bool done1, done2;
int local_buffer;

void check(std::string context) {
  bool req1 = progress_required();
  say() << context << ": progress " << (req1?"IS":"IS NOT") << " required";

  { // consistency checks for progress_required argument:
    persona dummy;
    persona_scope ps(dummy);

    bool req2 = progress_required(top_persona_scope());
    UPCXX_ASSERT_ALWAYS(req2 == false);

    bool req3 = progress_required(default_persona_scope());
    UPCXX_ASSERT_ALWAYS(req3 == req1);

    bool req4 = progress_required(); // defaulted scope argument is ...
    #if UPCXX_VERSION < 20230303
      UPCXX_ASSERT_ALWAYS(req4 == false); // only current_persona
    #else
      UPCXX_ASSERT_ALWAYS(req4 == req1); // all personas
    #endif
  } // pop dummy persona

  bool req5 = progress_required();
  UPCXX_ASSERT_ALWAYS(req5 == req1);
}

int main() {
  upcxx::init();
  print_test_header();

  if (local_team().rank_n() == rank_n()) {
    print_test_skipped("This test requires multiple nodes");
    upcxx::finalize();
    return 0;
  }
  // select a unique peer that is likely to reside on a different local_team
  int peer = (rank_me() + rank_n()/2) % rank_n();

  auto gptr = new_<int>();
  dist_object<global_ptr<int>> dobj(gptr);
  auto peer_gptr = dobj.fetch(peer).wait();
  barrier();
  check("idle");
  UPCXX_ASSERT_ALWAYS(!progress_required());
  barrier();

  rpc(peer,[=](){
      // activates internal function detail::copy_as_rget:
      copy(gptr, &local_buffer, 1, remote_cx::as_rpc([](){ done1 = true; }));
      // It's deliberately unspecified whether progress is required to complete the above.
      // We are leveraging internal knowledge for this demonstration, 
      // which (with high likelihood) WILL require further progress

      check("inside RPC");
  }).wait();

  check("after outer RPC completion");
  while(!done1)upcxx::progress();
  UPCXX_ASSERT_ALWAYS(!progress_required());
  barrier();

  copy(peer_gptr, &local_buffer, 1, remote_cx::as_rpc([](){ done2 = true; }));
  check("after second copy");
  discharge();
  check("after discharge");
  UPCXX_ASSERT_ALWAYS(!progress_required());
  UPCXX_ASSERT_ALWAYS(done2 == false);

  while(!done2)upcxx::progress();
  UPCXX_ASSERT_ALWAYS(!progress_required());
  barrier();

  delete_(gptr);

  print_test_success();
  upcxx::finalize();
}

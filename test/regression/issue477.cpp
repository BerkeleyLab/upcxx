#include <upcxx/upcxx.hpp>
#include <iostream>
#include <assert.h>
#include "../util.hpp"

using namespace upcxx;

static int done = 0;
static int errs = 0;

#define CHECK(condition, context) do { \
  if (!(condition)) { \
    say() << "ERROR: failed check " << #condition << " in copy " << context \
          << " at line " << __LINE__; \
    errs++; \
  } \
} while (0)

void do_rc(const char *ctx) {
  CHECK(&current_persona() == &master_persona(), ctx);
  #if UPCXX_SPEC_VERSION >= 20201000
   CHECK(upcxx::in_progress(), ctx);
  #endif
  done = 1;
}

int main() {
  upcxx::init();
  print_test_header();

  int peer = (rank_me()+1)%rank_n();
  global_ptr<int> gp = new_array<int>(2);
  dist_object<global_ptr<int>> dgp(gp);
  global_ptr<int> rgp = dgp.fetch(peer).wait();

  {
  #if UPCXX_THREADMODE
  persona p;
  persona_scope ps(p);
  #endif

  barrier();

  copy(gp, gp+1, 1, remote_cx::as_rpc([]() { do_rc("host-to-host loopback"); }));
  for (int i=0; i<1000; i++) progress(progress_level::internal);
  CHECK(!done, "host-to-host loopback"); // RC runs locally, but must be in user progress
  while (!done) progress();
  done = 0;

  barrier();

  copy(gp, rgp+1, 1, remote_cx::as_rpc([]() { do_rc("host-to-host put"); }));
  while (!done) progress();
  done = 0;

  barrier();

  copy(rgp, gp+1, 1, remote_cx::as_rpc([]() { do_rc("host-to-host get"); }));
  for (int i=0; i<1000; i++) progress(progress_level::internal);
  CHECK(!done, "host-to-host get"); // RC runs locally, but must be in user progress
  while (!done) progress();
  done = 0;

  barrier();
  }

  delete_array(gp);
  print_test_success(errs==0);
  upcxx::finalize();
  return 0;
}

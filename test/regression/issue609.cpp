#include <upcxx/upcxx.hpp>
#include <iostream>
#include "../util.hpp"

using namespace upcxx;

bool done;
bool speak;
int count = 0;

struct AvalFn {
 int _c;
 AvalFn(int c) : _c(c) {}
 AvalFn(const AvalFn&) = default; // copyable
 void operator()() {  // the common case, no ref qualifier
   if (speak) say() << "AvalFn ran, constructed on line " << _c; 
   done = true;
   count++;
 }
 UPCXX_SERIALIZED_VALUES(_c)
};

struct LvalFn {
 int _c;
 LvalFn(int c) : _c(c) {}
 LvalFn(const LvalFn&) = default; // copyable
 void operator()() const & { // const lval qualifier
   if (speak) say() << "LvalFn ran, constructed on line " << _c; 
   done = true;
   count++;
 }
 UPCXX_SERIALIZED_VALUES(_c)
};

struct RvalFn {
 int _c;
 RvalFn(int c) : _c(c) {}
 RvalFn(const RvalFn&) = default; // copyable
 void operator()() && { // only invocable on rvalue
   if (speak) say() << "RvalFn ran, constructed on line " << _c; 
   done = true;
   count++;
 }
 UPCXX_SERIALIZED_VALUES(_c)
};

struct NcAvalFn {
 int _c;
 NcAvalFn(int c) : _c(c) {}
 NcAvalFn(NcAvalFn&&) = default; 
 NcAvalFn(const NcAvalFn&) = delete; // non-copyable
 void operator()() {  // the common case, no ref qualifier
   if (speak) say() << "NcAvalFn ran, constructed on line " << _c; 
   done = true;
   count++;
 }
 UPCXX_SERIALIZED_VALUES(_c)
};

struct NcLvalFn {
 int _c;
 NcLvalFn(int c) : _c(c) {}
 NcLvalFn(NcLvalFn&&) = default; 
 NcLvalFn(const NcLvalFn&) = delete; // non-copyable
 void operator()() const & {  // const lval qualifier
   if (speak) say() << "NcLvalFn ran, constructed on line " << _c; 
   done = true;
   count++;
 }
 UPCXX_SERIALIZED_VALUES(_c)
};

struct NcRvalFn {
 int _c;
 NcRvalFn(int c) : _c(c) {}
 NcRvalFn(NcRvalFn&&) = default;
 NcRvalFn(const NcRvalFn&) = delete; // non-copyable
 void operator()() && { // only invocable on rvalue
   if (speak) say() << "NcRvalFn ran, constructed on line " << _c; 
   done = true;
   count++;
 }
 UPCXX_SERIALIZED_VALUES(_c)
};

int main() {
  upcxx::init();

  auto gp = new_<int>();

  speak = !rank_me();

  // future::then

  auto ready = make_future();
  ready.then(NcAvalFn(__LINE__)); 
  ready.then(NcLvalFn(__LINE__)); 
  ready.then(NcRvalFn(__LINE__)); 
  { AvalFn f(__LINE__); ready.then(f); }
  { LvalFn f(__LINE__); ready.then(f); }
  { RvalFn f(__LINE__); ready.then(f); }

  { promise<> p;
    p.get_future().then(NcAvalFn(__LINE__)); 
    p.finalize();
  }

  { promise<> p;
    p.get_future().then(NcLvalFn(__LINE__)); 
    p.finalize();
  }

  { promise<> p;
    p.get_future().then(NcRvalFn(__LINE__)); 
    p.finalize();
  }

  { promise<> p; AvalFn f(__LINE__);
    p.get_future().then(f); 
    p.finalize();
  }

  { promise<> p; LvalFn f(__LINE__);
    p.get_future().then(f); 
    p.finalize();
  }

  { promise<> p; RvalFn f(__LINE__);
    p.get_future().then(f); 
    p.finalize();
  }

  // lpc_ff

  done = false;
  current_persona().lpc_ff(NcAvalFn(__LINE__)); 
  do { progress(); } while (!done);
  
  done = false;
  current_persona().lpc_ff(NcLvalFn(__LINE__)); 
  do { progress(); } while (!done);
  
  done = false;
  current_persona().lpc_ff(NcRvalFn(__LINE__)); 
  do { progress(); } while (!done);
  
  { AvalFn f(__LINE__);
    done = false;
    current_persona().lpc_ff(f); 
    do { progress(); } while (!done);
  }
  
  { LvalFn f(__LINE__);
    done = false;
    current_persona().lpc_ff(f); 
    do { progress(); } while (!done);
  }
  
  { RvalFn f(__LINE__);
    done = false;
    current_persona().lpc_ff(f); 
    do { progress(); } while (!done);
  }
  
  // lpc
  current_persona().lpc(NcAvalFn(__LINE__)).wait(); 

  { AvalFn f(__LINE__);
    current_persona().lpc(f).wait(); 
  }
 
  current_persona().lpc(NcLvalFn(__LINE__)).wait(); 

  { LvalFn f(__LINE__);
    current_persona().lpc(f).wait(); 
  }
 
 #if 1 // issue #609: previously these cases did not compile

  current_persona().lpc(NcRvalFn(__LINE__)).wait(); 

  { RvalFn f(__LINE__);
    current_persona().lpc(f).wait(); 
  }
 
 #endif

  // rpc_ff

  done = false;
  rpc_ff(rank_me(),NcAvalFn(__LINE__)); 
  do { progress(); } while (!done);
  
  done = false;
  rpc_ff(rank_me(),NcLvalFn(__LINE__)); 
  do { progress(); } while (!done);
  
  done = false;
  rpc_ff(rank_me(),NcRvalFn(__LINE__)); 
  do { progress(); } while (!done);

  { AvalFn f(__LINE__);
    done = false;
    rpc_ff(rank_me(),f); 
    do { progress(); } while (!done);
  }

  { LvalFn f(__LINE__);
    done = false;
    rpc_ff(rank_me(),f); 
    do { progress(); } while (!done);
  }

  { RvalFn f(__LINE__);
    done = false;
    rpc_ff(rank_me(),f); 
    do { progress(); } while (!done);
  }

  // rpc
  
  rpc(rank_me(),NcAvalFn(__LINE__)).wait(); 
 
  rpc(rank_me(),NcLvalFn(__LINE__)).wait(); 
 
  rpc(rank_me(),NcRvalFn(__LINE__)).wait(); 

  { AvalFn f(__LINE__); rpc(rank_me(),f).wait(); }

  { LvalFn f(__LINE__); rpc(rank_me(),f).wait(); }

  { RvalFn f(__LINE__); rpc(rank_me(),f).wait(); }

  // as_rpc : requires copyable function object

  done = false;
  rput(0, gp, remote_cx::as_rpc(AvalFn(__LINE__)));
  do { progress(); } while (!done);

  done = false;
  rput(0, gp, remote_cx::as_rpc(LvalFn(__LINE__)));
  do { progress(); } while (!done);

  done = false;
  rput(0, gp, remote_cx::as_rpc(RvalFn(__LINE__)));
  do { progress(); } while (!done);

  // as_lpc : requires copyable function object
  
  done = false;
  rput(0, gp, operation_cx::as_lpc(current_persona(),AvalFn(__LINE__)));
  do { progress(); } while (!done);

  done = false;
  rput(0, gp, operation_cx::as_lpc(current_persona(),LvalFn(__LINE__)));
  do { progress(); } while (!done);

  done = false;
  rput(0, gp, operation_cx::as_lpc(current_persona(),RvalFn(__LINE__)));
  do { progress(); } while (!done);

  if (speak) say() << "Final callback count: " << count;
  UPCXX_ASSERT_ALWAYS(count == 42);

  delete_(gp);

  print_test_success();

  upcxx::finalize();
  return 0;
}

#include <cstdint>
#include <cmath>
#include <iostream>

#ifndef VRANKS_IMPL
  #error "This is a partial source file. Please include one of the vranks_xxx.hpp headers then include this file."
#endif

using namespace std;

// WARNING: This is an "open-box" test that relies upon unspecified interfaces and/or
// behaviors of the UPC++ implementation that are subject to change or removal
// without notice. See "Unspecified Internals" in docs/implementation-defined.md
// for details, and consult the UPC++ Specification for guaranteed interfaces/behaviors.

using upcxx::detail::digest;

struct uts_node {
  uts_node *next;
  digest id;
  int depth;
};

VRANK_LOCAL int vrank_n, vrank_me;
VRANK_LOCAL uint64_t qd_lsend_n = 0;
VRANK_LOCAL uint64_t qd_lrecv_n = 0;

bool qd_progress(bool local_quiescence);

void uts_serial(uint64_t &tree_node_n, digest &tree_hash);
void uts_parallel(uint64_t &par_node_n, digest &par_hash);

void finish(uint64_t par_node_n, digest par_hash);

double uts_width;

int main() {
  vranks::spawn(
    [](int vrank_me1, int vrank_n1) {
      vrank_me = vrank_me1;
      vrank_n = vrank_n1;
      
      uts_width = os_env<double>("UTS_WIDTH", 100);
      if (!vrank_me) std::cout<<"UTS_WIDTH: " << uts_width << std::endl;

      uint64_t par_node_n;
      digest par_hash;
      
      uts_parallel(par_node_n, par_hash);
      finish(par_node_n, par_hash);
    }
  );
  
  return 0;
}

int uts_kid_n(uts_node nd) {
  int kid_n;
  
  if(nd.depth == 0)
    kid_n = 100;
  else {
    double expected = nd.depth < 5 ? std::pow(uts_width/100, 0.25) :
                      nd.depth < 30 ? 1.0 :
                      0.1;
    
    kid_n = (500 +
        nd.id.w0 % int(1000*expected) +
        nd.id.w1 % int(1000*expected)
      )/1000;
  }
  
  return kid_n;  
}

void uts_serial(uint64_t &tree_node_n, digest &tree_hash) {
  tree_node_n = 0;
  tree_hash = {0,0};
  
  std::vector<uts_node> work;
  work.push_back(uts_node{
    /*next*/nullptr,
    /*id*/digest{0xdeadbeef, 0xdeadbeef},
    /*depth*/0
  });
  
  while(!work.empty()) {
    uts_node nd = work.back();
    work.pop_back();
    
    // tree partition totals
    tree_node_n += 1;
    tree_hash.w0 ^= nd.id.w0;
    tree_hash.w1 ^= nd.id.w1;
    
    int kid_n = uts_kid_n(nd);
    
    for(int k=0; k != kid_n; k++) {
      digest kid_id = nd.id.eat(k, nd.depth);
      int kid_depth = nd.depth + 1;
      work.push_back(uts_node{nullptr, kid_id, kid_depth});
    }
  }
}

VRANK_LOCAL uts_node *par_work_head = nullptr;

void uts_parallel(uint64_t &par_node_n, digest &par_hash) {
  par_node_n = 0;
  par_hash = {0,0};
  
  par_work_head = vrank_me == 0
    ? new uts_node{
      /*next*/nullptr,
      /*id*/digest{0xdeadbeef, 0xdeadbeef},
      /*depth*/0
    }
    : nullptr;
  
  do {
    if(par_work_head != nullptr) {
      // pop one node
      uts_node *nd = par_work_head;
      par_work_head = nd->next;
      
      // tree partition totals
      par_node_n += 1;
      par_hash.w0 ^= nd->id.w0;
      par_hash.w1 ^= nd->id.w1;
      
      int kid_n = uts_kid_n(*nd);
      
      for(int k=0; k != kid_n; k++) {
        digest kid_id = nd->id.eat(k, nd->depth);
        int kid_depth = nd->depth + 1;
        int kid_vrank = kid_id.w1 % vrank_n;
        
        qd_lsend_n += 1;
        vranks::send(kid_vrank, [=]() {
          qd_lrecv_n += 1;
          par_work_head = new uts_node{par_work_head, kid_id, kid_depth};
        });
      }
      
      delete nd;
    }
  }
  while(!qd_progress(par_work_head == nullptr));
}

VRANK_LOCAL uint64_t fin_node_n = 0;
VRANK_LOCAL digest fin_hash = digest::zero();
VRANK_LOCAL int fin_counter = 0;

void finish(uint64_t par_node_n, digest par_hash) {
  // reduce par_node_n & par_hash to vrank 0
  vranks::send(0, [=]() {
    fin_node_n += par_node_n;
    fin_hash.w0 ^= par_hash.w0;
    fin_hash.w1 ^= par_hash.w1;
    fin_counter += 1;
  });
  
  if(vrank_me == 0) {
    while(fin_counter != vrank_n)
      vranks::progress();
    
    // compare reduced tree measures to serial versions
    uint64_t ser_node_n;
    digest ser_hash;
    uts_serial(ser_node_n, ser_hash);
    
    bool success = ser_node_n == fin_node_n && ser_hash == fin_hash;
    
    std::cout<<"Tree size: "<<ser_node_n<<'\n';
    std::cout<<"Tree hash: "<<ser_hash<<'\n';
    if(!success) {
      std::cout<<"Wrong size: "<<fin_node_n<<'\n';
      std::cout<<"Wrong hash: "<<fin_hash<<'\n';
    }
    
    std::cout<<"Test result: "<<(success?"SUCCESS":"FAILURE")<<std::endl<<std::flush;
    UPCXX_ASSERT_ALWAYS(success);
  }
}

VRANK_LOCAL uint64_t qd_gsend_acc = 0;
VRANK_LOCAL uint64_t qd_grecv_acc = 0;
VRANK_LOCAL uint64_t qd_gsend_prev_n = 0;
VRANK_LOCAL int qd_tree_incoming = 0;

enum {
  qd_status_working,
  qd_status_reducing,
  qd_status_reduced_q,
  qd_status_reduced_notq
};
VRANK_LOCAL int qd_status = qd_status_working;

void qd_bcast(int status, int ub) {
  qd_status = status;
  
  while(true) {
    int mid = vrank_me + (ub - vrank_me)/2;
    if(mid == vrank_me) break;
    
    vranks::send(mid, [=]() {
      qd_bcast(status, ub);
    });
    
    ub = mid;
  }
}

void qd_reduce(uint64_t send_n, uint64_t recv_n, int from) {
  if(qd_tree_incoming == 0) {
    while(true) {
      int kid = vrank_me | (1<<qd_tree_incoming);
      if(kid == vrank_me || vrank_n <= kid)
        break;
      qd_tree_incoming += 1;
    }
    qd_tree_incoming += 1; // add one for self
  }
  
  qd_gsend_acc += send_n;
  qd_grecv_acc += recv_n;
  
  if(0 == --qd_tree_incoming) {
    uint64_t gsend_n = qd_gsend_acc;
    uint64_t grecv_n = qd_grecv_acc;
    qd_gsend_acc = 0;
    qd_grecv_acc = 0;
    
    if(vrank_me == 0) {
      bool quiescent =
        gsend_n == grecv_n &&
        gsend_n == qd_gsend_prev_n;
      
      qd_gsend_prev_n = gsend_n;
      
      int status = quiescent ? qd_status_reduced_q : qd_status_reduced_notq;
      qd_bcast(status, vrank_n);
    }
    else {
      int parent = vrank_me & (vrank_me-1);
      int from = vrank_me;
      vranks::send(parent, [=]() {
        qd_reduce(gsend_n, grecv_n, from);
      });
    }
  }
}

bool qd_progress(bool local_quiescence) {
  vranks::progress();
      
  switch(qd_status) {
  case qd_status_working:
    if(local_quiescence) {
      qd_status = qd_status_reducing;
      qd_reduce(qd_lsend_n, qd_lrecv_n, vrank_me);
    }
    return false;
  
  case qd_status_reducing:
    return false;
  
  case qd_status_reduced_q:
    UPCXX_ASSERT_ALWAYS(local_quiescence);
    qd_status = qd_status_working;
    return true;
  
  case qd_status_reduced_notq:
    qd_status = qd_status_working;
    return false;
  }
  
  return false;
}

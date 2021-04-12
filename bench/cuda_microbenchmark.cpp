#include <upcxx/upcxx.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace std;
using namespace upcxx;

bool run_gg = false;
bool run_sg = false;
bool run_gs = false;
bool run_ss = false;
bool run_ps = false;
bool run_pg = false;
bool use_downcast_self = false;
bool use_downcast_peer = false;
bool use_concise = false;
bool use_firstlast = false;

const char *Private() {
  if (use_downcast_self) return "Downcast-Self";
  else if (use_downcast_peer) return "Downcast-Peer";
  else return "Private";
}
std::string Priv() {
  if (use_downcast_self) return "DShS";
  else if (use_downcast_peer) return "DShP";
  else return "Priv";
}

static bool is_active_rank;
static long max_warmup, window_size, max_trials, max_volume;

long trials_for_size(long msg_sz) {
  if (!max_volume) return max_trials;
  else {
    long window_volume = window_size * msg_sz;
    if (!window_volume) return 1; // error in cmdline parsing
    long trial_cap = (max_volume + window_volume - 1) / window_volume;
    return std::min(trial_cap, max_trials);
  }
}

template<typename T>
intrank_t owner(T*) { return rank_me(); }
template<typename T, memory_kind K>
intrank_t owner(global_ptr<T,K> p) { return p.where(); }

enum class sync_type {
  blocking_op,
  flood_op,
  flood_remote
};

template<sync_type sync, typename src_ptr_type, typename dst_ptr_type>
static double helper(long len, src_ptr_type src_ptr, dst_ptr_type dst_ptr) {
    double elapsed = 0.0;
    long trials = trials_for_size(len);
    long warmup = std::min(max_warmup, trials);
    //if (!rank_me()) std::cout << len << ":" << trials << std::endl;


    if (sync == sync_type::flood_remote) {
      static promise<> data_arrival; 
      static promise<> ack;
      static int my_sender;  my_sender = -1;

      upcxx::barrier();
      if (is_active_rank) { // inform target ranks of their sender
        rpc(owner(dst_ptr), [](int me) { 
           assert(my_sender == -1); 
           my_sender = me;              // register sender
           data_arrival = promise<>();  // setup for first window
           data_arrival.require_anonymous(window_size);
        }, rank_me()).wait();
      }
      upcxx::barrier();

      std::chrono::steady_clock::time_point start;

      for (long i = 0; i < warmup+trials; i++) { // all ranks run trial loop
        if (i == warmup) { // done with warmup
          upcxx::barrier();
          start = std::chrono::steady_clock::now(); // begin timed region
        }

        if (is_active_rank) {
          static auto rem_cx = remote_cx::as_rpc([](){ data_arrival.fulfill_anonymous(1); });
          ack = promise<>(); // prepare for ack
          ack.require_anonymous(1);
          for (long j = 0; j < window_size; j++) { // send copies
            upcxx::copy(src_ptr, dst_ptr, len, rem_cx);
          }
        }
        if (my_sender >= 0) { // this process is a target
          data_arrival.finalize().wait(); // await arrival of all copies
          data_arrival = promise<>();     // reset for next window
          data_arrival.require_anonymous(window_size);
          rpc_ff(my_sender, []() { ack.fulfill_anonymous(1); }); // send ack
        }
        if (is_active_rank) {
          ack.finalize().wait(); // await acknowledgment
        }
      }

      upcxx::barrier(); // ensure timed region includes comms from all ranks

      std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();
      elapsed = std::chrono::duration<double>(end - start).count();

      return elapsed;

    } else { // blocking_op, flood_op

      upcxx::barrier();

      if (is_active_rank) {
        std::chrono::steady_clock::time_point start;

        for (long i = 0; i < warmup+trials; i++) {
            if (i == warmup) { // done with warmup
              upcxx::barrier();
              start = std::chrono::steady_clock::now(); // begin timed region
            }

            upcxx::promise<> prom;
            for (long j = 0; j < window_size; j++) {
                upcxx::copy(src_ptr, dst_ptr, len,
                        upcxx::operation_cx::as_promise(prom));
                if (sync == sync_type::blocking_op) {
                    prom.finalize().wait();
                    prom = upcxx::promise<>();
                }
            }
            prom.finalize().wait();
        }

        upcxx::barrier(); // ensure timed region includes comms from all ranks

        std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();
        elapsed = std::chrono::duration<double>(end - start).count();
      } else { upcxx::barrier(); upcxx::barrier(); } 

      return elapsed;
    }
}

static double local_gpu_to_remote_gpu, remote_gpu_to_local_gpu,
              local_shared_to_remote_gpu, remote_gpu_to_local_shared,
              local_gpu_to_remote_shared, remote_shared_to_local_gpu,
              local_shared_to_remote_shared, remote_shared_to_local_shared,
              local_private_to_remote_shared, remote_shared_to_local_private,
              local_private_to_remote_gpu, remote_gpu_to_local_private;

using gp_cuda_t = global_ptr<uint8_t, memory_kind::cuda_device>;
using gp_host_t = global_ptr<uint8_t, memory_kind::host>;
gp_cuda_t local_gpu_array;
gp_cuda_t remote_gpu_array;
gp_host_t local_shared_array;
gp_host_t remote_shared_array;
uint8_t *local_private_array;

template<sync_type sync>
static void run_all_copies(long msg_len) {

    if (run_gg) {
        local_gpu_to_remote_gpu =
            helper<sync>(msg_len, local_gpu_array, remote_gpu_array);
        remote_gpu_to_local_gpu =
            helper<sync>(msg_len, remote_gpu_array, local_gpu_array);
    }

    if (run_sg) {
        local_shared_to_remote_gpu =
            helper<sync>(
                    msg_len, local_shared_array, remote_gpu_array);
        remote_gpu_to_local_shared =
            helper<sync>(
                    msg_len, remote_gpu_array, local_shared_array);
    }

    if (run_gs) {
        local_gpu_to_remote_shared =
            helper<sync>(msg_len, local_gpu_array, remote_shared_array);
        remote_shared_to_local_gpu =
            helper<sync>(msg_len, remote_shared_array, local_gpu_array);
    }

    if (run_ss) {
        local_shared_to_remote_shared =
            helper<sync>(msg_len, local_shared_array, remote_shared_array);
        remote_shared_to_local_shared =
            helper<sync>(msg_len, remote_shared_array, local_shared_array);
    }

    if (run_ps) {
        local_private_to_remote_shared =
            helper<sync>(msg_len, local_private_array, remote_shared_array);
        remote_shared_to_local_private =
            helper<sync>(msg_len, remote_shared_array, local_private_array);
    }

    if (run_pg) {
        local_private_to_remote_gpu =
            helper<sync>(msg_len, local_private_array, remote_gpu_array);
        remote_gpu_to_local_private =
            helper<sync>(msg_len, remote_gpu_array, local_private_array);
    }
}

static void legend() {
  if (!use_concise || rank_me()) return;
  std::cout << "\n=== Output Legend ===\n" << std::endl;
  std::cout << "Copy-Size : Size of each copy() operation payload, in bytes" << std::endl;
  std::cout << "X->Y : Performance measured for copy from memory region X to memory region Y" << std::endl;
  std::cout << "LGpu : Local GPU memory (owned by this process)" << std::endl;
  std::cout << "RGpu : Remote GPU memory (owned by another process)" << std::endl;
  std::cout << "LSh  : Local Shared Host memory (owned by this process)" << std::endl;
  std::cout << "RSh  : Remote Shared Host memory (owned by another process)" << std::endl;
  if (use_downcast_self)
  std::cout << "DShS : Downcast Shared Host memory (owned by this process)" << std::endl;
  else if (use_downcast_peer)
  std::cout << "DShP : Downcast Shared Host memory (owned by a local_team peer)" << std::endl;
  else
  std::cout << "Priv : Private Host memory (owned by this process)" << std::endl;

}

static const char *desc;
static void test_header(const char *_desc) {
  desc = _desc;
  if (rank_me()) return;

  std::cout << "\n=== Testing " << desc << " ===\n" << std::endl;
  if (use_concise) {
      auto col = std::setw(12);
      std::cout << col << "Copy-Size";
      if (run_gg) std::cout << col << "LGpu->RGpu" << col << "RGpu->LGpu";
      if (run_sg) std::cout << col << "LSh->RGpu"  << col << "RGpu->LSh";
      if (run_gs) std::cout << col << "LGpu->RSh"  << col << "RSh->LGpu";
      if (run_ss) std::cout << col << "LSh->RSh"   << col << "RSh->LSh";
      if (run_ps) std::cout << col << Priv() + "->RSh"  << col << "RSh->" + Priv();
      if (run_pg) std::cout << col << Priv() + "->RGpu" << col << "RGpu->" + Priv();
      std::cout << std::endl;
      return;
  }
}

static void print_latency_results() {
    long nmsgs = trials_for_size(8)*window_size;
    double Mmsgs = double(nmsgs) / 1.0e6;

    if (use_concise) {
      auto col = std::setw(12);
      std::cout << col << 8;
      if (run_gg) std::cout << col << local_gpu_to_remote_gpu/Mmsgs << col << remote_gpu_to_local_gpu/Mmsgs;
      if (run_sg) std::cout << col << local_shared_to_remote_gpu/Mmsgs << col << remote_gpu_to_local_shared/Mmsgs;
      if (run_gs) std::cout << col << local_gpu_to_remote_shared/Mmsgs << col << remote_shared_to_local_gpu/Mmsgs;
      if (run_ss) std::cout << col << local_shared_to_remote_shared/Mmsgs << col << remote_shared_to_local_shared/Mmsgs;
      if (run_ps) std::cout << col << local_private_to_remote_shared/Mmsgs << col << remote_shared_to_local_private/Mmsgs;
      if (run_pg) std::cout << col << local_private_to_remote_gpu/Mmsgs << col << remote_gpu_to_local_private/Mmsgs;
      std::cout << std::endl;
      return;
    }

    if (run_gg) {
        std::cout << "  Local GPU -> Remote GPU: " <<
            (local_gpu_to_remote_gpu / Mmsgs) << " us" << std::endl;
        std::cout << "  Remote GPU -> Local GPU: " <<
            (remote_gpu_to_local_gpu / Mmsgs) << " us" << std::endl;
    }
    if (run_sg) {
        std::cout << "  Local Shared -> Remote GPU: " <<
            (local_shared_to_remote_gpu / Mmsgs) << " us" << std::endl;
        std::cout << "  Remote GPU -> Local Shared: " <<
            (remote_gpu_to_local_shared / Mmsgs) << " us" << std::endl;
    }
    if (run_gs) {
        std::cout << "  Local GPU -> Remote Shared: " <<
            (local_gpu_to_remote_shared / Mmsgs) << " us" << std::endl;
        std::cout << "  Remote Shared -> Local GPU: " <<
            (remote_shared_to_local_gpu / Mmsgs) << " us" << std::endl;
    }
    if (run_ss) {
        std::cout << "  Local Shared -> Remote Shared: " <<
            (local_shared_to_remote_shared / Mmsgs) << " us" << std::endl;
        std::cout << "  Remote Shared -> Local Shared: " <<
            (remote_shared_to_local_shared / Mmsgs) << " us" << std::endl;
    }
    if (run_ps) {
        std::cout << "  Local " << Private() << " -> Remote Shared: " <<
            (local_private_to_remote_shared / Mmsgs) << " us" << std::endl;
        std::cout << "  Remote Shared -> Local " << Private() << ": " <<
            (remote_shared_to_local_private / Mmsgs) << " us" << std::endl;
    }
    if (run_pg) {
        std::cout << "  Local " << Private() << " -> Remote GPU: " <<
            (local_private_to_remote_gpu / Mmsgs) << " us" << std::endl;
        std::cout << "  Remote GPU -> Local " << Private() << ": " <<
            (remote_gpu_to_local_private / Mmsgs) << " us" << std::endl;
    }
}

static void print_bandwidth_results(long msg_len, bool bidirectional) {
    long nmsgs = trials_for_size(msg_len)*window_size;
    if (bidirectional) nmsgs *= 2;

    long nbytes = nmsgs * msg_len;
    double gbytes = double(nbytes) / (1024.0 * 1024.0 * 1024.0);

    if (use_concise) {
      auto col = std::setw(12);
      std::cout << col << msg_len;
      if (run_gg) std::cout << col << gbytes/local_gpu_to_remote_gpu << col << gbytes/remote_gpu_to_local_gpu;
      if (run_sg) std::cout << col << gbytes/local_shared_to_remote_gpu << col << gbytes/remote_gpu_to_local_shared;
      if (run_gs) std::cout << col << gbytes/local_gpu_to_remote_shared << col << gbytes/remote_shared_to_local_gpu;
      if (run_ss) std::cout << col << gbytes/local_shared_to_remote_shared << col << gbytes/remote_shared_to_local_shared;
      if (run_ps) std::cout << col << gbytes/local_private_to_remote_shared << col << gbytes/remote_shared_to_local_private;
      if (run_pg) std::cout << col << gbytes/local_private_to_remote_gpu << col << gbytes/remote_gpu_to_local_private;
      std::cout << std::endl;
      return;
    }

    std::cout << desc << " bandwidth results for " <<
        "message size = " << msg_len << " byte(s)" << std::endl;

    if (run_gg) {
        std::cout << "  Local GPU -> Remote GPU: " <<
            (double(nmsgs) / local_gpu_to_remote_gpu) << " msgs/s, " <<
            (double(gbytes) / local_gpu_to_remote_gpu) << " GiB/s" <<
            std::endl;
        std::cout << "  Remote GPU -> Local GPU: " <<
            (double(nmsgs) / remote_gpu_to_local_gpu) << " msgs/s, " <<
            (double(gbytes) / remote_gpu_to_local_gpu) << " GiB/s" <<
            std::endl;
    }
    if (run_sg) {
        std::cout << "  Local Shared -> Remote GPU: " <<
            (double(nmsgs) / local_shared_to_remote_gpu) << " msgs/s, " <<
            (double(gbytes) / local_shared_to_remote_gpu) << " GiB/s" <<
            std::endl;
        std::cout << "  Remote GPU -> Local Shared: " <<
            (double(nmsgs) / remote_gpu_to_local_shared) << " msgs/s, " <<
            (double(gbytes) / remote_gpu_to_local_shared) << " GiB/s" <<
            std::endl;
    }
    if (run_gs) {
        std::cout << "  Local GPU -> Remote Shared: " <<
            (double(nmsgs) / local_gpu_to_remote_shared) << " msgs/s, " <<
            (double(gbytes) / local_gpu_to_remote_shared) << " GiB/s" <<
            std::endl;
        std::cout << "  Remote Shared -> Local GPU: " <<
            (double(nmsgs) / remote_shared_to_local_gpu) << " msgs/s, " <<
            (double(gbytes) / remote_shared_to_local_gpu) << " GiB/s" <<
            std::endl;
    }
    if (run_ss) {
        std::cout << "  Local Shared -> Remote Shared: " <<
            (double(nmsgs) / local_shared_to_remote_shared) << " msgs/s, " <<
            (double(gbytes) / local_shared_to_remote_shared) << " GiB/s" <<
            std::endl;
        std::cout << "  Remote Shared -> Local Shared: " <<
            (double(nmsgs) / remote_shared_to_local_shared) << " msgs/s, " <<
            (double(gbytes) / remote_shared_to_local_shared) << " GiB/s" <<
            std::endl;
    }
    if (run_ps) {
        std::cout << "  Local " << Private() << " -> Remote Shared: " <<
            (double(nmsgs) / local_private_to_remote_shared) << " msgs/s, " <<
            (double(gbytes) / local_private_to_remote_shared) << " GiB/s" <<
            std::endl;
        std::cout << "  Remote Shared -> Local " << Private() << ": " <<
            (double(nmsgs) / remote_shared_to_local_private) << " msgs/s, " <<
            (double(gbytes) / remote_shared_to_local_private) << " GiB/s" <<
            std::endl;
    }
    if (run_pg) {
        std::cout << "  Local " << Private() << " -> Remote GPU: " <<
            (double(nmsgs) / local_private_to_remote_gpu) << " msgs/s, " <<
            (double(gbytes) / local_private_to_remote_gpu) << " GiB/s" <<
            std::endl;
        std::cout << "  Remote GPU -> Local " << Private() << ": " <<
            (double(nmsgs) / remote_gpu_to_local_private) << " msgs/s, " <<
            (double(gbytes) / remote_gpu_to_local_private) << " GiB/s" <<
            std::endl;
    }
    std::cout << std::endl;
}

int do_main(int argc, char **argv) {

       // Assign partner via "cross-machine" pairing, to help ensure
       // that "Remote" memory regions are over a network (if one exists)
       // The special case of shared-memory pairing can be measured
       // by running all processes on a single node.
       intrank_t partner;
       bool active_half = false;
       if (rank_n()%2 && rank_me()==rank_n()-1) {
         partner = rank_me();
         active_half = true;
       } else {
         int half = rank_n()/2;
         if (rank_me() < half) {
           partner = rank_me() + half;
           active_half = true;
         } else { 
           partner = rank_me() - half;
           active_half = false;
         }
       }
       long max_msg_size = 4 * 1024 * 1024; // 4MB

       max_warmup = 10;
       max_trials = 100;
       window_size = 100;

       int arg_index = 1;
       while (arg_index < argc) {
           char *arg = argv[arg_index];
           if (strcmp(arg, "-t") == 0) {
               if (arg_index + 1 == argc || !std::isdigit(*argv[arg_index+1])) {
                   if (!rank_me()) fprintf(stderr, "Missing argument to -t\n");
                   return 1;
               }
               arg_index++;
               max_trials = atol(argv[arg_index]);
           } else if (strcmp(arg, "-w") == 0) {
               if (arg_index + 1 == argc || !std::isdigit(*argv[arg_index+1])) {
                   if (!rank_me()) fprintf(stderr, "Missing argument to -w\n");
                   return 1;
               }
               arg_index++;
               window_size = atol(argv[arg_index]);
           } else if (strcmp(arg, "-m") == 0) {
               if (arg_index + 1 == argc || !std::isdigit(*argv[arg_index+1])) {
                   if (!rank_me()) fprintf(stderr, "Missing argument to -m\n");
                   return 1;
               }
               arg_index++;
               max_msg_size = atol(argv[arg_index]);
           } else if (strcmp(arg, "-v") == 0) {
               if (arg_index + 1 == argc || !std::isdigit(*argv[arg_index+1])) {
                   if (!rank_me()) fprintf(stderr, "Missing argument to -v\n");
                   return 1;
               }
               arg_index++;
               max_volume = atol(argv[arg_index]);
           } else if (strcmp(arg, "-gg") == 0) {
               run_gg = true;
           } else if (strcmp(arg, "-sg") == 0) {
               run_sg = true;
           } else if (strcmp(arg, "-gs") == 0) {
               run_gs = true;
           } else if (strcmp(arg, "-ss") == 0) {
               run_ss = true;
           } else if (strcmp(arg, "-ps") == 0) {
               run_ps = true;
           } else if (strcmp(arg, "-pg") == 0) {
               run_pg = true;
           } else if (strcmp(arg, "-ds") == 0) {
               use_downcast_self = true;
           } else if (strcmp(arg, "-dp") == 0) {
               use_downcast_peer = true;
           } else if (strcmp(arg, "-c") == 0) {
               use_concise = true;
           } else if (strcmp(arg, "-f") == 0) {
               use_firstlast = true;
           } else {
               if (!rank_me()) {
                   fprintf(stderr, "usage: %s ...\n", argv[0]);
                   fprintf(stderr, "  Iteration control:\n");
                   fprintf(stderr, "       -t <max_trials>: Run up to `trials` number of windows per measurement\n");
                   fprintf(stderr, "       -w <window>: Issue `window` number of copies per window\n");
                   fprintf(stderr, "       -m <max_msg_size>: Cap copy payloads at `max_msg_size` bytes\n");
                   fprintf(stderr, "       -v <max_volume>: Cap trials at larger payloads so each rank sends only enough windows to reach `max_volume` bytes\n");
                   fprintf(stderr, "       -f: First/last mode, where ranks 1..(ranks-2) remain idle\n");
                   fprintf(stderr, "  Memory type selection:\n");
                   fprintf(stderr, "       -gg: Run tests between local and remote GPU segment\n");
                   fprintf(stderr, "       -sg: Run tests between the local shared segment and remote GPU segment\n");
                   fprintf(stderr, "       -gs: Run tests between local GPU and the remote shared segment\n");
                   fprintf(stderr, "       -ss: Run tests between local and remote shared segments\n");
                   fprintf(stderr, "       -ps: Run tests between the local private segment and remote shared segment\n");
                   fprintf(stderr, "       -pg: Run tests between the local private segment and remote GPU segment\n");
                   fprintf(stderr, "  Buffer options:\n");
                   fprintf(stderr, "       -ds: Replace 'private' buffers with downcast shared memory owned by self\n");
                   fprintf(stderr, "       -dp: Replace 'private' buffers with downcast shared memory owned by peer\n");
                   fprintf(stderr, "  Output control:\n");
                   fprintf(stderr, "       -c: Concise/machine-parseable output format\n");
               }
               return 1;
           }
           arg_index++;
       }

       if (!run_gg && !run_sg && !run_gs && !run_ss && !run_ps && !run_pg) {
           // If no tests are selected at the command line, run them all
           run_gg = run_sg = run_gs = run_ss = run_ps = run_pg = true;
       }

       if (rank_me() == 0) {
           std::cout << "cuda_microbenchmark: " ;
           if (use_firstlast) std::cout << " first/last ranks,";
           if (max_volume) std::cout << " trials=" << trials_for_size(1) << ".." << trials_for_size(max_msg_size) 
                                     << " max_volume=" << max_volume;
           else std::cout << " trials=" << max_trials;
           std::cout << " window=" << window_size 
                     << " max_msg_size=" << max_msg_size
                     << std::endl;
       }
       if (!(max_trials > 0 && window_size > 0 && max_msg_size > 0)) {
         if (!rank_me()) std::cerr << "Invalid parameters" << std::endl;
         return 1;
       }

       auto gpu_device = upcxx::cuda_device( 0 ); // open device 0
       // alloc GPU segment
       auto gpu_alloc = device_allocator<cuda_device>(gpu_device,max_msg_size);

       local_gpu_array = gpu_alloc.allocate<uint8_t>(max_msg_size);

       upcxx::dist_object<gp_cuda_t> gpu_dobj(local_gpu_array);
       remote_gpu_array = gpu_dobj.fetch(partner).wait();

       assert(!(use_downcast_self && use_downcast_peer));
       uint8_t *private_array_free = nullptr;
       gp_host_t gp_downcast_area = nullptr;
       if (use_downcast_self) {
         gp_downcast_area = new_array<uint8_t>(max_msg_size);
         local_private_array = gp_downcast_area.local();
       } else if (use_downcast_peer) {
         gp_downcast_area = new_array<uint8_t>(max_msg_size);
         dist_object<gp_host_t> dd(gp_downcast_area, local_team());
         if (local_team().rank_n() == 1) {
           std::cerr << "WARNING: singleton local team, -dp is equivalent to -ds\n" << std::flush;
         }
         gp_host_t peer_downcast_area = dd.fetch((local_team().rank_me() + 1) % local_team().rank_n()).wait();
         assert(peer_downcast_area.is_local());
         local_private_array = peer_downcast_area.local();
         upcxx::barrier();
       } else {
         local_private_array = new uint8_t[max_msg_size];
         assert(local_private_array);
         private_array_free = local_private_array;
       }

       local_shared_array = upcxx::new_array<uint8_t>(max_msg_size);
       upcxx::dist_object<gp_host_t> host_dobj(local_shared_array);
       remote_shared_array = host_dobj.fetch(partner).wait();

       legend();
       if (use_firstlast) is_active_rank = !rank_me();
       else               is_active_rank = active_half;
       test_header("Uni-directional blocking 8-byte round-trip latency (microseconds)"); 
       run_all_copies<sync_type::blocking_op>(8);

       if (rank_me() == 0) print_latency_results();

       test_header("Uni-directional blocking op bandwidth (GiB/s)"); 
       for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
           run_all_copies<sync_type::blocking_op>(msg_len);

           if (rank_me() == 0) print_bandwidth_results( msg_len, false );
       }    

       test_header("Uni-directional flood op bandwidth (GiB/s)"); 
       for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
           run_all_copies<sync_type::flood_op>(msg_len);

           if (rank_me() == 0) print_bandwidth_results( msg_len, false );
       }

       test_header("Uni-directional flood remote_cx bandwidth (GiB/s)"); 
       for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
           run_all_copies<sync_type::flood_remote>(msg_len);

           if (rank_me() == 0) print_bandwidth_results( msg_len, false );
       }

       if (use_firstlast) is_active_rank = (rank_me() == 0 || rank_me() == rank_n()-1);
       else is_active_rank = true;
       test_header("Bi-directional blocking op bandwidth (GiB/s)"); 
       for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
           run_all_copies<sync_type::blocking_op>(msg_len);

           if (rank_me() == 0) print_bandwidth_results( msg_len, true );
       }

       test_header("Bi-directional flood op bandwidth (GiB/s)"); 
       for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
           run_all_copies<sync_type::flood_op>(msg_len);

           if (rank_me() == 0) print_bandwidth_results( msg_len, true );
       }

       test_header("Bi-directional flood remote_cx bandwidth (GiB/s)"); 
       for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
           run_all_copies<sync_type::flood_remote>(msg_len);

           if (rank_me() == 0) print_bandwidth_results( msg_len, true );
       }


       gpu_alloc.deallocate(local_gpu_array);
       upcxx::delete_array(local_shared_array);
       upcxx::delete_array(gp_downcast_area);
       delete[] private_array_free;
       gpu_device.destroy();

       upcxx::barrier();

       if (!rank_me())  std::cout << "\nSUCCESS" << std::endl;
       return 0;
}

int main(int argc, char **argv) {
   upcxx::init();
   int retval = do_main(argc, argv);
   upcxx::finalize();
   return retval;
}

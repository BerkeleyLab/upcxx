#include <upcxx/upcxx.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unistd.h>

using namespace std;
using namespace upcxx;

#ifndef DEVICE
  #if UPCXX_KIND_ZE
    #define DEVICE ze_device
  #elif UPCXX_KIND_HIP
    #define DEVICE hip_device
  #elif UPCXX_KIND_CUDA
    #define DEVICE cuda_device
  #else
    #error This test requires UPC++ built with device support.
  #endif
#endif
using Device = upcxx::DEVICE;
#ifndef STRINGIFY
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
#endif
std::string DeviceStr(STRINGIFY(DEVICE));

bool run_gg = false;
bool run_sg = false;
bool run_gs = false;
bool run_ss = false;
bool run_ps = false;
bool run_pg = false;
bool run_uni = false;
bool run_bi = false;
bool run_put = false;
bool run_get = false;
bool run_block = false;
bool run_flood = false;
bool run_remote = false;
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
           UPCXX_ASSERT(my_sender == -1); 
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
static double row_time() {
  return local_gpu_to_remote_gpu + remote_gpu_to_local_gpu +
              local_shared_to_remote_gpu + remote_gpu_to_local_shared +
              local_gpu_to_remote_shared + remote_shared_to_local_gpu +
              local_shared_to_remote_shared + remote_shared_to_local_shared +
              local_private_to_remote_shared + remote_shared_to_local_private +
              local_private_to_remote_gpu + remote_gpu_to_local_private;
}

using gp_gpu_t = global_ptr<uint8_t, Device::kind>;
using gp_host_t = global_ptr<uint8_t, memory_kind::host>;
gp_gpu_t local_gpu_array;
gp_gpu_t remote_gpu_array;
gp_host_t local_shared_array;
gp_host_t remote_shared_array;
uint8_t *local_private_array;

template<sync_type sync>
static void run_all_copies(long msg_len) {

    if (run_gg) {
        if (run_put) local_gpu_to_remote_gpu =
            helper<sync>(msg_len, local_gpu_array, remote_gpu_array);
        if (run_get) remote_gpu_to_local_gpu =
            helper<sync>(msg_len, remote_gpu_array, local_gpu_array);
    }

    if (run_sg) {
        if (run_put) local_shared_to_remote_gpu =
            helper<sync>(
                    msg_len, local_shared_array, remote_gpu_array);
        if (run_get) remote_gpu_to_local_shared =
            helper<sync>(
                    msg_len, remote_gpu_array, local_shared_array);
    }

    if (run_gs) {
        if (run_put) local_gpu_to_remote_shared =
            helper<sync>(msg_len, local_gpu_array, remote_shared_array);
        if (run_get) remote_shared_to_local_gpu =
            helper<sync>(msg_len, remote_shared_array, local_gpu_array);
    }

    if (run_ss) {
        if (run_put) local_shared_to_remote_shared =
            helper<sync>(msg_len, local_shared_array, remote_shared_array);
        if (run_get) remote_shared_to_local_shared =
            helper<sync>(msg_len, remote_shared_array, local_shared_array);
    }

    if (run_ps) {
        if (run_put) local_private_to_remote_shared =
            helper<sync>(msg_len, local_private_array, remote_shared_array);
        if (run_get) remote_shared_to_local_private =
            helper<sync>(msg_len, remote_shared_array, local_private_array);
    }

    if (run_pg) {
        if (run_put) local_private_to_remote_gpu =
            helper<sync>(msg_len, local_private_array, remote_gpu_array);
        if (run_get) remote_gpu_to_local_private =
            helper<sync>(msg_len, remote_gpu_array, local_private_array);
    }
}

static void legend() {
  if (!use_concise || rank_me()) return;
  std::cout << "\n=== Output Legend ===\n" << std::endl;
  std::cout << "Copy-Size : Size of each copy() operation payload, in bytes" << std::endl;
  std::cout << "Row-Time : Sum of timed region durations for this row (at this Copy-Size), in seconds" << std::endl;
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
      if (run_gg) {
        if (run_put) std::cout << col << "LGpu->RGpu";
        if (run_get) std::cout << col << "RGpu->LGpu";
      }
      if (run_sg) {
        if (run_put) std::cout << col << "LSh->RGpu";
        if (run_get) std::cout << col << "RGpu->LSh";
      }
      if (run_gs) {
        if (run_put) std::cout << col << "LGpu->RSh";
        if (run_get) std::cout << col << "RSh->LGpu";
      }
      if (run_ss) {
        if (run_put) std::cout << col << "LSh->RSh";
        if (run_get) std::cout << col << "RSh->LSh";
      }
      if (run_ps) {
        if (run_put) std::cout << col << Priv() + "->RSh";
        if (run_get) std::cout << col << "RSh->" + Priv();
      }
      if (run_pg) {
        if (run_put) std::cout << col << Priv() + "->RGpu";
        if (run_get) std::cout << col << "RGpu->" + Priv();
      }
      std::cout << col << "Row-Time" << std::endl;
      return;
  }
}

static void print_latency_results() {
    long nmsgs = trials_for_size(8)*window_size;
    double Mmsgs = double(nmsgs) / 1.0e6;

    if (use_concise) {
      auto col = std::setw(12);
      std::cout << col << 8;
      if (run_gg) {
        if (run_put) std::cout << col << local_gpu_to_remote_gpu/Mmsgs;
        if (run_get) std::cout << col << remote_gpu_to_local_gpu/Mmsgs;
      }
      if (run_sg) {
        if (run_put) std::cout << col << local_shared_to_remote_gpu/Mmsgs;
        if (run_get) std::cout << col << remote_gpu_to_local_shared/Mmsgs;
      }
      if (run_gs) {
        if (run_put) std::cout << col << local_gpu_to_remote_shared/Mmsgs;
        if (run_get) std::cout << col << remote_shared_to_local_gpu/Mmsgs;
      }
      if (run_ss) {
        if (run_put) std::cout << col << local_shared_to_remote_shared/Mmsgs;
        if (run_get) std::cout << col << remote_shared_to_local_shared/Mmsgs;
      }
      if (run_ps) {
        if (run_put) std::cout << col << local_private_to_remote_shared/Mmsgs;
        if (run_get) std::cout << col << remote_shared_to_local_private/Mmsgs;
      }
      if (run_pg) {
        if (run_put) std::cout << col << local_private_to_remote_gpu/Mmsgs;
        if (run_get) std::cout << col << remote_gpu_to_local_private/Mmsgs;
      }
      std::cout << col << row_time() << std::endl;
      return;
    }

    if (run_gg) {
        if (run_put) std::cout << "  Local GPU -> Remote GPU: " <<
            (local_gpu_to_remote_gpu / Mmsgs) << " us" << std::endl;
        if (run_get) std::cout << "  Remote GPU -> Local GPU: " <<
            (remote_gpu_to_local_gpu / Mmsgs) << " us" << std::endl;
    }
    if (run_sg) {
        if (run_put) std::cout << "  Local Shared -> Remote GPU: " <<
            (local_shared_to_remote_gpu / Mmsgs) << " us" << std::endl;
        if (run_get) std::cout << "  Remote GPU -> Local Shared: " <<
            (remote_gpu_to_local_shared / Mmsgs) << " us" << std::endl;
    }
    if (run_gs) {
        if (run_put) std::cout << "  Local GPU -> Remote Shared: " <<
            (local_gpu_to_remote_shared / Mmsgs) << " us" << std::endl;
        if (run_get) std::cout << "  Remote Shared -> Local GPU: " <<
            (remote_shared_to_local_gpu / Mmsgs) << " us" << std::endl;
    }
    if (run_ss) {
        if (run_put) std::cout << "  Local Shared -> Remote Shared: " <<
            (local_shared_to_remote_shared / Mmsgs) << " us" << std::endl;
        if (run_get) std::cout << "  Remote Shared -> Local Shared: " <<
            (remote_shared_to_local_shared / Mmsgs) << " us" << std::endl;
    }
    if (run_ps) {
        if (run_put) std::cout << "  Local " << Private() << " -> Remote Shared: " <<
            (local_private_to_remote_shared / Mmsgs) << " us" << std::endl;
        if (run_get) std::cout << "  Remote Shared -> Local " << Private() << ": " <<
            (remote_shared_to_local_private / Mmsgs) << " us" << std::endl;
    }
    if (run_pg) {
        if (run_put) std::cout << "  Local " << Private() << " -> Remote GPU: " <<
            (local_private_to_remote_gpu / Mmsgs) << " us" << std::endl;
        if (run_get) std::cout << "  Remote GPU -> Local " << Private() << ": " <<
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
      if (run_gg) {
        if (run_put) std::cout << col << gbytes/local_gpu_to_remote_gpu;
        if (run_get) std::cout << col << gbytes/remote_gpu_to_local_gpu;
      }
      if (run_sg) {
        if (run_put) std::cout << col << gbytes/local_shared_to_remote_gpu;
        if (run_get) std::cout  << col << gbytes/remote_gpu_to_local_shared;
      }
      if (run_gs) {
        if (run_put) std::cout << col << gbytes/local_gpu_to_remote_shared;
        if (run_get) std::cout  << col << gbytes/remote_shared_to_local_gpu;
      }
      if (run_ss) {
        if (run_put) std::cout << col << gbytes/local_shared_to_remote_shared;
        if (run_get) std::cout  << col << gbytes/remote_shared_to_local_shared;
      }
      if (run_ps) {
        if (run_put) std::cout << col << gbytes/local_private_to_remote_shared;
        if (run_get) std::cout  << col << gbytes/remote_shared_to_local_private;
      }
      if (run_pg) {
        if (run_put) std::cout << col << gbytes/local_private_to_remote_gpu;
        if (run_get) std::cout  << col << gbytes/remote_gpu_to_local_private;
      }
      std::cout << col << row_time() << std::endl;
      return;
    }

    std::cout << desc << " bandwidth results for " <<
        "message size = " << msg_len << " byte(s)" << std::endl;

    if (run_gg) {
        if (run_put) std::cout << "  Local GPU -> Remote GPU: " <<
            (double(nmsgs) / local_gpu_to_remote_gpu) << " msgs/s, " <<
            (double(gbytes) / local_gpu_to_remote_gpu) << " GiB/s" <<
            std::endl;
        if (run_get) std::cout << "  Remote GPU -> Local GPU: " <<
            (double(nmsgs) / remote_gpu_to_local_gpu) << " msgs/s, " <<
            (double(gbytes) / remote_gpu_to_local_gpu) << " GiB/s" <<
            std::endl;
    }
    if (run_sg) {
        if (run_put) std::cout << "  Local Shared -> Remote GPU: " <<
            (double(nmsgs) / local_shared_to_remote_gpu) << " msgs/s, " <<
            (double(gbytes) / local_shared_to_remote_gpu) << " GiB/s" <<
            std::endl;
        if (run_get) std::cout << "  Remote GPU -> Local Shared: " <<
            (double(nmsgs) / remote_gpu_to_local_shared) << " msgs/s, " <<
            (double(gbytes) / remote_gpu_to_local_shared) << " GiB/s" <<
            std::endl;
    }
    if (run_gs) {
        if (run_put) std::cout << "  Local GPU -> Remote Shared: " <<
            (double(nmsgs) / local_gpu_to_remote_shared) << " msgs/s, " <<
            (double(gbytes) / local_gpu_to_remote_shared) << " GiB/s" <<
            std::endl;
        if (run_get) std::cout << "  Remote Shared -> Local GPU: " <<
            (double(nmsgs) / remote_shared_to_local_gpu) << " msgs/s, " <<
            (double(gbytes) / remote_shared_to_local_gpu) << " GiB/s" <<
            std::endl;
    }
    if (run_ss) {
        if (run_put) std::cout << "  Local Shared -> Remote Shared: " <<
            (double(nmsgs) / local_shared_to_remote_shared) << " msgs/s, " <<
            (double(gbytes) / local_shared_to_remote_shared) << " GiB/s" <<
            std::endl;
        if (run_get) std::cout << "  Remote Shared -> Local Shared: " <<
            (double(nmsgs) / remote_shared_to_local_shared) << " msgs/s, " <<
            (double(gbytes) / remote_shared_to_local_shared) << " GiB/s" <<
            std::endl;
    }
    if (run_ps) {
        if (run_put) std::cout << "  Local " << Private() << " -> Remote Shared: " <<
            (double(nmsgs) / local_private_to_remote_shared) << " msgs/s, " <<
            (double(gbytes) / local_private_to_remote_shared) << " GiB/s" <<
            std::endl;
        if (run_get) std::cout << "  Remote Shared -> Local " << Private() << ": " <<
            (double(nmsgs) / remote_shared_to_local_private) << " msgs/s, " <<
            (double(gbytes) / remote_shared_to_local_private) << " GiB/s" <<
            std::endl;
    }
    if (run_pg) {
        if (run_put) std::cout << "  Local " << Private() << " -> Remote GPU: " <<
            (double(nmsgs) / local_private_to_remote_gpu) << " msgs/s, " <<
            (double(gbytes) / local_private_to_remote_gpu) << " GiB/s" <<
            std::endl;
        if (run_get) std::cout << "  Remote GPU -> Local " << Private() << ": " <<
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
           } else if (strcmp(arg, "-uni") == 0) {
               run_uni = true;
           } else if (strcmp(arg, "-bi") == 0) {
               run_bi = true;
           } else if (strcmp(arg, "-p") == 0) {
               run_put = true;
           } else if (strcmp(arg, "-g") == 0) {
               run_get = true;
           } else if (strcmp(arg, "-block") == 0) {
               run_block = true;
           } else if (strcmp(arg, "-flood") == 0) {
               run_flood = true;
           } else if (strcmp(arg, "-remote") == 0) {
               run_remote = true;
           } else {
               if (!rank_me()) {
                   fprintf(stderr, "usage: %s ...\n", argv[0]);
                   fprintf(stderr, "  Iteration control:\n");
                   fprintf(stderr, "       -t <max_trials>: Run up to `trials` number of windows per measurement\n");
                   fprintf(stderr, "       -w <window>: Issue `window` number of copies per window\n");
                   fprintf(stderr, "       -m <max_msg_size>: Cap copy payloads at `max_msg_size` bytes\n");
                   fprintf(stderr, "       -v <max_volume>: Cap trials at larger payloads so each rank sends only enough windows to reach `max_volume` bytes\n");
                   fprintf(stderr, "       -f: First/last mode, where ranks other than 0 and its partner remain idle\n");
                   fprintf(stderr, "  Test pattern selection: (default all)\n");
                   fprintf(stderr, "       -uni: Run unidirectional tests (each proc is either initiator or target)\n");
                   fprintf(stderr, "       -bi:  Run bidirectional tests (initiator procs are also target procs)\n");
                   fprintf(stderr, "       -p:   Run \"put-like\" tests (data movement away from local buffer)\n");
                   fprintf(stderr, "       -g:   Run \"get-like\" tests (data movement towards local buffer)\n");
                   fprintf(stderr, "       -block:  Run one-copy-at-a-time blocking test\n");
                   fprintf(stderr, "       -flood:  Run many-copy-at-a-time flood test synchronized with operation_cx\n");
                   fprintf(stderr, "       -remote: Run many-copy-at-a-time flood test synchronized with remote_cx\n");
                   fprintf(stderr, "  Memory type selection: (default all)\n");
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
       if (!run_uni && !run_bi) {
         run_uni = run_bi = true;
       }
       if (upcxx::rank_n() == 1) {
         std::cerr << "WARNING: Single-rank job, bi-directional tests disabled\n" << std::flush;
         run_bi = false;
       }
       if (!run_put && !run_get) {
         run_put = run_get = true;
       }
       if (!run_block && !run_flood && !run_remote) {
         run_block = run_flood = run_remote = true;
       }

       if (rank_me() == 0) {
           std::cout << "gpu_microbenchmark(" << DeviceStr << "): " ;
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

     #if UPCXX_VERSION >= 20210905
       // use GPU auto-assignment, which spreads local_team members across available physical GPUs
       auto gpu_alloc = upcxx::make_gpu_allocator<Device>(max_msg_size); // alloc GPU segment 
       #if UPCXX_VERSION >= 20220905
         auto kind_info = Device::kind_info();
       #else
         std::string kind_info{};
       #endif
       UPCXX_ASSERT_ALWAYS(gpu_alloc.is_active(), "Failed to open GPU:\n" << kind_info);
       auto& cleanup = gpu_alloc;
       string my_gpu_desc = DeviceStr + ":" + to_string(gpu_alloc.device_id()) + "/" + to_string(Device::device_n());
     #else // pre 2022.3.0 API
       auto gpu_device = Device( 0 ); // open device 0
       // alloc GPU segment
       auto gpu_alloc = device_allocator<Device>(gpu_device,max_msg_size);
       auto& cleanup = gpu_device;
       string my_gpu_desc = DeviceStr + ":0";
     #endif

       local_gpu_array = gpu_alloc.allocate<uint8_t>(max_msg_size);

       upcxx::dist_object<gp_gpu_t> gpu_dobj(local_gpu_array);
       remote_gpu_array = gpu_dobj.fetch(partner).wait();

       UPCXX_ASSERT(!(use_downcast_self && use_downcast_peer));
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
           local_private_array = gp_downcast_area.local();
         } else {
           int lpeer = (local_team().rank_me() + 1) % local_team().rank_n(); // select a downcast peer
           if (local_team()[lpeer] == partner && local_team().rank_n() > 2) { // avoid partner when possible
             lpeer = (local_team().rank_me() + 2) % local_team().rank_n();
           }
           gp_host_t peer_downcast_area = dd.fetch(lpeer).wait();
           UPCXX_ASSERT(peer_downcast_area.is_local());
           local_private_array = peer_downcast_area.local();
         }
         upcxx::barrier();
       } else {
         local_private_array = new uint8_t[max_msg_size];
         UPCXX_ASSERT(local_private_array);
         private_array_free = local_private_array;
       }

       local_shared_array = upcxx::new_array<uint8_t>(max_msg_size);
       upcxx::dist_object<gp_host_t> host_dobj(local_shared_array);
       remote_shared_array = host_dobj.fetch(partner).wait();

       bool active_uni;
       bool active_bi;
       if (use_firstlast) {
         active_uni = !rank_me();
         active_bi = (rank_me() == 0 || partner == 0);
       } else {
         active_uni = active_half;
         active_bi = true;
       }

       barrier();
       { std::ostringstream oss;
         auto col = std::setw(2);
         oss << "Rank " << col << rank_me() << "/" << col << rank_n();
         oss << ": " << my_gpu_desc;
         if (active_uni || active_bi) {
           oss << " partner = " << col << partner;
           if (use_downcast_peer)
             oss << ", downcast peer = " << col << try_global_ptr(local_private_array).where();
         }
         if (active_uni) oss << " (active for ALL tests)";
         else if (active_bi) oss << " (active for BIdirectional tests)";
         else oss << " (passive rank)";
         oss << '\n';
         std::cout << oss.str() << std::flush;
       }
       sleep(1); // help ensure clean output
       barrier();

       legend();
       if (run_uni) {
         is_active_rank = active_uni;

         if (run_block) {
           test_header("Uni-directional blocking 8-byte round-trip latency (microseconds)"); 
           run_all_copies<sync_type::blocking_op>(8);

           if (rank_me() == 0) print_latency_results();

           test_header("Uni-directional blocking op bandwidth (GiB/s)"); 
           for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
               run_all_copies<sync_type::blocking_op>(msg_len);

               if (rank_me() == 0) print_bandwidth_results( msg_len, false );
           }
         }
         if (run_flood) {
           test_header("Uni-directional flood op bandwidth (GiB/s)"); 
           for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
               run_all_copies<sync_type::flood_op>(msg_len);

               if (rank_me() == 0) print_bandwidth_results( msg_len, false );
           }
         }
         if (run_remote) {
           test_header("Uni-directional flood remote_cx bandwidth (GiB/s)"); 
           for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
               run_all_copies<sync_type::flood_remote>(msg_len);

               if (rank_me() == 0) print_bandwidth_results( msg_len, false );
           }
         }
       } // run uni

       if (run_bi) {
         is_active_rank = active_bi;

         if (run_block) {
           test_header("Bi-directional blocking op bandwidth (GiB/s)"); 
           for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
               run_all_copies<sync_type::blocking_op>(msg_len);

               if (rank_me() == 0) print_bandwidth_results( msg_len, true );
           }
         }
         if (run_flood) {
           test_header("Bi-directional flood op bandwidth (GiB/s)"); 
           for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
               run_all_copies<sync_type::flood_op>(msg_len);

               if (rank_me() == 0) print_bandwidth_results( msg_len, true );
           }
         }
         if (run_remote) {
           test_header("Bi-directional flood remote_cx bandwidth (GiB/s)"); 
           for (long msg_len = 1; msg_len <= max_msg_size; msg_len *= 2) {
               run_all_copies<sync_type::flood_remote>(msg_len);

               if (rank_me() == 0) print_bandwidth_results( msg_len, true );
           }
         }
       } // run bi


       gpu_alloc.deallocate(local_gpu_array);
       upcxx::delete_array(local_shared_array);
       upcxx::delete_array(gp_downcast_area);
       delete[] private_array_free;
       cleanup.destroy();

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

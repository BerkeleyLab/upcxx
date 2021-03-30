#include <upcxx/upcxx.hpp>
#include <iostream>
#include <chrono>

using namespace std;
using namespace upcxx;

int run_gg = 0;
int run_sg = 0;
int run_gs = 0;
int run_ss = 0;
int run_ps = 0;
int run_pg = 0;

template<typename src_ptr_type, typename dst_ptr_type, int flood>
static double helper(int warmup, int window_size, int trials, int len,
        src_ptr_type &src_ptr, dst_ptr_type &dst_ptr,
        int is_active_rank) {
    double elapsed = 0.0;

    if (is_active_rank) {
        for (int i = 0; i < warmup; i++) {
            upcxx::promise<> prom;
            for (int j = 0; j < window_size; j++) {
                upcxx::copy(src_ptr, dst_ptr, len,
                        upcxx::operation_cx::as_promise(prom));
                if (!flood) {
                    upcxx::future<> fut = prom.finalize();
                    fut.wait();
                    prom = upcxx::promise<>();
                }
            }

            upcxx::future<> fut = prom.finalize();
            fut.wait();
        }
    }

    upcxx::barrier();

    if (is_active_rank) {
        std::chrono::steady_clock::time_point start =
            std::chrono::steady_clock::now();

        for (int i = 0; i < trials; i++) {
            upcxx::promise<> prom;
            for (int j = 0; j < window_size; j++) {
                upcxx::copy(src_ptr, dst_ptr, len,
                        upcxx::operation_cx::as_promise(prom));
                if (!flood) {
                    upcxx::future<> fut = prom.finalize();
                    fut.wait();
                    prom = upcxx::promise<>();
                }
            }
            upcxx::future<> fut = prom.finalize();
            fut.wait();
        }

        std::chrono::steady_clock::time_point end =
            std::chrono::steady_clock::now();
        elapsed = std::chrono::duration<double>(end - start).count();
    }

    upcxx::barrier();

    return elapsed;
}

template<int flood>
static void run_all_copies(int warmup, int window_size, int trials, int msg_len,
        global_ptr<uint8_t,memory_kind::cuda_device> &local_gpu_array,
        global_ptr<uint8_t,memory_kind::cuda_device> &remote_gpu_array,
        global_ptr<uint8_t> &local_shared_array,
        global_ptr<uint8_t> &remote_shared_array,
        uint8_t *local_private_array,
        int is_active_rank,
        double &local_gpu_to_remote_gpu, double &remote_gpu_to_local_gpu,
        double &local_shared_to_remote_gpu, double &remote_gpu_to_local_shared,
        double &local_gpu_to_remote_shared, double &remote_shared_to_local_gpu,
        double &local_shared_to_remote_shared, double &remote_shared_to_local_shared,
        double &local_private_to_remote_shared, double &remote_shared_to_local_private,
        double &local_private_to_remote_gpu, double &remote_gpu_to_local_private) {

    if (run_gg) {
        local_gpu_to_remote_gpu =
            helper<global_ptr<uint8_t, memory_kind::cuda_device>,
                   global_ptr<uint8_t, memory_kind::cuda_device>, flood>(
                    warmup, window_size, trials, msg_len,
                    local_gpu_array, remote_gpu_array, is_active_rank);
        remote_gpu_to_local_gpu =
            helper<global_ptr<uint8_t, memory_kind::cuda_device>,
                   global_ptr<uint8_t, memory_kind::cuda_device>, flood>(
                    warmup, window_size, trials, msg_len,
                    remote_gpu_array, local_gpu_array, is_active_rank);
    }

    if (run_sg) {
        local_shared_to_remote_gpu =
            helper<global_ptr<uint8_t>, upcxx::global_ptr<uint8_t, memory_kind::cuda_device>, flood>(
                    warmup, window_size, trials, msg_len,
                    local_shared_array, remote_gpu_array, is_active_rank);
        remote_gpu_to_local_shared =
            helper<upcxx::global_ptr<uint8_t, memory_kind::cuda_device>, global_ptr<uint8_t>, flood>(
                    warmup, window_size, trials, msg_len,
                    remote_gpu_array, local_shared_array, is_active_rank);
    }

    if (run_gs) {
        local_gpu_to_remote_shared =
            helper<global_ptr<uint8_t, memory_kind::cuda_device>, global_ptr<uint8_t>, flood>(
                    warmup, window_size, trials, msg_len, local_gpu_array,
                    remote_shared_array, is_active_rank);
        remote_shared_to_local_gpu =
            helper<global_ptr<uint8_t>, global_ptr<uint8_t, memory_kind::cuda_device>, flood>(
                    warmup, window_size, trials, msg_len, remote_shared_array,
                    local_gpu_array, is_active_rank);
    }

    if (run_ss) {
        local_shared_to_remote_shared =
            helper<global_ptr<uint8_t>, global_ptr<uint8_t>, flood>(
                    warmup, window_size, trials, msg_len, local_shared_array,
                    remote_shared_array, is_active_rank);
        remote_shared_to_local_shared =
            helper<global_ptr<uint8_t>, global_ptr<uint8_t>, flood>(
                    warmup, window_size, trials, msg_len, remote_shared_array,
                    local_shared_array, is_active_rank);
    }

    if (run_ps) {
        local_private_to_remote_shared =
            helper<uint8_t*, global_ptr<uint8_t>, flood>(
                    warmup, window_size, trials, msg_len, local_private_array,
                    remote_shared_array, is_active_rank);
        remote_shared_to_local_private =
            helper<global_ptr<uint8_t>, uint8_t*, flood>(
                    warmup, window_size, trials, msg_len, remote_shared_array,
                    local_private_array, is_active_rank);
    }

    if (run_pg) {
        local_private_to_remote_gpu =
            helper<uint8_t*, global_ptr<uint8_t, memory_kind::cuda_device>, flood>(
                    warmup, window_size, trials, msg_len, local_private_array,
                    remote_gpu_array, is_active_rank);
        remote_gpu_to_local_private =
            helper<global_ptr<uint8_t, memory_kind::cuda_device>, uint8_t*, flood>(
                    warmup, window_size, trials, msg_len, remote_gpu_array,
                    local_private_array, is_active_rank);
    }
}

static void print_latency_results(double local_gpu_to_remote_gpu,
        double remote_gpu_to_local_gpu, double local_shared_to_remote_gpu,
        double remote_gpu_to_local_shared, double local_gpu_to_remote_shared,
        double remote_shared_to_local_gpu, double local_shared_to_remote_shared,
        double remote_shared_to_local_shared, double local_private_to_remote_shared,
        double remote_shared_to_local_private, double local_private_to_remote_gpu,
        double remote_gpu_to_local_private, int trials, int window_size) {
    long nmsgs = trials * window_size;

    std::cout << "Latency results for 8-byte transfers" << std::endl;

    if (run_gg) {
        std::cout << "  Local GPU -> Remote GPU: " <<
            (local_gpu_to_remote_gpu / double(nmsgs)) <<
            " s of latency" << std::endl;
        std::cout << "  Remote GPU -> Local GPU: " <<
            (remote_gpu_to_local_gpu / double(nmsgs)) <<
            " s of latency" << std::endl;
    }
    if (run_sg) {
        std::cout << "  Local Shared -> Remote GPU: " <<
            (local_shared_to_remote_gpu / double(nmsgs)) <<
            " s of latency" << std::endl;
        std::cout << "  Remote GPU -> Local Shared: " <<
            (remote_gpu_to_local_shared / double(nmsgs)) <<
            " s of latency" << std::endl;
    }
    if (run_gs) {
        std::cout << "  Local GPU -> Remote Shared: " <<
            (local_gpu_to_remote_shared / double(nmsgs)) <<
            " s of latency" << std::endl;
        std::cout << "  Remote Shared -> Local GPU: " <<
            (remote_shared_to_local_gpu / double(nmsgs)) <<
            " s of latency" << std::endl;
    }
    if (run_ss) {
        std::cout << "  Local Shared -> Remote Shared: " <<
            (local_shared_to_remote_shared / double(nmsgs)) <<
            " s of latency" << std::endl;
        std::cout << "  Remote Shared -> Local Shared: " <<
            (remote_shared_to_local_shared / double(nmsgs)) <<
            " s of latency" << std::endl;
    }
    if (run_ps) {
        std::cout << "  Local Private -> Remote Shared: " <<
            (local_private_to_remote_shared / double(nmsgs)) <<
            " s of latency" << std::endl;
        std::cout << "  Remote Shared -> Local Private: " <<
            (remote_shared_to_local_private / double(nmsgs)) <<
            " s of latency" << std::endl;
    }
    if (run_pg) {
        std::cout << "  Local Private -> Remote GPU: " <<
            (local_private_to_remote_gpu / double(nmsgs)) <<
            " s of latency" << std::endl;
        std::cout << "  Remote GPU -> Local Private: " <<
            (remote_gpu_to_local_private / double(nmsgs)) <<
            " s of latency" << std::endl;
    }
}

static void print_bandwidth_results(double local_gpu_to_remote_gpu,
        double remote_gpu_to_local_gpu, double local_shared_to_remote_gpu,
        double remote_gpu_to_local_shared, double local_gpu_to_remote_shared,
        double remote_shared_to_local_gpu, double local_shared_to_remote_shared,
        double remote_shared_to_local_shared, double local_private_to_remote_shared,
        double remote_shared_to_local_private, double local_private_to_remote_gpu,
        double remote_gpu_to_local_private, int trials, int window_size,
        int msg_len, int bidirectional, int flood) {
    std::string sync_type, dir_type;
    long nmsgs = trials * window_size;
    if (bidirectional) {
        nmsgs *= 2;
    }

    long nbytes = nmsgs * msg_len;
    double gbytes = double(nbytes) / (1024.0 * 1024.0 * 1024.0);

    if (flood) {
        sync_type = "Asynchronous";
    } else {
        sync_type = "Blocking";
    }

    if (bidirectional) {
        dir_type = "bi-directional";
    } else {
        dir_type = "uni-directional";
    }

    std::cout << sync_type << " " << dir_type << " bandwidth results for " <<
        "message size = " << msg_len << " byte(s)" << std::endl;

    if (run_gg) {
        std::cout << "  Local GPU -> Remote GPU: " <<
            (double(nmsgs) / local_gpu_to_remote_gpu) << " msgs/s, " <<
            (double(gbytes) / local_gpu_to_remote_gpu) << " GB/s" <<
            std::endl;
        std::cout << "  Remote GPU -> Local GPU: " <<
            (double(nmsgs) / remote_gpu_to_local_gpu) << " msgs/s, " <<
            (double(gbytes) / remote_gpu_to_local_gpu) << " GB/s" <<
            std::endl;
    }
    if (run_sg) {
        std::cout << "  Local Shared -> Remote GPU: " <<
            (double(nmsgs) / local_shared_to_remote_gpu) << " msgs/s, " <<
            (double(gbytes) / local_shared_to_remote_gpu) << " GB/s" <<
            std::endl;
        std::cout << "  Remote GPU -> Local Shared: " <<
            (double(nmsgs) / remote_gpu_to_local_shared) << " msgs/s, " <<
            (double(gbytes) / remote_gpu_to_local_shared) << " GB/s" <<
            std::endl;
    }
    if (run_gs) {
        std::cout << "  Local GPU -> Remote Shared: " <<
            (double(nmsgs) / local_gpu_to_remote_shared) << " msgs/s, " <<
            (double(gbytes) / local_gpu_to_remote_shared) << " GB/s" <<
            std::endl;
        std::cout << "  Remote Shared -> Local GPU: " <<
            (double(nmsgs) / remote_shared_to_local_gpu) << " msgs/s, " <<
            (double(gbytes) / remote_shared_to_local_gpu) << " GB/s" <<
            std::endl;
    }
    if (run_ss) {
        std::cout << "  Local Shared -> Remote Shared: " <<
            (double(nmsgs) / local_shared_to_remote_shared) << " msgs/s, " <<
            (double(gbytes) / local_shared_to_remote_shared) << " GB/s" <<
            std::endl;
        std::cout << "  Remote Shared -> Local Shared: " <<
            (double(nmsgs) / remote_shared_to_local_shared) << " msgs/s, " <<
            (double(gbytes) / remote_shared_to_local_shared) << " GB/s" <<
            std::endl;
    }
    if (run_ps) {
        std::cout << "  Local Private -> Remote Shared: " <<
            (double(nmsgs) / local_private_to_remote_shared) << " msgs/s, " <<
            (double(gbytes) / local_private_to_remote_shared) << " GB/s" <<
            std::endl;
        std::cout << "  Remote Shared -> Local Private: " <<
            (double(nmsgs) / remote_shared_to_local_private) << " msgs/s, " <<
            (double(gbytes) / remote_shared_to_local_private) << " GB/s" <<
            std::endl;
    }
    if (run_pg) {
        std::cout << "  Local Private -> Remote GPU: " <<
            (double(nmsgs) / local_private_to_remote_gpu) << " msgs/s, " <<
            (double(gbytes) / local_private_to_remote_gpu) << " GB/s" <<
            std::endl;
        std::cout << "  Remote GPU -> Local Private: " <<
            (double(nmsgs) / remote_gpu_to_local_private) << " msgs/s, " <<
            (double(gbytes) / remote_gpu_to_local_private) << " GB/s" <<
            std::endl;
    }
}

int main(int argc, char **argv) {
   {
       upcxx::init();

       const int partner = (rank_me() + 1) % rank_n();
       int max_msg_size = 4 * 1024 * 1024; // 4MB
       std::size_t segsize = max_msg_size;
       auto gpu_device = upcxx::cuda_device( 0 ); // open device 0

       int warmup = 10;
       int trials = 100;
       int window_size = 100;

       int arg_index = 1;
       while (arg_index < argc) {
           char *arg = argv[arg_index];
           if (strcmp(arg, "-t") == 0) {
               if (arg_index + 1 == argc) {
                   if (!rank_me()) fprintf(stderr, "Missing argument to -t\n");
                   upcxx::finalize();
                   return 1;
               }
               arg_index++;
               trials = atoi(argv[arg_index]);
           } else if (strcmp(arg, "-w") == 0) {
               if (arg_index + 1 == argc) {
                   if (!rank_me()) fprintf(stderr, "Missing argument to -w\n");
                   upcxx::finalize();
                   return 1;
               }
               arg_index++;
               window_size = atoi(argv[arg_index]);
           } else if (strcmp(arg, "-gg") == 0) {
               run_gg = 1;
           } else if (strcmp(arg, "-sg") == 0) {
               run_sg = 1;
           } else if (strcmp(arg, "-gs") == 0) {
               run_gs = 1;
           } else if (strcmp(arg, "-ss") == 0) {
               run_ss = 1;
           } else if (strcmp(arg, "-ps") == 0) {
               run_ps = 1;
           } else if (strcmp(arg, "-pg") == 0) {
               run_pg = 1;
           } else {
               if (!rank_me()) {
                   fprintf(stderr, "usage: %s [-t trials] [-w window] [-gg] [-sg] [-gs] [-ss] [-ps] [-pg]\n", argv[0]);
                   fprintf(stderr, "       -gg: Run tests between local and remote GPU segment\n");
                   fprintf(stderr, "       -sg: Run tests between the local shared segment and remote GPU segment\n");
                   fprintf(stderr, "       -gs: Run tests between local GPU and the remote shared segment\n");
                   fprintf(stderr, "       -ss: Run tests between local and remote shared segments\n");
                   fprintf(stderr, "       -ps: Run tests between the local private segment and remote shared segment\n");
                   fprintf(stderr, "       -pg: Run tests between the local private segment and remote GPU segment\n");
               }
               upcxx::finalize();
               return 1;
           }
           arg_index++;
       }

       if (!run_gg && !run_sg && !run_gs && !run_ss && !run_ps && !run_pg) {
           // If no tests are selected at the command line, run them all
           run_gg = run_sg = run_gs = run_ss = run_ps = run_pg = 1;
       }

       if (rank_me() == 0) {
           std::cout << "Running " << trials << " trials of window_size=" <<
               window_size << std::endl;
       }

       // alloc GPU segment
       auto gpu_alloc = upcxx::device_allocator<upcxx::cuda_device>(gpu_device,
               segsize);

       global_ptr<uint8_t,memory_kind::cuda_device> local_gpu_array =
           gpu_alloc.allocate<uint8_t>(max_msg_size);

       upcxx::dist_object<upcxx::global_ptr<uint8_t, memory_kind::cuda_device>> gpu_dobj(local_gpu_array);
       global_ptr<uint8_t, memory_kind::cuda_device> remote_gpu_array =
           gpu_dobj.fetch(partner).wait();

       uint8_t *private_array = new uint8_t[max_msg_size];
       assert(private_array);

       global_ptr<uint8_t, memory_kind::host> shared_array =
           upcxx::new_array<uint8_t>(max_msg_size);
       upcxx::dist_object<upcxx::global_ptr<uint8_t>> host_dobj(shared_array);
       global_ptr<uint8_t> remote_shared_array = host_dobj.fetch(partner).wait();

       double local_gpu_to_remote_gpu, remote_gpu_to_local_gpu,
              local_shared_to_remote_gpu, remote_gpu_to_local_shared,
              local_gpu_to_remote_shared, remote_shared_to_local_gpu,
              local_shared_to_remote_shared, remote_shared_to_local_shared,
              local_private_to_remote_shared, remote_shared_to_local_private,
              local_private_to_remote_gpu, remote_gpu_to_local_private;

       run_all_copies<0>(warmup, window_size, trials, 8, local_gpu_array,
               remote_gpu_array, shared_array, remote_shared_array,
               private_array,
               (rank_me() == 0),
               local_gpu_to_remote_gpu, remote_gpu_to_local_gpu,
               local_shared_to_remote_gpu, remote_gpu_to_local_shared,
               local_gpu_to_remote_shared, remote_shared_to_local_gpu,
               local_shared_to_remote_shared, remote_shared_to_local_shared,
               local_private_to_remote_shared, remote_shared_to_local_private,
               local_private_to_remote_gpu, remote_gpu_to_local_private);

       if (rank_me() == 0) {
           print_latency_results(local_gpu_to_remote_gpu,
                   remote_gpu_to_local_gpu, local_shared_to_remote_gpu,
                   remote_gpu_to_local_shared, local_gpu_to_remote_shared,
                   remote_shared_to_local_gpu, local_shared_to_remote_shared,
                   remote_shared_to_local_shared, local_private_to_remote_shared,
                   remote_shared_to_local_private, local_private_to_remote_gpu,
                   remote_gpu_to_local_private, trials, window_size);
           std::cout << std::endl;
       }

       upcxx::barrier();

       int msg_len = 1;
       while (msg_len <= max_msg_size) {
           // Uni-directional blocking bandwidth test
           int is_active_rank = !(rank_me() & 1);
           run_all_copies<0>(warmup, window_size, trials, msg_len,
                   local_gpu_array, remote_gpu_array, shared_array,
                   remote_shared_array, private_array,
                   is_active_rank,
                   local_gpu_to_remote_gpu, remote_gpu_to_local_gpu,
                   local_shared_to_remote_gpu, remote_gpu_to_local_shared,
                   local_gpu_to_remote_shared, remote_shared_to_local_gpu,
                   local_shared_to_remote_shared, remote_shared_to_local_shared,
                   local_private_to_remote_shared, remote_shared_to_local_private,
                   local_private_to_remote_gpu, remote_gpu_to_local_private);

           if (rank_me() == 0) {
               print_bandwidth_results(local_gpu_to_remote_gpu,
                       remote_gpu_to_local_gpu, local_shared_to_remote_gpu,
                       remote_gpu_to_local_shared, local_gpu_to_remote_shared,
                       remote_shared_to_local_gpu, local_shared_to_remote_shared,
                       remote_shared_to_local_shared, local_private_to_remote_shared,
                       remote_shared_to_local_private, local_private_to_remote_gpu,
                       remote_gpu_to_local_private, trials, window_size,
                       msg_len, 0, 0);
               std::cout << std::endl;
           }
           
           upcxx::barrier();

           // Uni-directional non-blocking bandwidth test
           run_all_copies<1>(warmup, window_size, trials, msg_len,
                   local_gpu_array, remote_gpu_array, shared_array,
                   remote_shared_array, private_array,
                   is_active_rank,
                   local_gpu_to_remote_gpu, remote_gpu_to_local_gpu,
                   local_shared_to_remote_gpu, remote_gpu_to_local_shared,
                   local_gpu_to_remote_shared, remote_shared_to_local_gpu,
                   local_shared_to_remote_shared, remote_shared_to_local_shared,
                   local_private_to_remote_shared, remote_shared_to_local_private,
                   local_private_to_remote_gpu, remote_gpu_to_local_private);

           if (rank_me() == 0) {
               print_bandwidth_results(local_gpu_to_remote_gpu,
                       remote_gpu_to_local_gpu, local_shared_to_remote_gpu,
                       remote_gpu_to_local_shared, local_gpu_to_remote_shared,
                       remote_shared_to_local_gpu, local_shared_to_remote_shared,
                       remote_shared_to_local_shared, local_private_to_remote_shared,
                       remote_shared_to_local_private, local_private_to_remote_gpu,
                       remote_gpu_to_local_private, trials, window_size,
                       msg_len, 0, 1);
               std::cout << std::endl;
           }

           upcxx::barrier();

           // Bi-directional blocking bandwidth test
           is_active_rank = 1;
           run_all_copies<0>(warmup, window_size, trials, msg_len,
                   local_gpu_array, remote_gpu_array, shared_array,
                   remote_shared_array, private_array,
                   is_active_rank,
                   local_gpu_to_remote_gpu, remote_gpu_to_local_gpu,
                   local_shared_to_remote_gpu, remote_gpu_to_local_shared,
                   local_gpu_to_remote_shared, remote_shared_to_local_gpu,
                   local_shared_to_remote_shared, remote_shared_to_local_shared,
                   local_private_to_remote_shared, remote_shared_to_local_private,
                   local_private_to_remote_gpu, remote_gpu_to_local_private);

           if (rank_me() == 0) {
               print_bandwidth_results(local_gpu_to_remote_gpu,
                       remote_gpu_to_local_gpu, local_shared_to_remote_gpu,
                       remote_gpu_to_local_shared, local_gpu_to_remote_shared,
                       remote_shared_to_local_gpu, local_shared_to_remote_shared,
                       remote_shared_to_local_shared, local_private_to_remote_shared,
                       remote_shared_to_local_private, local_private_to_remote_gpu,
                       remote_gpu_to_local_private, trials, window_size,
                       msg_len, 1, 0);
               std::cout << std::endl;
           }
           
           upcxx::barrier();

           // Bi-directional non-blocking bandwidth test
           run_all_copies<1>(warmup, window_size, trials, msg_len,
                   local_gpu_array, remote_gpu_array, shared_array,
                   remote_shared_array, private_array,
                   is_active_rank,
                   local_gpu_to_remote_gpu, remote_gpu_to_local_gpu,
                   local_shared_to_remote_gpu, remote_gpu_to_local_shared,
                   local_gpu_to_remote_shared, remote_shared_to_local_gpu,
                   local_shared_to_remote_shared, remote_shared_to_local_shared,
                   local_private_to_remote_shared, remote_shared_to_local_private,
                   local_private_to_remote_gpu, remote_gpu_to_local_private);

           if (rank_me() == 0) {
               print_bandwidth_results(local_gpu_to_remote_gpu,
                       remote_gpu_to_local_gpu, local_shared_to_remote_gpu,
                       remote_gpu_to_local_shared, local_gpu_to_remote_shared,
                       remote_shared_to_local_gpu, local_shared_to_remote_shared,
                       remote_shared_to_local_shared, local_private_to_remote_shared,
                       remote_shared_to_local_private, local_private_to_remote_gpu,
                       remote_gpu_to_local_private, trials, window_size,
                       msg_len, 1, 1);
               std::cout << std::endl;
           }

           upcxx::barrier();

           msg_len *= 2;
       }

       gpu_alloc.deallocate(local_gpu_array);
       upcxx::delete_array(shared_array);
       delete[] private_array;
       gpu_device.destroy();

       upcxx::barrier();

       if (!rank_me())  std::cout << "SUCCESS" << std::endl;
   }
   upcxx::finalize();
}

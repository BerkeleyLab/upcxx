/* This example simulates heat transfer through a cube with a power source on 
 * one side. At every timestep it calculates the cube's average temperature.
 *
 * It has been adapted from a Kokkos/MPI interoperability example in 
 * the Kokkos Tutorials repository (https://github.com/kokkos/kokkos-tutorials/)
 * as allowed under terms specified in the LICENSE-Kokkos.txt file that can be 
 * found in this directory.
 *
 * This example uses the rput-rpc idiom to send halo data to neighboring
 * processes and ensure that the data has been received before performing
 * computation on the relevant cells. The interprocess communication for this
 * can be found in the `exchange_T_halo()` function.
 *
 * This variant of the test only supports host execution. For a variant 
 * supporting GPU execution, UPC++ memory kinds, and Kokkos, check out
 * upcxx-extras/examples/kokkos_3Dhalo/upcxx_heat_conduction.cpp.
*/

#include<chrono>
#include<cmath>
#include<algorithm>
#include<upcxx/upcxx.hpp>

typedef struct _Expected {
    int length, iters;
    double avg;
} Expected;

static Expected exp_avg_temp[] = {
    {20,10,     28910001.659627},
    {20,100,   101705231.273039},
    {20,1000,  226372573.760934},
    {20,10000, 132848459.167244},
    {100,10,     5808348.947955},
    {100,100,   21452388.032534},
    {100,1000,  58583907.520696},
    {100,10000,135304050.230331},
    {200,10,     2905829.475261},
    {200,100,   10799013.696056},
    {200,1000,  30229622.441892},
    {200,10000, 77422551.027710}
};

static void validate(int cube, int iters, double avg) {
    for (size_t i = 0; i < sizeof(exp_avg_temp)/sizeof(exp_avg_temp[0]); i++) {
        Expected *exp = &exp_avg_temp[i];
        if (exp->length == cube && exp->iters == iters) {
            double abs = std::fabs(avg - exp->avg);
            double rel = abs/exp->avg;
            printf("Average temperature: expected = %e, calculated = %e, "
                    "absolute error = %e, percent error = %e\n",
                    exp->avg, avg, abs, rel);
            if (rel > 0.01 || std::isnan(rel))
                printf("FAIL\n");
            else
                printf("SUCCESS\n");
            return;
        }
    }
    printf("WARNING: Unable to validate simulation, unsupported problem size\n");
    printf("SUCCESS\n");
}

struct CommHelper {
  upcxx::team &team;

  // This rank and # UPC++ ranks
  int x, nx;

  // Neighbor Ranks
  int left,right;
 
  CommHelper(upcxx::team &team_) : team(team_) {
    nx = team.rank_n();
    x = team.rank_me();
    left  = x==0 ?-1:x-1;
    right = x==nx-1?-1:x+1;
    upcxx::barrier(team);
  }
};

struct System {
  // Using theoretical physicists way of describing system, 
  // i.e. we stick everything in as few constants as possible
  // be i and i+1 two timesteps dt apart: 
  // T[x*X*X+y*X+z]_(i+1) = T[x*X*X+y*X+z]_(i)+dT[x*X*X+y*X+z]*dt; 
  // dT[x*X*X+y*X+z] = q * sum_dxdydz( T(x+dx,y+dy,z+dz) - T[x*X*X+y*X+z] )
  // If its surface of the body add:
  // dT[x*X*X+y*X+z] += -sigma*T[x*X*X+y*X+z]^4
  // If its z==0 surface add incoming radiation energy
  // dT(x,y,0) += P

  // Communicator
  CommHelper comm;

  // Number of neighbors in logical space
  int nbors;

  // size of system
  int X;

  // Local box
  int lo, hi;

  // number of timesteps
  int N;
  
  // interval for print
  int I;

  // Temperature and delta Temperature
  double *T, *dT;

  // Halo buffers and pointers to neighboring buffers
  upcxx::global_ptr<double> gptr_left, gptr_right, left_ghost_slab, right_ghost_slab;

  // Initial temperature
  double T0;

  // timestep width
  double dt;

  // thermal transfer coefficient 
  double q;

  // thermal radiation coefficient (assume Stefan Boltzmann law P = sigma*A*T^4
  double sigma;

  // incoming power
  double P;

  // init_system
  System() : comm(upcxx::world()) {
    nbors = 0;
    X = 100;
    lo = 0;
    hi = X; 
    N = 100;
    I = 10;
    T = nullptr;
    dT = nullptr;
    T0 = 0.0;
    dt = 0.1;
    q = 1.0;
    sigma = 1.0;
    P = 1.0;
    gptr_left = nullptr;
    gptr_right = nullptr;
    left_ghost_slab = nullptr;
    right_ghost_slab = nullptr;
  }

  void setup_subdomain() {
    int dX = X/comm.nx;
    if (comm.x < X%comm.nx)
        dX++;
    lo = dX*comm.x;
    if (comm.x >= X%comm.nx)
        lo += X%comm.nx;
    hi = lo + dX;

    printf("Rank %i Domain: [%i,%i)\n",comm.x,lo,hi);
    size_t local_sz = (hi-lo)*X*X;
    T = new double[local_sz];
    std::fill_n(T, local_sz, T0);
    dT = new double[local_sz]();

    if(lo != 0) {
        left_ghost_slab = upcxx::new_array<double>(X*X);
        nbors++;
    }
    if(hi != X) {
        right_ghost_slab = upcxx::new_array<double>(X*X);
        nbors++;
    }
    upcxx::dist_object<upcxx::global_ptr<double>> dist_left{left_ghost_slab}; 
    upcxx::dist_object<upcxx::global_ptr<double>> dist_right{right_ghost_slab};
    if(lo != 0) 
        gptr_left = dist_right.fetch(comm.left).wait();
    if(hi != X) 
        gptr_right = dist_left.fetch(comm.right).wait();
    upcxx::barrier(comm.team);
  }

  void destroy_subdomain() {
    delete [] T;
    delete [] dT;
    upcxx::delete_array<double>(left_ghost_slab);
    upcxx::delete_array<double>(right_ghost_slab);
  }

  void print_help() {
    if (upcxx::rank_me() == 0) {
        printf("Options (default):\n");
        printf("  1st argument: (%i) num elements in each direction\n", X); 
        printf("  2nd argument: (%i) num timesteps\n", N); 
    }
  }

  bool check_args(int argc, char *argv[]) {
    if (argc > 1) {
        X = atoi(argv[1]);
        if (argc > 2)
            N = atoi(argv[2]);
    }
    if (X >= upcxx::rank_n() && N > 0)
        return true;
    else {
        print_help();
        return false;
    }
  }

  enum {left,right,down,up,front,back};

  // run time loops
  void timestep() {
    double T_ave = 0;
    for(int t=0; t<=N; t++) {
      if(t>N/2) P = 0.0;
      exchange_T_halo();
      compute_inner_dT();
      compute_surface_dT<down>();
      compute_surface_dT<up>();
      compute_surface_dT<front>();
      compute_surface_dT<back>();
      sync_halo();
      compute_surface_dT<left>();
      compute_surface_dT<right>();
      T_ave = compute_T();
      T_ave/=1e-9*(X * X * X);
      if(I != 0 && (t%I == 0 || t==N) && (comm.x==0)) 
        printf("%i T=%f\n",t,T_ave);
    }
    if (comm.x==0)
        validate(X,N,T_ave);
  }

  // Compute inner update
  void compute_inner_dT() {
    for (int x=1; x<hi-lo-1; x++)
        for (int y=1; y<X-1; y++)
            for (int z=1; z<X-1; z++) {
                double dT_xyz = 0.0;
                double T_xyz = T[x*X*X+y*X+z];
                dT_xyz += q * (T[(x-1)*X*X+y*X+z] - T_xyz);
                dT_xyz += q * (T[(x+1)*X*X+y*X+z] - T_xyz);
                dT_xyz += q * (T[x*X*X+(y-1)*X+z] - T_xyz);
                dT_xyz += q * (T[x*X*X+(y+1)*X+z] - T_xyz);
                dT_xyz += q * (T[x*X*X+y*X+z-1] - T_xyz);
                dT_xyz += q * (T[x*X*X+y*X+z+1] - T_xyz);

                dT[x*X*X+y*X+z] = dT_xyz;
            }
  }

  template<int Surface>
  void compute_surface_dT() {      
    // indices of the surface to iterate over: one is fixed since 2D   
    int x, y, z;
    // lower and upper iteration bounds for the two free dimensions
    int l1, l2, u1, u2;
    // some surfaces have larger bounds so edges and corners aren't done twice
    if(Surface == left)  { l1 = 0; l2 = 0; u1 = X; u2 = X;}
    if(Surface == right) { l1 = 0; l2 = 0; u1 = X; u2 = X;}
    if(Surface == down)  { l1 = 1; l2 = 0; u1 = hi-lo-1; u2 = X;}
    if(Surface == up)    { l1 = 1; l2 = 0; u1 = hi-lo-1; u2 = X;}
    if(Surface == front) { l1 = 1; l2 = 1; u1 = hi-lo-1; u2 = X-1;}
    if(Surface == back)  { l1 = 1; l2 = 1; u1 = hi-lo-1; u2 = X-1;}

    for (int i=l1; i<u1; i++)
        for (int j=l2; j<u2; j++) {
            if(Surface == left)  { x = 0;    y = i;    z = j; }
            if(Surface == right) { x = hi-lo-1; y = i;    z = j; }
            if(Surface == down)  { x = i;    y = 0;    z = j; }
            if(Surface == up)    { x = i;    y = X-1; z = j; }
            if(Surface == front) { x = i;    y = j;    z = 0; }
            if(Surface == back)  { x = i;    y = j;    z = X-1; }

            double dT_xyz = 0.0;
            double T_xyz = T[x*X*X+y*X+z];

            // Heat conduction to inner body
            if(x > 0)    dT_xyz += q * (T[(x-1)*X*X+y*X+z] - T_xyz);
            if(x < hi-lo-1) dT_xyz += q * (T[(x+1)*X*X+y*X+z] - T_xyz);
            if(y > 0)    dT_xyz += q * (T[x*X*X+(y-1)*X+z] - T_xyz);
            if(y < X-1) dT_xyz += q * (T[x*X*X+(y+1)*X+z] - T_xyz);
            if(z > 0)    dT_xyz += q * (T[x*X*X+y*X+z-1] - T_xyz);
            if(z < X-1) dT_xyz += q * (T[x*X*X+y*X+z+1] - T_xyz);

            // Heat conduction with Halo    
            if(x == 0 && lo != 0)  dT_xyz += q * (left_ghost_slab.local()[y*X+z] - T_xyz);
            if(x == (hi-lo-1) && hi != X)  dT_xyz += q * (right_ghost_slab.local()[y*X+z] - T_xyz);

            // Incoming Power
            if(x == 0 && lo == 0) dT_xyz += P;

            // thermal radiation
            int num_surfaces = ( (x==0  && lo == 0) ? 1 : 0)
                              +( (x==(hi-lo-1) && hi == X) ? 1 : 0)
                              +( (y==0) ? 1 : 0)
                              +( (y==(X-1)) ? 1 : 0)
                              +( (z==0) ? 1 : 0)
                              +( (z==(X-1)) ? 1 : 0);
            dT_xyz -= sigma * T_xyz * T_xyz * T_xyz * T_xyz * num_surfaces;
            dT[x*X*X+y*X+z] = dT_xyz;
        }
  }

//=============================================================================
/* This section is the crux of this example, as it illustrates the rput-rpc
 * idiom. For an explanation of how it works, check out the `Remote Completions`
 * section of the programming guide as well as this PAW paper: 
 * https://doi.org/10.25344/S4630V
 */

  // used in RPCs to check rput completion
  static int count;

  void exchange_T_halo() {
/* Completion object is an RPC which increments a local counter variable. 
 * Completion of the remote put operations here is determined by whether the 
 * counter has been incremented to equal the number of neighbors the calling 
 * rank has in the domain, and user-level progress is advanced until that 
 * happens, after which the counter is reset.
 */ 
    if(lo != 0) {
      upcxx::rput(T,gptr_left,X*X,
        upcxx::remote_cx::as_rpc([](){count++;}));
    }
    if(hi != X) {
      upcxx::rput(T+X*X*(hi-lo-1),gptr_right,X*X,
        upcxx::remote_cx::as_rpc([](){count++;}));
    }
  }

  void sync_halo() {
      while(count < nbors) upcxx::progress();
      count = 0;
  }
//=============================================================================

  double compute_T() {
    double my_T=0.;
    for (int i=0; i<hi-lo; i++)
        for (int j=0; j<X; j++)
            for (int k=0; k<X; k++) {
                my_T += T[i*X*X+j*X+k];
                T[i*X*X+j*X+k] += dt * dT[i*X*X+j*X+k];
            }
    return upcxx::reduce_all(my_T,upcxx::op_fast_add,comm.team).wait();
  }
};
int System::count = 0;

int main(int argc, char* argv[]) {
    upcxx::init();
    System sys;
    if(sys.check_args(argc,argv)) {
        sys.setup_subdomain();
        sys.timestep();
        sys.destroy_subdomain();
    }
    upcxx::finalize();
}

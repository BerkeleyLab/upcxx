// This example demonstrates how to construct a singleton team
// on each individual process, which acts analogously to MPI_COMM_SELF.

#include <iostream>
#include <upcxx/upcxx.hpp>

upcxx::team *team_self_p;
inline upcxx::team &self() { return *team_self_p; }

int main() {
   upcxx::init();
   int world_rank = upcxx::rank_me();
   // setup a singleton self-team:
   team_self_p = new upcxx::team(upcxx::world().create(std::vector<int>{world_rank}));

   // use the new team
   UPCXX_ASSERT_ALWAYS(self().rank_me() == 0);
   UPCXX_ASSERT_ALWAYS(self().rank_n() == 1);
   upcxx::barrier(self());
   upcxx::dist_object<int> dobj(world_rank, self());
   UPCXX_ASSERT_ALWAYS(dobj.fetch(0).wait() == world_rank);

   // cleanup (optional)
   self().destroy();
   delete team_self_p;

   upcxx::barrier();
   if (!upcxx::rank_me()) std::cout<<"SUCCESS"<<std::endl;

   upcxx::finalize();
}

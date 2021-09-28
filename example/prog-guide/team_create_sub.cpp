#include <upcxx/upcxx.hpp>

using namespace std;

int main() {
  upcxx::init();

  upcxx::team & world_team = upcxx::world();  
  int color = upcxx::rank_me() % 2;
  int key = upcxx::rank_me() / 2;
  upcxx::team new_team = world_team.split(color, key);

//SNIPPET
  upcxx::intrank_t group = new_team.rank_me() / 2; // rounds-down
  upcxx::intrank_t left  = group * 2;
  upcxx::intrank_t right = left + 1;
  std::vector<upcxx::intrank_t> members({left});
  if (right != new_team.rank_n()) // right member exists
    members.push_back(right);  
  upcxx::team sub_team = new_team.create(members);
//SNIPPET
  
  if (!upcxx::rank_me()) cout << "SUCCESS" << endl;
  upcxx::finalize();
  return 0;
}

#include <upcxx/upcxx.hpp>

using namespace std;

int main() {
  upcxx::init();

  upcxx::team & world_team = upcxx::world();  
  int color = upcxx::rank_me() % 2;
  int key = upcxx::rank_me() / 2;
  upcxx::team new_team = world_team.split(color, key);

//SNIPPET
  // select some ranks in parent team:
  upcxx::intrank_t right = new_team.rank_me() | 0x1;
  std::vector<upcxx::intrank_t> members({right-1});
  if (right != new_team.rank_n()) members.push_back(right);
  // construct a sub-team:
  upcxx::team sub_team = new_team.create(members);
//SNIPPET
  
  if (!upcxx::rank_me()) cout << "SUCCESS" << endl;
  upcxx::finalize();
  return 0;
}

#include <string>
#include <cstdio>
#include <fstream>
#include <unistd.h>
#include "upcxx/upcxx.hpp"

#define N 100

void process_data(size_t n, size_t *arr) {
  UPCXX_ASSERT_ALWAYS(N == n);
  for (size_t i = 0; i < n; ++i)
    UPCXX_ASSERT_ALWAYS(arr[i] == i);
}

int main() {
  upcxx::init();

  // Create input file for snippet
  std::string filename = std::string("input-file-")+std::to_string(getpid())+"-"+std::to_string(upcxx::rank_me());

  if (!upcxx::local_team().rank_me()) {
    std::ofstream output_file(filename, std::ifstream::out);

    output_file << N << ' ';
    for (size_t i = 0; i < N; ++i)
      output_file << i << ' ';

    output_file.close();
  }

  //SNIPPET
  std::ifstream input_file;
  std::pair<size_t, upcxx::global_ptr<size_t>> data;

  if (!upcxx::local_team().rank_me()) {
    input_file.open(filename, std::ifstream::in);

    input_file >> data.first;

    data.second = upcxx::new_array<size_t>(data.first);

    for (size_t i = 0; i < data.first; ++i)
      input_file >> data.second.local()[i];
  }

  data = broadcast(data, 0, upcxx::local_team()).wait();

  // Downcast global pointer
  size_t *local = data.second.local();

  // Work with local ptr
  process_data(data.first, local);
  //SNIPPET

  upcxx::barrier(upcxx::local_team());

  if (!upcxx::local_team().rank_me()) {
    upcxx::delete_array(data.second);
    input_file.close();
    remove(filename.c_str());
  }

  upcxx::finalize();
  return 0;
}

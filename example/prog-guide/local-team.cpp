#include <cstdio>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <unistd.h>
#include "upcxx/upcxx.hpp"

// Function to consume the data shared by the leader process in the node
void process_data(size_t n, double *arr) {
  UPCXX_ASSERT_ALWAYS(n);

  double sum = 0.0;
  for (size_t i = 0; i < n; ++i)
    sum += arr[i];

  UPCXX_ASSERT_ALWAYS(sum == (n*n - n) / 2.0);
}

int main() {
  upcxx::init();

  // Create input file for snippet
  std::string filename = std::string("input-file-")+std::to_string(getpid())+"-"+std::to_string(upcxx::rank_me())+".bin";

  // If I'm the leader process in this node
  if (!upcxx::local_team().rank_me()) {
    // Create a binary file
    std::ofstream output_file(filename, std::ios::binary);

    if (!output_file.is_open()) {
      std::cerr << "Couldn't create the file.\n";
      return 1;
    }

    constexpr size_t n = 100;

    // How many elements am I going to write
    output_file.write(reinterpret_cast<const char*>(&n), sizeof(n));

    double *arr = new double[n];

    // [0.0 .. n-1]
    std::iota(arr, arr + n, 0.0);

    // Write entire array to the file
    output_file.write(reinterpret_cast<const char*>(arr), sizeof(*arr)*n);

    output_file.close();
    delete[] arr;
  }

  //SNIPPET
  std::pair<size_t, upcxx::global_ptr<double> > data;

  // If I'm the leader process in this node
  if (!upcxx::local_team().rank_me()) {
    // Open the file in binary mode
    std::ifstream input_file(filename, std::ios::binary);

    if (!input_file.is_open()) {
      std::cerr << "No input file.\n";
      return 1;
    }

    // How many elements am I supposed to read
    input_file.read(reinterpret_cast<char*>(&data.first), sizeof(data.first));

    // Allocate space in shared memory
    data.second = upcxx::new_array<double>(data.first);

    // Read the entire array of doubles from the file
    input_file.read(reinterpret_cast<char*>(data.second.local()), sizeof(*data.second.local())*data.first);

    // I no longer need the file
    input_file.close();
    remove(filename.c_str());
  }

  // Leader makes data available to other processes in the local team
  data = broadcast(data, 0, upcxx::local_team()).wait(); // Implicit barrier

  // Downcast global pointer
  double *local = data.second.local();

  // Work with local ptr
  process_data(data.first, local);
  //SNIPPET

  // At this point, the node leader process can safely deallocate the shared memory
  upcxx::barrier(upcxx::local_team());
  if (!upcxx::local_team().rank_me())
    upcxx::delete_array(data.second);

  // For sanity, the leader of upcxx::world() prints SUCCESS if everyone reaches this point
  upcxx::barrier();
  if (!upcxx::rank_me())
    std::cout << "SUCCESS" << std::endl;

  upcxx::finalize();
  return 0;
}

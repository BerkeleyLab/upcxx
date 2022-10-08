#include <cstdio>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <tuple>
#include <unistd.h>
#include "upcxx/upcxx.hpp"

// Function to consume the data shared by the leader process in the node
void process_data(size_t n, double *arr) {
  UPCXX_ASSERT_ALWAYS(n);

  double sum = std::accumulate(arr, arr + n, 0.0);

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

    // Ensure any I/O errors will throw an exception
    output_file.exceptions(std::ofstream::failbit);

    // Length of the array
    constexpr size_t n = 100;

    output_file.write(reinterpret_cast<const char*>(&n), sizeof(n));

    double *arr = new double[n];

    // [0.0 .. n-1]
    std::iota(arr, arr + n, 0.0);

    // Write entire array to the file
    output_file.write(reinterpret_cast<const char*>(arr), sizeof(double)*n);

    delete[] arr;
  }

  //SNIPPET
  size_t n = 0;
  upcxx::global_ptr<double> data;

  if (!upcxx::local_team().rank_me()) { // I'm the leader process in this node
    // Open the file in binary mode
    std::ifstream input_file(filename, std::ios::binary);

    // Ensure any I/O errors will throw an exception
    input_file.exceptions(std::ifstream::failbit);

    // How many elements am I supposed to read?
    input_file.read(reinterpret_cast<char*>(&n), sizeof(n));

    data = upcxx::new_array<double>(n); // Allocate space in shared memory

    // Read the entire array of doubles from the file
    input_file.read(reinterpret_cast<char*>(data.local()), sizeof(double)*n);
  }

  // Leader broadcasts data to other processes in the local team (implicit barrier)
  std::tie(n, data) = 
    broadcast(std::make_tuple(n, data), 0, upcxx::local_team()).wait(); 

  double *ldata = data.local(); // Downcast global pointer

  process_data(n, ldata); // Access shared data using local ptr
  //SNIPPET

  // At this point, the node leader process can safely deallocate the shared memory
  upcxx::barrier(upcxx::local_team());
  if (!upcxx::local_team().rank_me()) {
    upcxx::delete_array(data);
    remove(filename.c_str());
  }

  upcxx::barrier();
  if (!upcxx::rank_me())
    std::cout << "SUCCESS" << std::endl;

  upcxx::finalize();
  return 0;
}

// Find global minimum and its location

#include <algorithm>
#include <complex>
#include <iostream>
#include <random>
#include <utility>
#include "upcxx/upcxx.hpp"

#define N 5

using Complex = std::complex<float>;

int main() {
    upcxx::init();

    int myrank = upcxx::rank_me();

    std::mt19937 generator(myrank);

    std::uniform_real_distribution<float> distribution(0.0, 1.0);

    Complex *arr = new Complex[N];

    for (size_t i = 0; i < N; ++i)
        arr[i] = { distribution(generator), distribution(generator) };

    //SNIPPET
    // Find local minimum
    Complex lmin = *std::min_element(arr, arr + N, [](const Complex& a, const Complex& b) {
        return std::norm(a) < std::norm(b);
        });

    // Find global minimum and its location (MINLOC)
    auto gmin = upcxx::reduce_one(std::make_pair(lmin, upcxx::rank_me()),
        [](const std::pair<Complex, int>& a, const std::pair<Complex, int>& b) {
            auto norma = std::norm(a.first), normb = std::norm(b.first);
            if (norma == normb && b.second < a.second) return b;
            else return (norma <= normb ? a : b);
        }, 0).wait();
    //SNIPPET

    if (!myrank)
        std::cout << "Minimum complex number is " << gmin.first << " found in process " <<
        gmin.second << "\nSUCCESS\n";

    delete[] arr;

    upcxx::finalize();
    return 0;
}

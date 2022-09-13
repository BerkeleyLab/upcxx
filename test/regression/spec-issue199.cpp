#include <iostream>
#include <string>
#include <functional>
#include "../util.hpp"

int main(int argc, char *argv[]) {
  upcxx::init();
  print_test_header();

  std::string s = "Hello from rank ";
  s += std::to_string(upcxx::rank_me());

  if (!upcxx::rank_me()) std::cout << upcxx::is_trivially_serializable<decltype(s)>::value <<std::endl;
  upcxx::barrier();
  upcxx::rpc(0,[](std::string const &s1) {
    std::cout << s1 << std::endl;
  }, s).wait();
  upcxx::barrier();

  std::string &s_ref = s;
  if (!upcxx::rank_me()) std::cout << upcxx::is_trivially_serializable<decltype(s_ref)>::value <<std::endl;
  upcxx::barrier();
  upcxx::rpc(0,[](std::string const &s1, std::string const &s2) {
    UPCXX_ASSERT_ALWAYS(s1 == s2);
    std::cout << s2 << std::endl;
  }, s, s_ref).wait();
  upcxx::barrier();

  std::reference_wrapper<std::string> s_refwrap = s;
  if (!upcxx::rank_me()) std::cout << upcxx::is_trivially_serializable<decltype(s_refwrap)>::value <<std::endl;
  upcxx::barrier();
  upcxx::rpc(0,[](std::string const &s1, std::string const &s2) {
    UPCXX_ASSERT_ALWAYS(s1 == s2);
    std::cout << s2 << std::endl;
  }, s, s_refwrap).wait();
  upcxx::barrier();

  upcxx::barrier();

  print_test_success();
  upcxx::finalize();
}

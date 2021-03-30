#include "../util.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

using namespace upcxx;

int main() {
  upcxx::init();
  print_test_header();

  static_assert(std::is_default_constructible<dist_id<int>>::value,
                "dist_id not DefaultConstructible");
  static_assert(std::is_trivially_copyable<dist_id<int>>::value,
                "dist_id not TriviallyCopyable");
  static_assert(std::is_standard_layout<dist_id<int>>::value,
                "dist_id not StandardLayoutType");

  dist_id<int> id1;
  dist_object<int> dobj2(3);
  dist_object<int> dobj3(-1);
  dist_object<double> dobj4(3.14);
  UPCXX_ASSERT_ALWAYS(!(id1 == dobj3.id()));
  UPCXX_ASSERT_ALWAYS(id1 != dobj3.id());
  UPCXX_ASSERT_ALWAYS((id1 <= dobj3.id()) ^ (id1 > dobj3.id()));
  UPCXX_ASSERT_ALWAYS((id1 >= dobj3.id()) ^ (id1 < dobj3.id()));
  UPCXX_ASSERT_ALWAYS(!(dobj2.id() == dobj3.id()));
  UPCXX_ASSERT_ALWAYS(dobj2.id() != dobj3.id());
  UPCXX_ASSERT_ALWAYS((dobj2.id() <= dobj3.id()) ^ (dobj2.id() > dobj3.id()));
  UPCXX_ASSERT_ALWAYS((dobj2.id() >= dobj3.id()) ^ (dobj2.id() < dobj3.id()));

  std::size_t hash1 = std::hash<dist_id<int>>()(id1);
  std::cout << id1 << ": " << hash1 << std::endl;
  std::size_t hash2 = std::hash<dist_id<int>>()(dobj2.id());
  std::cout << dobj2.id() << ": " << hash2 << std::endl;
  std::size_t hash3 = std::hash<dist_id<int>>()(dobj3.id());
  std::cout << dobj3.id() << ": " << hash3 << std::endl;
  std::size_t hash4 = std::hash<dist_id<double>>()(dobj4.id());
  std::cout << dobj4.id() << ": " << hash4 << std::endl;

  std::ostringstream tmp;
  tmp << id1;
  std::string s1 = tmp.str();
  tmp.str("");
  tmp << dobj2.id();
  std::string s2 = tmp.str();
  tmp.str("");
  tmp << dobj3.id();
  std::string s3 = tmp.str();
  tmp.str("");
  tmp << dobj4.id();
  std::string s4 = tmp.str();
  std::cout << s1 << " " << s2 << " " << s3 << " " << s4 << std::endl;
  UPCXX_ASSERT_ALWAYS(s1 != s2);
  UPCXX_ASSERT_ALWAYS(s2 != s3);
  UPCXX_ASSERT_ALWAYS(s3 != s1);
  UPCXX_ASSERT_ALWAYS(s1 != s4);
  UPCXX_ASSERT_ALWAYS(s2 != s4);
  UPCXX_ASSERT_ALWAYS(s3 != s4);

  print_test_success();
  upcxx::finalize();
}

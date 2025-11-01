# Simple test driver for UPCXX CMake package

if(CMAKE_VERSION VERSION_LESS 3.12)
  cmake_minimum_required( VERSION 3.6 )
else() # range syntax was introduced in 3.12
  cmake_minimum_required( VERSION 3.6...4.0 )
endif()
project (upcxx_test)

if(DEFINED ENV{UPCXX_INSTALL})
  list(APPEND CMAKE_PREFIX_PATH $ENV{UPCXX_INSTALL} )
endif()

find_package( UPCXX 2019.9.1 REQUIRED )
message("FOUND UPCXX_VERSION=${UPCXX_VERSION}" )

add_executable( upcxx_test upcxx_test.cxx )
target_link_libraries(upcxx_test PUBLIC UPCXX::upcxx)

add_executable( c_test c_test.c )
target_link_libraries(c_test PUBLIC UPCXX::upcxx)

message("SUCCESS")


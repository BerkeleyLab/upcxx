#[=======================================================================[.rst:
UPCXXConfig
-------

Configuration for UPC++.

UPC++ is a C++ library that supports Partitioned Global Address Space
(PGAS) programming, and is designed to interoperate smoothly and
efficiently with MPI, OpenMP, CUDA and AMTs. It leverages GASNet-EX to
deliver low-overhead, fine-grained communication, including Remote Memory
Access (RMA) and Remote Procedure Call (RPC).

This module tries to find the upcxx-meta utility in the following locations:
 * $ENV{UPCXX_INSTALL}/bin, if set
 * $ENV{UPCXX_DIR}/bin, if set
 * The bin directory for the prefix where this CMake package file is located
 * Anywhere in $ENV{PATH}

#]=======================================================================]


if(CMAKE_VERSION VERSION_LESS 3.12)
  cmake_minimum_required( VERSION 3.6 )
else() # range syntax was introduced in 3.12
  cmake_minimum_required( VERSION 3.6...4.0 )
endif()

option(UPCXX_VERBOSE "Verbose UPC++ detection" OFF)
function(UPCXX_VERB MESSAGE)
  if (UPCXX_VERBOSE OR DEFINED ENV{UPCXX_VERBOSE} )
    message(STATUS "${MESSAGE}")
  endif()
endfunction()


# Find upcxx-meta, taking hints into account
if(DEFINED ENV{UPCXX_INSTALL} ) # recommended best-practice for UPC++ installs
  find_program( UPCXX_META_EXECUTABLE upcxx-meta HINTS "$ENV{UPCXX_INSTALL}/bin" NO_DEFAULT_PATH )
elseif(DEFINED ENV{UPCXX_DIR} ) # CMake's find_package auto variable
  find_program( UPCXX_META_EXECUTABLE upcxx-meta HINTS "$ENV{UPCXX_DIR}/bin" NO_DEFAULT_PATH )
elseif(DEFINED CMAKE_CURRENT_LIST_DIR AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../bin/upcxx-meta" )
  find_program( UPCXX_META_EXECUTABLE upcxx-meta HINTS "${CMAKE_CURRENT_LIST_DIR}/../../../bin" NO_DEFAULT_PATH )
else() # Use PATH
  find_program( UPCXX_META_EXECUTABLE upcxx-meta )
endif()
if (NOT EXISTS "${UPCXX_META_EXECUTABLE}")
  message(WARNING "Failed to find UPC++ command interface 'upcxx-meta'. Please set UPCXX_INSTALL=/path/to/upcxx or add /path/to/upcxx/bin to $PATH")
endif()


if( UPCXX_META_EXECUTABLE )
  message(STATUS "Found UPCXX: ${UPCXX_META_EXECUTABLE}")

  if ((NOT DEFINED ENV{UPCXX_CODEMODE}) OR ("$ENV{UPCXX_CODEMODE}" STREQUAL "unset"))
    string(TOUPPER "${CMAKE_BUILD_TYPE}" uc_CMAKE_BUILD_TYPE)
    if (uc_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
      set(ENV{UPCXX_CODEMODE} "debug" )
    else()
      set(ENV{UPCXX_CODEMODE} "O3" ) # O3 is compatible with any version
    endif()
    unset(uc_CMAKE_BUILD_TYPE)
  endif()
  if (NOT DEFINED ENV{UPCXX_THREADMODE} )
    set(ENV{UPCXX_THREADMODE} "seq" )
  endif()

  execute_process( COMMAND ${UPCXX_META_EXECUTABLE} CXXFLAGS OUTPUT_VARIABLE UPCXX_CXXFLAGS
                   OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process( COMMAND ${UPCXX_META_EXECUTABLE} CPPFLAGS OUTPUT_VARIABLE UPCXX_CPPFLAGS
                   OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process( COMMAND ${UPCXX_META_EXECUTABLE} LIBS OUTPUT_VARIABLE UPCXX_LIBFLAGS
                   OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process( COMMAND ${UPCXX_META_EXECUTABLE} LDFLAGS OUTPUT_VARIABLE UPCXX_LDFLAGS
                   OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process( COMMAND ${UPCXX_META_EXECUTABLE} CXX OUTPUT_VARIABLE UPCXX_CXX_COMPILER
                   OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process( COMMAND ${UPCXX_META_EXECUTABLE} GASNET_CONDUIT OUTPUT_VARIABLE UPCXX_NETWORK
                   OUTPUT_STRIP_TRAILING_WHITESPACE)

  string(STRIP ${UPCXX_LIBFLAGS} UPCXX_LIBFLAGS)
  string(STRIP ${UPCXX_CPPFLAGS} UPCXX_CPPFLAGS)
  string(STRIP ${UPCXX_CXXFLAGS} UPCXX_CXXFLAGS)
  string(STRIP ${UPCXX_LDFLAGS} UPCXX_LDFLAGS)
  string(STRIP ${UPCXX_CXX_COMPILER} UPCXX_CXX_COMPILER)

  list( APPEND UPCXX_LIBRARIES ${UPCXX_LIBFLAGS})

  # move any embedded options from UPCXX_CXX_COMPILER to UPCXX_CXXFLAGS
  if (UPCXX_CXX_COMPILER MATCHES "[ \t\n]+(.+)$")
     UPCXX_VERB("embedded CXX options: ${CMAKE_MATCH_0}")
     string(REGEX REPLACE "[ \t\n]+.+$" "" UPCXX_CXX_COMPILER ${UPCXX_CXX_COMPILER})
     string(STRIP "${CMAKE_MATCH_0} ${UPCXX_CXXFLAGS}" UPCXX_CXXFLAGS)
     string(STRIP "${CMAKE_MATCH_0} ${UPCXX_LDFLAGS}" UPCXX_LDFLAGS)
     UPCXX_VERB("augmented UPCXX_CXXFLAGS=${UPCXX_CXXFLAGS}")
     UPCXX_VERB("augmented UPCXX_LDFLAGS=${UPCXX_LDFLAGS}")
  endif()
  UPCXX_VERB("UPCXX_CXX_COMPILER=${UPCXX_CXX_COMPILER}")
  UPCXX_VERB("CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
  #get absolute path, resolving symbolic links, of UPCXX_CXX_COMPILER
  find_program( ABS_UPCXX_CXX_PATH ${UPCXX_CXX_COMPILER} )
  get_filename_component(ABS_UPCXX_CXX_PATH ${ABS_UPCXX_CXX_PATH} REALPATH /)
  if (NOT EXISTS "${ABS_UPCXX_CXX_PATH}")
    message(WARNING "CANNOT FIND ABSOLUTE PATH TO UPCXX_CXX_COMPILER (${UPCXX_CXX_COMPILER})")
    set(ABS_UPCXX_CXX_PATH "${UPCXX_CXX_COMPILER}")
  endif()

  #get absolute path, resolving symbolic links, of CMAKE_CXX_COMPILER
  get_filename_component(ABS_CMAKE_CXX_PATH ${CMAKE_CXX_COMPILER} REALPATH /)
  UPCXX_VERB("ABS_UPCXX_CXX_PATH=${ABS_UPCXX_CXX_PATH}")
  UPCXX_VERB("ABS_CMAKE_CXX_PATH=${ABS_CMAKE_CXX_PATH}")

  set( UPCXX_COMPATIBLE_COMPILER FALSE)
  if("${ABS_UPCXX_CXX_PATH}" STREQUAL "${ABS_CMAKE_CXX_PATH}")
    set( UPCXX_COMPATIBLE_COMPILER TRUE)
  else()
    get_filename_component(UPCXX_CXX_NAME ${ABS_UPCXX_CXX_PATH} NAME)
    get_filename_component(CMAKE_CXX_NAME ${ABS_CMAKE_CXX_PATH} NAME)
    UPCXX_VERB("compiler names: ${UPCXX_CXX_NAME} vs ${CMAKE_CXX_NAME}")
    if("${UPCXX_CXX_NAME}" STREQUAL "${CMAKE_CXX_NAME}")
      #compare the versions
      execute_process( COMMAND ${UPCXX_CXX_COMPILER}  --version OUTPUT_VARIABLE UPCXX_CXX_COMPILER_VERSION)
      string(REPLACE "\n" " " UPCXX_CXX_COMPILER_VERSION ${UPCXX_CXX_COMPILER_VERSION})
      execute_process( COMMAND ${CMAKE_CXX_COMPILER}  --version OUTPUT_VARIABLE LOC_CMAKE_CXX_COMPILER_VERSION)
      string(REPLACE "\n" " " LOC_CMAKE_CXX_COMPILER_VERSION ${LOC_CMAKE_CXX_COMPILER_VERSION})
      #message(STATUS "${UPCXX_CXX_COMPILER_VERSION} vs ${LOC_CMAKE_CXX_COMPILER_VERSION}")
      if("${UPCXX_CXX_COMPILER_VERSION}" STREQUAL "${LOC_CMAKE_CXX_COMPILER_VERSION}")
        set( UPCXX_COMPATIBLE_COMPILER TRUE)
      endif()
    endif()
  endif()

  if( NOT UPCXX_COMPATIBLE_COMPILER )
    message(WARNING "Compiler compatibility check failed!\nUPCXX compiler provided by upcxx-meta CXX:\n    ${UPCXX_CXX_COMPILER} ->\n    ${ABS_UPCXX_CXX_PATH}\nis different from CMAKE_CXX_COMPILER:\n    ${CMAKE_CXX_COMPILER} ->\n    ${ABS_CMAKE_CXX_PATH}\n\nPlease either pass cmake: -DCMAKE_CXX_COMPILER=${UPCXX_CXX_COMPILER}\nor re-install UPC++ with: CXX=${CMAKE_CXX_COMPILER}\n")
  endif()

  unset(ABS_UPCXX_CXX_PATH)
  unset(ABS_CMAKE_CXX_PATH)
  unset(UPCXX_CXX_NAME)
  unset(CMAKE_CXX_NAME)
  unset(UPCXX_CXX_COMPILER_VERSION)

  #now separate include dirs from flags
  if(UPCXX_CPPFLAGS)
    string(REGEX REPLACE "[ ]+" ";" UPCXX_CPPFLAGS ${UPCXX_CPPFLAGS})
    foreach( option ${UPCXX_CPPFLAGS} )
      string(STRIP ${option} option)
      string(REGEX MATCH "^-I" UPCXX_INCLUDE ${option})
      if( UPCXX_INCLUDE )
        string( REGEX REPLACE "^-I" "" option ${option} )
        list( APPEND UPCXX_INCLUDE_DIRS ${option})
      else()
        string(REGEX MATCH "^-D" UPCXX_DEFINE ${option})
        if( UPCXX_DEFINE )
          string( REGEX REPLACE "^-D" "" option ${option} )
          list( APPEND UPCXX_DEFINITIONS ${option})
        else()
          list( APPEND UPCXX_OPTIONS ${option})
        endif()
      endif()
    endforeach()
  endif()
  if(UPCXX_LDFLAGS)
    string(REGEX REPLACE "[ ]+" ";" UPCXX_LDFLAGS ${UPCXX_LDFLAGS})
    foreach( option ${UPCXX_LDFLAGS} )
      string(STRIP ${option} option)
      if (option MATCHES "^-O" AND CMAKE_BUILD_TYPE)
        # filter -O options when CMake is handling that
      else()
        list( APPEND UPCXX_LINK_OPTIONS ${option})
      endif()
    endforeach()
  endif()

  #extract the required cxx standard from the flags
  if(UPCXX_CXXFLAGS)
    string(REGEX REPLACE "[ ]+" ";" UPCXX_CXXFLAGS ${UPCXX_CXXFLAGS})
    foreach( option ${UPCXX_CXXFLAGS} )
      if( option MATCHES "^-+std=(c|gnu)\\+\\+([0-9a-z]+)" )
        set( UPCXX_CXX_STD_FLAG ${option} )
        set( UPCXX_CXX_STANDARD ${CMAKE_MATCH_2})
        # Compilers use 'provisional' standard arguments for unratified specs,
        # But CMake uses the final ratified name for compiler features 
        string(REGEX REPLACE "[01]x$" "11" UPCXX_CXX_STANDARD "${UPCXX_CXX_STANDARD}")
        string(REGEX REPLACE "1y$" "14" UPCXX_CXX_STANDARD "${UPCXX_CXX_STANDARD}")
        string(REGEX REPLACE "1z$" "17" UPCXX_CXX_STANDARD "${UPCXX_CXX_STANDARD}")
        string(REGEX REPLACE "2a$" "20" UPCXX_CXX_STANDARD "${UPCXX_CXX_STANDARD}")
      elseif (option MATCHES "^-O" AND CMAKE_BUILD_TYPE)
        # filter -O options when CMake is handling that
      else()
        list( APPEND UPCXX_OPTIONS ${option})
      endif()
    endforeach()
  endif()
  if(NOT UPCXX_CXX_STANDARD)
    set(UPCXX_CXX_STANDARD "11") # default base version
  endif()

  unset( UPCXX_CXXFLAGS )
  unset( UPCXX_LIBFLAGS )
  unset( UPCXX_CPPFLAGS )
  unset( UPCXX_LDFLAGS )
  unset( UPCXX_INCLUDE )
  unset( UPCXX_DEFINE )
endif()

foreach( dir ${UPCXX_INCLUDE_DIRS} )
  if( EXISTS         ${dir}/upcxx/version.hpp ) # > 2020.3.0
    set(version_file ${dir}/upcxx/version.hpp ) 
  elseif ( EXISTS    ${dir}/upcxx/upcxx.hpp ) # <= 2020.3.0
    set(version_file ${dir}/upcxx/upcxx.hpp ) 
  endif()
  if ( version_file )
    set( version_pattern 
      "#[\t ]*define[\t ]+UPCXX_VERSION[\t ]+([0-9]+)"
      )
    #UPCXX_VERB("checking ${version_file} for ${version_pattern}" )
    file( STRINGS ${version_file} upcxx_version
      REGEX ${version_pattern} )
    #UPCXX_VERB("upcxx_version ${upcxx_version}" )

    if( ${upcxx_version} MATCHES ${version_pattern} )
      set(UPCXX_VERSION_STRING ${CMAKE_MATCH_1})
    endif()

    unset( upcxx_version )
    unset( version_file )
    unset( version_pattern )
  endif()
endforeach()

if(UPCXX_VERSION_STRING)
  UPCXX_VERB("UPCXX_VERSION_STRING: ${UPCXX_VERSION_STRING}")
  if( ${UPCXX_VERSION_STRING} MATCHES "([0-9][0-9][0-9][0-9])([0-9][0-9])([0-9][0-9])" )
      set(UPCXX_VERSION_MAJOR ${CMAKE_MATCH_1})
      set(UPCXX_VERSION_MINOR ${CMAKE_MATCH_2})
      set(UPCXX_VERSION_PATCH ${CMAKE_MATCH_3})
      string(REGEX REPLACE "^0([0-9])" "\\1" UPCXX_VERSION_MINOR "${UPCXX_VERSION_MINOR}")
      string(REGEX REPLACE "^0([0-9])" "\\1" UPCXX_VERSION_PATCH "${UPCXX_VERSION_PATCH}")
      set(UPCXX_VERSION "${UPCXX_VERSION_MAJOR}.${UPCXX_VERSION_MINOR}.${UPCXX_VERSION_PATCH}")
      set(UPCXX_VERSION_COUNT 3)
      UPCXX_VERB("UPCXX_VERSION: ${UPCXX_VERSION}")
  endif()
endif()

# CMake bug #15826: CMake's ill-advised deduplication mis-feature breaks certain types
# of compiler arguments, see: https://gitlab.kitware.com/cmake/cmake/issues/15826
# Here we workaround the problem as best we can to prevent compile failures
function(UPCXX_FIX_FRAGILE_OPTS var)
  set(fragile_option_pat ";(-+param|-+Xcompiler);([^;]+);")
  set(temp ";${${var}};")
  while (temp MATCHES "${fragile_option_pat}") 
    # should NOT need a loop here, but regex replace is buggy, at least in cmake 3.6
    if (CMAKE_VERSION VERSION_LESS 3.12.0)
      # no known workaround, must strip these options
      string(REGEX REPLACE "${fragile_option_pat}" ";" temp "${temp}")
    else()
      # use the SHELL: prefix introduced in cmake 3.12
      string(REGEX REPLACE "${fragile_option_pat}" ";SHELL:\\1 \\2;" temp "${temp}")
    endif()
  endwhile()
  list(FILTER temp EXCLUDE REGEX "^$") # strip surrounding empties
  set("${var}" "${temp}" PARENT_SCOPE)
endfunction()
UPCXX_FIX_FRAGILE_OPTS(UPCXX_OPTIONS)
UPCXX_FIX_FRAGILE_OPTS(UPCXX_LINK_OPTIONS)

# Determine if we've found UPCXX
mark_as_advanced( UPCXX_FOUND UPCXX_META_EXECUTABLE UPCXX_INCLUDE_DIRS
                  UPCXX_LIBRARIES UPCXX_DEFINITIONS UPCXX_CXX_STANDARD
                  UPCXX_OPTIONS UPCXX_LINK_OPTIONS UPCXX_COMPATIBLE_COMPILER)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args( UPCXX
  REQUIRED_VARS UPCXX_META_EXECUTABLE UPCXX_LIBRARIES UPCXX_INCLUDE_DIRS
                UPCXX_DEFINITIONS 
                UPCXX_COMPATIBLE_COMPILER
  VERSION_VAR UPCXX_VERSION
  HANDLE_COMPONENTS)

message(STATUS "This UPC++ install requires the c++${UPCXX_CXX_STANDARD} standard.")
message(STATUS "UPCXX_NETWORK=${UPCXX_NETWORK}")
message(STATUS "UPCXX_THREADMODE=$ENV{UPCXX_THREADMODE}")
message(STATUS "UPCXX_CODEMODE=$ENV{UPCXX_CODEMODE}")

# Export a UPCXX::upcxx target for modern cmake projects
if( UPCXX_FOUND AND NOT TARGET UPCXX::upcxx )
  add_library( UPCXX::upcxx INTERFACE IMPORTED )
  # Handle various CMake version dependencies
  # CMake older than 3.8 lacks cxx_std_* features
  # CMake 3.8..3.11 max is cxx_std_17
  if (  (NOT CMAKE_VERSION VERSION_LESS 3.8.0 AND NOT UPCXX_CXX_STANDARD GREATER "17") 
     OR (NOT CMAKE_VERSION VERSION_LESS 3.12.0 AND NOT UPCXX_CXX_STANDARD GREATER "20") 
     )
    set_property(TARGET UPCXX::upcxx PROPERTY
      INTERFACE_COMPILE_FEATURES  "cxx_std_${UPCXX_CXX_STANDARD}"
    )
  elseif(DEFINED UPCXX_CXX_STD_FLAG)
    UPCXX_VERB("UPCXX_CXX_STD_FLAG=${UPCXX_CXX_STD_FLAG}")
    list( APPEND UPCXX_OPTIONS ${UPCXX_CXX_STD_FLAG})
  endif()
  if (CMAKE_VERSION VERSION_LESS 3.13.0) 
     # INTERFACE_LINK_OPTIONS ignored prior to cmake 3.13.0, so smuggle it here
     # For 3.12, must also undo SHELL: which is not correctly handled by INTERFACE_LINK_LIBRARIES
     string(REGEX REPLACE "SHELL:" "" UPCXX_LINK_OPTIONS_cleaned "${UPCXX_LINK_OPTIONS}")
     # Would like these options to be conditional on COMPILE_LANGUAGE:CXX, but that is sadly prohibited by cmake
     set(UPCXX_LIBRARIES "${UPCXX_LINK_OPTIONS_cleaned} ${UPCXX_LIBRARIES}")
     string(STRIP "${UPCXX_LIBRARIES}" UPCXX_LIBRARIES)
     unset(UPCXX_LINK_OPTIONS_cleaned)
  endif()
  set_target_properties( UPCXX::upcxx PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${UPCXX_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES      "${UPCXX_LIBRARIES}"
    INTERFACE_COMPILE_DEFINITIONS "${UPCXX_DEFINITIONS}"
    # generators ensure that potentially proprietary compiler options 
    # are only added when invoking the CXX compiler frontend:
    INTERFACE_LINK_OPTIONS        "$<$<COMPILE_LANGUAGE:CXX>:${UPCXX_LINK_OPTIONS}>"
    INTERFACE_COMPILE_OPTIONS     "$<$<COMPILE_LANGUAGE:CXX>:${UPCXX_OPTIONS}>"
    )
  UPCXX_VERB( "UPCXX_INCLUDE_DIRS: ${UPCXX_INCLUDE_DIRS}" )
  UPCXX_VERB( "UPCXX_DEFINITIONS:  ${UPCXX_DEFINITIONS}" )
  UPCXX_VERB( "UPCXX_OPTIONS:      ${UPCXX_OPTIONS}" )
  UPCXX_VERB( "UPCXX_LINK_OPTIONS: ${UPCXX_LINK_OPTIONS}" )
  UPCXX_VERB( "UPCXX_LIBRARIES:    ${UPCXX_LIBRARIES}" )
  set(UPCXX_LIBRARIES UPCXX::upcxx)
endif()

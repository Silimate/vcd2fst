cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# Set the project and version
# Version changes whenever some new features accumulated, or the
# grammar or the cache format changes to make sure caches
# are invalidated.
project(VCDHELPERS VERSION 0.1)


# Detect build type, fallback to release and throw a warning if use didn't
# specify any
if(NOT CMAKE_BUILD_TYPE)
  message(WARNING "Build type not set, falling back to Release mode.
 To specify build type use:
 -DCMAKE_BUILD_TYPE=<mode> where <mode> is Debug or Release.")
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE STRING "Choose the type of build, options are: Debug Release."
            FORCE)
endif(NOT CMAKE_BUILD_TYPE)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS_DEBUG
"${CMAKE_CXX_FLAGS_DEBUG} -Wall -g -fpermissive"
)
set(CMAKE_CXX_FLAGS_RELEASE
"${CMAKE_CXX_FLAGS_RELEASE} -Wall -O3 -g -fpermissive"
)

set_source_files_properties(vcd2fst.c fstapi.c fastlz.c lz4.c jrb.c PROPERTIES LANGUAGE CXX)

if (UNIX AND NOT APPLE)
  set (STACK_DETAILS_AUTO_DETECT FALSE CACHE BOOL "Auto detect backward's stack details dependencies")
  set (STACK_DETAILS_DW TRUE CACHE BOOL "Use libdw to read debug info")
endif()
add_subdirectory(third_party/backward-cpp)

set(vcd2fst_SRC
  ${PROJECT_SOURCE_DIR}/vcd2fst.c
  ${PROJECT_SOURCE_DIR}/fstapi.c
  ${PROJECT_SOURCE_DIR}/fastlz.c
  ${PROJECT_SOURCE_DIR}/lz4.c
  ${PROJECT_SOURCE_DIR}/jrb.c
  ${PROJECT_SOURCE_DIR}/third_party/backward-cpp/backward.cpp)

add_executable(vcd2fst-bin ${vcd2fst_SRC})
set_target_properties(vcd2fst-bin PROPERTIES OUTPUT_NAME vcd2fst)

target_include_directories(vcd2fst-bin PUBLIC .)
target_link_libraries(vcd2fst-bin PUBLIC z)
target_link_libraries(vcd2fst-bin PUBLIC Backward::Interface)
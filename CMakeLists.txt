cmake_minimum_required(VERSION 3.16)
project(openppl-ls LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# tree-sitter (the runtime library, not a grammar) and yyjson are fetched
# and built from source rather than found via pkg-config, so this builds
# the same way on macOS, Linux and Windows with nothing but a C/C++
# compiler and CMake, no system package manager, Homebrew formula or
# vcpkg port required. Pinned to the exact versions this project has been
# built and tested against (confirmed via `brew list --versions` on the
# machine this was last verified on), same pattern as
# phh-ls/CMakeLists.txt.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(tree_sitter_runtime
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG v0.27.0
)
FetchContent_Declare(yyjson
  GIT_REPOSITORY https://github.com/ibireme/yyjson.git
  GIT_TAG 0.12.0
)
FetchContent_MakeAvailable(tree_sitter_runtime yyjson)

set(TREE_SITTER_OPENPPL_DIR ${CMAKE_SOURCE_DIR}/../tree-sitter-openppl)

add_library(openppl_grammar STATIC
  ${TREE_SITTER_OPENPPL_DIR}/src/parser.c
)
target_include_directories(openppl_grammar PUBLIC
  ${TREE_SITTER_OPENPPL_DIR}/src
)

# Registry argument now exists (openppl/registry/symbols.json), unlike
# this server's first release, it now takes one, same shape as phh-ls/ohh-ls.
add_executable(openppl-ls
  src/registry.cpp
  src/document.cpp
  src/validate.cpp
  src/jsonrpc.cpp
  src/main.cpp
)
target_include_directories(openppl-ls PRIVATE ${TREE_SITTER_OPENPPL_DIR}/src)
target_link_libraries(openppl-ls PRIVATE openppl_grammar tree-sitter yyjson)

add_executable(openppl-check
  src/registry.cpp
  src/document.cpp
  src/validate.cpp
  src/check_main.cpp
)
target_include_directories(openppl-check PRIVATE ${TREE_SITTER_OPENPPL_DIR}/src)
target_link_libraries(openppl-check PRIVATE openppl_grammar tree-sitter yyjson)

cmake_minimum_required(VERSION 3.16)
project(openppl-ls LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(TREE_SITTER REQUIRED tree-sitter)
pkg_check_modules(YYJSON REQUIRED yyjson)

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
target_compile_options(openppl-ls PRIVATE ${TREE_SITTER_CFLAGS_OTHER} ${YYJSON_CFLAGS_OTHER})
target_include_directories(openppl-ls PRIVATE ${TREE_SITTER_INCLUDE_DIRS} ${YYJSON_INCLUDE_DIRS})
target_link_directories(openppl-ls PRIVATE ${TREE_SITTER_LIBRARY_DIRS} ${YYJSON_LIBRARY_DIRS})
target_link_libraries(openppl-ls PRIVATE openppl_grammar ${TREE_SITTER_LIBRARIES} ${YYJSON_LIBRARIES})

add_executable(openppl-check
  src/registry.cpp
  src/document.cpp
  src/validate.cpp
  src/check_main.cpp
)
target_include_directories(openppl-check PRIVATE ${TREE_SITTER_OPENPPL_DIR}/src)
target_compile_options(openppl-check PRIVATE ${TREE_SITTER_CFLAGS_OTHER} ${YYJSON_CFLAGS_OTHER})
target_include_directories(openppl-check PRIVATE ${TREE_SITTER_INCLUDE_DIRS} ${YYJSON_INCLUDE_DIRS})
target_link_directories(openppl-check PRIVATE ${TREE_SITTER_LIBRARY_DIRS} ${YYJSON_LIBRARY_DIRS})
target_link_libraries(openppl-check PRIVATE openppl_grammar ${TREE_SITTER_LIBRARIES} ${YYJSON_LIBRARIES})

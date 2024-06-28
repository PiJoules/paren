project(Paren)
cmake_minimum_required(VERSION 3.22)

set(CLANG_FORMAT clang-format CACHE STRING "clang-format to use")
message(STATUS "CLANG_FORMAT: ${CLANG_FORMAT}")

set(LLVM_CONFIG llvm-config CACHE STRING "llvm-config to use")
message(STATUS "LLVM_CONFIG: ${LLVM_CONFIG}")

set(FILECHECK FileCheck CACHE STRING "FileCheck to use")
message(STATUS "FILECHECK: ${FILECHECK}")

execute_process(COMMAND ${LLVM_CONFIG} --cxxflags OUTPUT_VARIABLE LLVM_CONFIG_CXX_FLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --ldflags OUTPUT_VARIABLE LLVM_CONFIG_LD_FLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --system-libs OUTPUT_VARIABLE LLVM_CONFIG_SYSTEM_LIBS
  OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${LLVM_CONFIG} --libs core analysis x86 passes OUTPUT_VARIABLE LLVM_CONFIG_LIBS
  OUTPUT_STRIP_TRAILING_WHITESPACE)

separate_arguments(LLVM_CONFIG_CXX_FLAGS)
separate_arguments(LLVM_CONFIG_LD_FLAGS)
separate_arguments(LLVM_CONFIG_SYSTEM_LIBS)
separate_arguments(LLVM_CONFIG_LIBS)

message(STATUS "LLVM_CONFIG_CXX_FLAGS: ${LLVM_CONFIG_CXX_FLAGS}")
message(STATUS "LLVM_CONFIG_LD_FLAGS: ${LLVM_CONFIG_LD_FLAGS}")
message(STATUS "LLVM_CONFIG_SYSTEM_LIBS: ${LLVM_CONFIG_SYSTEM_LIBS}")
message(STATUS "LLVM_CONFIG_LIBS: ${LLVM_CONFIG_LIBS}")

include_directories(AFTER ${CMAKE_SOURCE_DIR})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_CXX_EXTENSIONS Off)

add_compile_options(
  -g3

  -Wall -Werror -Wconversion

  -ftrivial-auto-var-init=pattern

  -fno-exceptions -fno-rtti
  -ffunction-sections -fdata-sections

  ${LLVM_CONFIG_CXX_FLAGS}

  -fsanitize=address -fsanitize=undefined)

add_link_options(
  -Wl,--gc-sections

  -fsanitize=address -fsanitize=undefined)

add_library(argparse INTERFACE EXCLUDE_FROM_ALL argparse.h)

add_library(paren STATIC libparen.cpp libparen.h)
target_compile_options(paren PRIVATE -fPIC)

add_executable(paren_exe paren.cpp)
target_link_libraries(paren_exe paren
  argparse
  ${LLVM_CONFIG_LD_FLAGS}
  ${LLVM_CONFIG_SYSTEM_LIBS}
  ${LLVM_CONFIG_LIBS}
)
set_property(TARGET paren_exe PROPERTY OUTPUT_NAME paren)

# Copy library.paren into the build dir so we can reference it in tests.
configure_file(library.paren library.paren)

# TESTS
add_subdirectory(tests)

# FORMATTING
file(GLOB all_srcs CONFIGURE_DEPENDS "*.h" "*.cpp")
add_custom_target(format
  COMMAND ${CLANG_FORMAT} --style=file -i ${all_srcs}
  COMMAND python3 -m black ${CMAKE_SOURCE_DIR} --include tests/lit.site.cfg.py.in)
add_custom_target(format-check COMMAND ${CLANG_FORMAT} --style=file ${all_srcs} --dry-run -Werror)

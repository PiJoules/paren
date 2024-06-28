include(FetchContent)
FetchContent_Declare(
  googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(FILECHECK FileCheck CACHE STRING "FileCheck to use")
message(STATUS "FileCheck: ${FileCheck}")

add_executable(unittests EXCLUDE_FROM_ALL
  testargparse.cpp)
target_link_libraries(unittests
  argparse
  GTest::gtest_main)

configure_file(lit.site.cfg.py.in lit.site.cfg.py @ONLY)

add_custom_target(check
  COMMAND echo "Running unittests" && ./unittests
  COMMAND echo "Running lit tests" && python3 ${CMAKE_CURRENT_SOURCE_DIR}/run-lit.py ${CMAKE_CURRENT_BINARY_DIR} -v
  DEPENDS paren_exe paren unittests)

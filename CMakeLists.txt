cmake_minimum_required(VERSION 3.20)
project(c23pp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(C23PP_BUILD_TESTS "Build test suite" ON)

# Third-party: cpp-peglib (vendored in third_party/)
add_library(peglib INTERFACE)
target_include_directories(peglib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Core library
add_library(c23pp_lib STATIC
    src/string_pool.cpp
    src/lexer.cpp
    src/preprocessor.cpp
    src/source_manager.cpp
)
target_include_directories(c23pp_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(c23pp_lib PUBLIC peglib)
target_compile_options(c23pp_lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wno-unused-parameter>
)

# Executable
add_executable(c23pp src/main.cpp)
target_link_libraries(c23pp PRIVATE c23pp_lib)
target_compile_options(c23pp PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wno-unused-parameter>
)
# Link pthreads for multithreaded support
find_package(Threads REQUIRED)
target_link_libraries(c23pp_lib PUBLIC Threads::Threads)

# Tests
if(C23PP_BUILD_TESTS)
    enable_testing()
    add_executable(c23pp_tests
        tests/test_main.cpp
        tests/test_string_pool.cpp
        tests/test_lexer.cpp
        tests/test_preprocessor.cpp
    )
    target_link_libraries(c23pp_tests PRIVATE c23pp_lib)
    target_compile_options(c23pp_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wno-unused-parameter>
        $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wno-unused-parameter>
    )
    add_test(NAME c23pp_tests COMMAND c23pp_tests)
endif()

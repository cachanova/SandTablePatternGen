cmake_minimum_required(VERSION 3.16)
project(ThrGenCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

option(THRGEN_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(ThrGenCore
    src/EdgeDetector.cpp
    src/PathPlanner.cpp
    src/ThrGenerator.cpp
    src/ThrPreview.cpp
    src/stb_impl.cpp
)
target_include_directories(ThrGenCore
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(ThrGenCore PUBLIC Threads::Threads)

add_executable(ThrGenCpp src/main.cpp)
target_link_libraries(ThrGenCpp PRIVATE ThrGenCore)

add_executable(ThrGenCLI src/cli.cpp)
target_link_libraries(ThrGenCLI PRIVATE ThrGenCore)

add_executable(ThrGenCpp_Tests
    tests/main.cpp
    tests/test_EdgeDetector.cpp
    tests/test_PathPlanner.cpp
    tests/test_ThrGenerator.cpp
    tests/test_Integration.cpp
)
target_link_libraries(ThrGenCpp_Tests PRIVATE ThrGenCore)

set(THRGEN_TARGETS ThrGenCore ThrGenCpp ThrGenCLI ThrGenCpp_Tests)
foreach(target IN LISTS THRGEN_TARGETS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(THRGEN_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endforeach()

# The vendored stb implementation is intentionally outside our warning policy.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

enable_testing()
add_test(NAME ThrGenCpp_Tests COMMAND ThrGenCpp_Tests)
set_tests_properties(ThrGenCpp_Tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME ThrGenTransfer_Tests COMMAND "${NODE_EXECUTABLE}" --test tests/test_transfer.cjs)
    set_tests_properties(ThrGenTransfer_Tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

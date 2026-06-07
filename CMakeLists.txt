cmake_minimum_required(VERSION 3.31)
project(HelelaniRover)

set(CMAKE_CXX_STANDARD 23)

# Add this to disable C++20 module scanning and fix the Ninja '=' error
#set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# for .clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define your strict rules based on the compiler
if (MSVC)
    # Microsoft Visual C++ specific pedantic flags
    set(PEDANTIC_COMPILER_FLAGS /W4 /WX)
else ()
    # GCC/Clang specific pedantic flags
    set(PEDANTIC_COMPILER_FLAGS -Wall -Wextra -Wpedantic)
endif ()

# Global Saucer Setup
include(FetchContent)
FetchContent_Declare(
        saucer
        GIT_REPOSITORY https://github.com/saucer/saucer.git
        GIT_TAG master
)
FetchContent_MakeAvailable(saucer)

# Orchestrate sub-projects
add_subdirectory(shared_xmpp)
add_subdirectory(rover)
add_subdirectory(client)
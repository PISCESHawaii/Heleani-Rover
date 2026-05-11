cmake_minimum_required(VERSION 3.31)
project(HelelaniRover)

set(CMAKE_CXX_STANDARD 23)

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
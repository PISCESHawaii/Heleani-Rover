add_executable(rover_daemon main.cpp)

target_compile_options(rover_daemon PRIVATE ${PEDANTIC_COMPILER_FLAGS})

# Just link the internal wrapper; headers are handled automatically
target_link_libraries(rover_daemon PRIVATE shared_xmpp)
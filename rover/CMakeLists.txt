add_executable(rover_daemon main.cpp)

# Just link the internal wrapper; headers are handled automatically
target_link_libraries(rover_daemon PRIVATE shared_xmpp)
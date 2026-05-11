# 1. Force CMake to find the .a file specifically
find_library(STROPHE_STATIC_LIB
        NAMES libstrophe.a
        HINTS "$ENV{HOME}/.local/lib" "/usr/lib64" "/usr/lib/arm-linux-gnueabihf"
)

# 2. Libstrophe depends on an XML parser and SSL
# These are usually on the rover, but for safety, we find them too
find_package(PkgConfig REQUIRED)
pkg_check_modules(EXPAT REQUIRED expat)
pkg_check_modules(OPENSSL REQUIRED openssl)

add_library(shared_xmpp STATIC
        libstrophe_cpp.cpp
        xmpp_node.cpp
)

target_include_directories(shared_xmpp PUBLIC .)
target_include_directories(shared_xmpp PRIVATE "$ENV{HOME}/.local/include")

# 3. Link the static strophe file directly
# We also link its dependencies (expat/ssl) so the linker is happy
target_link_libraries(shared_xmpp PUBLIC
        ${STROPHE_STATIC_LIB}
        ${EXPAT_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        pthread
        resolv
)
# 1. Force CMake to find the .a/.lib file specifically
find_library(STROPHE_STATIC_LIB
        # Added strophe.lib to the search names
        NAMES strophe.lib strophe libstrophe.a
        # Added the local install folder we will create in the GitHub Action
        HINTS "$ENV{HOME}/.local/lib" "/usr/lib64" "/usr/lib/arm-linux-gnueabihf" "${CMAKE_SOURCE_DIR}/strophe_install/lib"
)

# 2. Libstrophe depends on an XML parser and SSL
# These are usually on the rover, but for safety, we find them too
find_package(PkgConfig REQUIRED)
find_package(LibXml2 REQUIRED)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(ZLIB REQUIRED zlib)

add_library(shared_xmpp STATIC
        libstrophe_cpp.cpp
        xmpp_node.cpp
)

target_include_directories(shared_xmpp PUBLIC .)
target_include_directories(shared_xmpp PUBLIC "$ENV{HOME}/.local/include")

target_compile_options(shared_xmpp PRIVATE ${PEDANTIC_COMPILER_FLAGS})

# 3. Link the static strophe file directly
# We also link its dependencies so the linker is happy
if (WIN32)
    # Windows native socket library instead of resolv
    target_link_libraries(shared_xmpp PUBLIC
            ${STROPHE_STATIC_LIB}
            ${LIBXML2_LIBRARIES}
            ${OPENSSL_LIBRARIES}
            ${ZLIB_LIBRARIES}
            ws2_32
    )
else ()
    # Your original Linux linking
    target_link_libraries(shared_xmpp PUBLIC
            ${STROPHE_STATIC_LIB}
            ${LIBXML2_LIBRARIES}
            ${OPENSSL_LIBRARIES}
            ${ZLIB_LIBRARIES}
            pthread
            resolv
    )
endif ()
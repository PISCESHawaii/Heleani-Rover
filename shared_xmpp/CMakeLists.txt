if (WIN32)
    # --- WINDOWS PATH (MSVC + VCPKG) ---

    find_package(LibXml2 REQUIRED)
    find_package(ZLIB REQUIRED)

    # 2. Download libstrophe source directly into the build folder
    include(FetchContent)
    FetchContent_Declare(
            libstrophe_src
            GIT_REPOSITORY https://github.com/strophe/libstrophe.git
            GIT_TAG 419b616
    )
    FetchContent_MakeAvailable(libstrophe_src)

    # 2. Swap the excluded TLS file to drop OpenSSL and KEEP Schannel
    file(GLOB STROPHE_SOURCES "${libstrophe_src_SOURCE_DIR}/src/*.c")
    list(REMOVE_ITEM STROPHE_SOURCES
            "${libstrophe_src_SOURCE_DIR}/src/parser_expat.c"
            "${libstrophe_src_SOURCE_DIR}/src/tls_gnutls.c"
            "${libstrophe_src_SOURCE_DIR}/src/tls_openssl.c"  # <--- DROP OPENSSL
            "${libstrophe_src_SOURCE_DIR}/src/tls_dummy.c"
            "${libstrophe_src_SOURCE_DIR}/src/compression_dummy.c"
    )

    add_library(strophe_static STATIC ${STROPHE_SOURCES})
    set_target_properties(strophe_static PROPERTIES C_STANDARD 11)
    target_include_directories(strophe_static PUBLIC "${libstrophe_src_SOURCE_DIR}")

    # 3. Removed HAVE_OPENSSL=1
    target_compile_definitions(strophe_static PRIVATE
            __LITTLE_ENDIAN__=1
            __BYTE_ORDER__=1234
            __ORDER_LITTLE_ENDIAN__=1234
            HAVE_ZLIB=1
    )

    # 4. Swap OpenSSL linking for crypt32
    target_link_libraries(strophe_static PUBLIC
            LibXml2::LibXml2
            ZLIB::ZLIB
            ws2_32
            secur32
            crypt32  # <--- Schannel relies on this
    )

    # 5. Build YOUR code and link it to our custom strophe build
    add_library(shared_xmpp STATIC
            libstrophe_cpp.cpp
            xmpp_node.cpp
    )
    target_include_directories(shared_xmpp PUBLIC .)
    target_compile_options(shared_xmpp PRIVATE ${PEDANTIC_COMPILER_FLAGS})
    target_link_libraries(shared_xmpp PUBLIC strophe_static)

else ()
    # --- FEDORA / LINUX PATH ---
    # (This remains entirely untouched so your local development doesn't break)

    find_library(STROPHE_STATIC_LIB
            NAMES libstrophe.a
            HINTS "$ENV{HOME}/.local/lib" "/usr/lib64" "/usr/lib/arm-linux-gnueabihf"
    )

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

    target_link_libraries(shared_xmpp PUBLIC
            ${STROPHE_STATIC_LIB}
            ${LIBXML2_LIBRARIES}
            ${OPENSSL_LIBRARIES}
            ${ZLIB_LIBRARIES}
            pthread
            resolv
    )
endif ()

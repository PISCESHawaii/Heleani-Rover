# shared_xmpp: Helelani Rover XMPP Wrapper

The `shared_xmpp` library provides a robust, modern C++ wrapper
around [libstrophe](https://github.com/strophe/libstrophe). It serves as the primary communication bridge between the
Helelani Rover hardware daemon and the operator client UI, abstracting away the underlying C library's memory management
and pointer logic into safe, easy-to-use C++ classes.

This code has been hard forked off
from [a testing repository](https://github.com/jjj333-p/libstrophe-cpp/tree/18f88a4de17b94e89e2f320bf3889432c1a4c075).

## Overview

This module is designed to handle XMPP connections, stanza routing, and Info/Query (IQ) request/response patterns
essential for the rover's telemetry and command infrastructure. By statically linking `libstrophe` alongside its
dependencies, `shared_xmpp` guarantees a portable, consistent communication interface across both the headless rover
daemon and the cross-platform Saucer webview client.

## Core Components

### `libstrophe_cpp`

The central connection manager and event router.

- **Connection Management:** Handles initialization, credentials (JID/password), and the main `libstrophe` event loop
  via `connect_noexcept()`. It utilizes success and failure callbacks to gracefully alert the parent application of
  connection state changes.
- **Pattern Matching Router:** Replaces standard broad stanza catching with a tuple-based `HandlerCriteria` (Namespace,
  Name, Type). This allows developers to route specific incoming XML stanzas to dedicated lambda functions without
  writing monolithic `if/else` chains.
- **IQ Handler:** Dedicated methods (`set_iq_handler` and `send_iq`) manage XMPP IQs, assigning sequence IDs and
  automatically routing asynchronous responses back to the originating caller.

### `XmppNode`

A lightweight, DOM-style class representing an XMPP stanza's hierarchical structure.

- **Abstracts C-Pointers:** Parses raw `xmpp_stanza_t` pointers into a safe C++ object containing `name`, `attributes`,
  `text_content`, and a vector of `children`.
- **Bidirectional Conversion:** Can parse incoming `libstrophe` stanzas (`from_libstrophe`) and serialize modified nodes
  back to raw stanzas for transmission (`to_libstrophe`).
- **Traversal:** Provides utility functions like `find_child` and `find_all` to quickly navigate the XML tree.

### `make_iq_query` (`xmpp_iq.h`)

A utility header for generating standardized IQ requests. It implements a thread-safe atomic sequence generator (
`get_next_iq_id()`) to ensure every outgoing packet has a unique identifier, which is crucial for mapping asynchronous
responses.

## Communication Pattern: IQ Requests

Instead of relying on bare message stanzas, the Helelani project utilizes XMPP Info/Query (IQ) requests, acting
similarly to HTTP GET/POST methods.

1. **Client to Rover:** The client sends an IQ request, such as `rover::getopts`, via `send_iq()`.
2. **Rover Processing:** The rover catches this using a registered handler (`set_iq_handler`), processes the command,
   and returns the requested data, such as UI command definitions or camera stream URLs.
3. **Telemetry Heartbeat:** The rover periodically dispatches `rover::telemetry` IQs. If the client fails to respond,
   the connection is considered degraded, and safety pauses are engaged.

## Code Layout and Usage

The `shared_xmpp` module is intentionally small. It provides a C++-friendly layer over `libstrophe` while keeping the
actual XMPP protocol flow explicit in the client and rover code.

At a high level:

- `libstrophe_cpp` owns the XMPP connection and event loop.
- `XmppNode` represents XML/XMPP stanzas as normal C++ objects.
- `xmpp_iq.h` provides helpers for constructing IQ requests.
- Consumers register handlers for incoming IQs and use `send_iq(...)` for request/response-style communication.

### File Layout

- **`libstrophe_cpp.h` / `libstrophe_cpp.cpp`**: Main wrapper around `libstrophe`.
  - Creates and owns the `xmpp_ctx_t` and `xmpp_conn_t`.
  - Stores the login JID/password and derives the localpart/domain from the JID.
  - Runs the `libstrophe` event loop through `connect_noexcept(...)`.
  - Provides `disconnect()` for safe shutdown.
  - Provides `send(...)` for raw stanza sending.
  - Provides `send_iq(...)` for IQ requests that expect a response.
  - Provides `set_iq_handler(...)` for incoming IQ requests that should produce a response.
  - Maintains an outgoing queue so stanzas can be queued from other threads and sent on the XMPP event loop thread.

- **`xmpp_node.h` / `xmpp_node.cpp`**: Lightweight tree representation of an XMPP stanza.
  - Stores the stanza/tag name in `name`.
  - Stores XML attributes in `attributes`.
  - Stores text content in `text_content`.
  - Stores nested child nodes in `children`.
  - Converts incoming `xmpp_stanza_t*` values into safe C++ objects with `XmppNode::from_libstrophe(...)`.
  - Converts C++ `XmppNode` objects back into libstrophe stanzas with `to_libstrophe(...)`.
  - Provides helper traversal methods like `find_child(...)` and `find_all(...)`.

- **`xmpp_iq.h`**: Small helper header for IQ stanza construction.
  - Generates unique IQ IDs with `get_next_iq_id()`.
  - Creates standard IQ/query node structures with `make_iq_query(...)`.

- **`CMakeLists.txt`**: Builds `shared_xmpp` as a static library.
  - On Linux, links against an existing `libstrophe.a` and system dependencies.
  - On Windows, builds a local static `libstrophe` target and links it against Windows Schannel-related libraries.

### Basic Usage Flow

A typical consumer of `shared_xmpp` follows this pattern:

1. Construct a `libstrophe_cpp` instance with a log level, JID, and password.
2. Register any incoming IQ handlers before or after connecting, depending on the use case.
3. Run `connect_noexcept(...)` on a background thread, because it owns the blocking XMPP event loop.
4. Use `send_iq(...)` for request/response messages.
5. Use `set_iq_handler(...)` for messages where this program needs to respond to another peer.
6. Call `disconnect()` when shutting down or replacing the connection.

## Build Requirements & CMake Integration

The wrapper is compiled as a static library (`shared_xmpp`) and linked to the main executables.

The build has separate dependency strategies for Linux and Windows:

- **Linux / Fedora / Raspberry Pi style builds:** Prefer an existing static `libstrophe.a` installation and use OpenSSL
  for TLS.
- **Windows / MSVC builds:** Build `libstrophe` from source during CMake configuration and use Windows Schannel instead
  of OpenSSL.

### Common Requirements

- CMake 3.31 or newer
- C++23 compiler
- `libxml2`
- `zlib`

### Linux Dependencies

On Linux, `shared_xmpp` expects `libstrophe` to already be available as a static archive.

Expected search locations include:

- `~/.local/lib`
- `/usr/lib64`
- `/usr/lib/arm-linux-gnueabihf`

Linux builds also require:

- `OpenSSL`
- `pthread`
- `resolv`
- `pkg-config`

The Linux build path links against:

- `libstrophe.a`
- `libxml2`
- `OpenSSL`
- `zlib`
- `pthread`
- `resolv`

Ensure development headers and static archives for `libstrophe`, `libxml2`, `zlib`, and `openssl` are installed on the
build machine prior to compilation.

### Windows Dependencies

On Windows, the project uses MSVC and vcpkg-provided dependencies for `libxml2` and `zlib`.

The Windows CMake path downloads `libstrophe` source during configuration using `FetchContent`, then builds a
project-local static `strophe_static` target.

Unlike the Linux build, the Windows build does **not** use OpenSSL. Instead, it uses the native Windows TLS stack via
Schannel.

The Windows build links against:

- `LibXml2::LibXml2`
- `ZLIB::ZLIB`
- `ws2_32`
- `secur32`
- `crypt32`

This keeps the Windows build simpler by avoiding a separate OpenSSL dependency while still supporting secure XMPP stream
negotiation.

## CMake Usage

When linking `shared_xmpp` to the `client` or `rover` targets, the module automatically forwards its include directories
and dependencies via `PUBLIC` linkage.

```cmake
# Example inclusion in another target
target_link_libraries(my_target PUBLIC shared_xmpp)
```

The root CMake project orchestrates the subprojects and makes `shared_xmpp` available to both the rover daemon and the
client application.

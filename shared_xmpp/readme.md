# shared_xmpp: Helelani Rover XMPP Wrapper

The `shared_xmpp` library provides a robust, modern C++ wrapper around [libstrophe](https://github.com/strophe/libstrophe). It serves as the primary communication bridge between the Helelani Rover hardware daemon and the operator client UI, abstracting away the underlying C library's memory management and pointer logic into safe, easy-to-use C++ classes.

This code has been hard forked off from [a testing repository](https://github.com/jjj333-p/libstrophe-cpp/tree/18f88a4de17b94e89e2f320bf3889432c1a4c075).

## Overview

This module is designed to handle XMPP connections, stanza routing, and Info/Query (IQ) request/response patterns essential for the rover's telemetry and command infrastructure. By statically linking `libstrophe` alongside its dependencies, `shared_xmpp` guarantees a portable, consistent communication interface across both the headless rover daemon and the cross-platform Saucer webview client.

## Core Components

### `libstrophe_cpp`
The central connection manager and event router. 
- **Connection Management:** Handles initialization, credentials (JID/password), and the main `libstrophe` event loop via `connect_noexcept()`. It utilizes success and failure callbacks to gracefully alert the parent application of connection state changes.
- **Pattern Matching Router:** Replaces standard broad stanza catching with a tuple-based `HandlerCriteria` (Namespace, Name, Type). This allows developers to route specific incoming XML stanzas to dedicated lambda functions without writing monolithic `if/else` chains.
- **IQ Handler:** Dedicated methods (`set_iq_handler` and `send_iq`) manage XMPP IQs, assigning sequence IDs and automatically routing asynchronous responses back to the originating caller.

### `XmppNode`
A lightweight, DOM-style class representing an XMPP stanza's hierarchical structure.
- **Abstracts C-Pointers:** Parses raw `xmpp_stanza_t` pointers into a safe C++ object containing `name`, `attributes`, `text_content`, and a vector of `children`.
- **Bidirectional Conversion:** Can parse incoming `libstrophe` stanzas (`from_libstrophe`) and serialize modified nodes back to raw stanzas for transmission (`to_libstrophe`).
- **Traversal:** Provides utility functions like `find_child` and `find_all` to quickly navigate the XML tree.

### `make_iq_query` (xmpp_iq.h)
A utility header for generating standardized IQ requests. It implements a thread-safe atomic sequence generator (`get_next_iq_id()`) to ensure every outgoing packet has a unique identifier, crucial for mapping asynchronous responses.

## Communication Pattern (IQ Requests)

Instead of relying on bare message stanzas, the Helelani project utilizes XMPP Info/Query (IQ) requests, acting similarly to HTTP GET/POST methods. 

1. **Client to Rover:** The client sends an IQ request (e.g., `rover::getopts`) via `send_iq()`. 
2. **Rover Processing:** The rover catches this using a registered handler (`set_iq_handler`), processes the command, and returns the requested data (such as UI command definitions or camera stream URLs).
3. **Telemetry Heartbeat:** The rover periodically dispatches `rover::telemetry` IQs. If the client fails to respond, the connection is considered degraded, and safety pauses are engaged.

## Build Requirements & CMake Integration

The wrapper is compiled as a static library (`shared_xmpp`) and linked to the main executables.

### Dependencies
To ensure portability across Linux environments (such as Raspbian on the rover), this module aggressively prefers static linking:
- **libstrophe:** Linked statically (`libstrophe.a`). Expected in `~/.local/lib` or system library paths.
- **libxml2:** Used as the underlying XML parser.
- **zlib:** Required by libstrophe for stream compression.
- **OpenSSL:** Required for secure stream negotiation.

### CMake Usage
When linking `shared_xmpp` to the `client` or `rover` targets, the module automatically forwards its include directories and dependencies via `PUBLIC` linkage.

```cmake
# Example inclusion in the root project
target_link_libraries(my_target PUBLIC shared_xmpp)
```

Ensure development headers and static archives for `libstrophe`, `libxml2`, `zlib`, and `openssl` are installed on the build machine prior to compilation.

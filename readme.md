# Helelani Rover Technical Notes

<img width="850" height="650" alt="image" src="https://github.com/user-attachments/assets/08948c8e-a8c3-40a1-b6a7-018aae2fa745" />


This document provides a concise summary of the C++ architecture, dependency chain, and build requirements for the Helelani Rover redesign.

---

## 1. Project Architecture
The repository uses a flat directory structure to simplify navigation and modularize the build targets:

* **`client/`**: Operator interface using the **Saucer** library. It uses native backends (WebKitGTK on Linux, WebView2 on Windows).
* **`rover/`**: Headless daemon for hardware logic.
* **`shared_xmpp/`**: A static wrapper around **libstrophe**. Both the client and rover link to this to ensure consistent communication logic.

---

## 2. Dependency Chain
A root `CMakeLists.txt` orchestrates the build. Dependencies are managed as follows:

* **Saucer**: Fetched automatically via `FetchContent`.
* **XMPP Core**: Uses a local build of `libstrophe` (prioritizing `~/.local`).
* **Parser & Compression**: The current `libstrophe.a` build is configured to use **libxml2** (not Expat) and requires **zlib** for stream compression.

---

## 3. Portability (The "Mostly Static" Build)
To ensure the `rover_daemon` is portable across different Linux environments (like Raspbian), the following linking strategy is used:

* **Statically Linked**: `libstrophe`, `libxml2`, and `zlib` are baked into the binary using `.a` archives to avoid "missing library" errors at runtime.
* **Dynamically Linked**: Core system libraries (`glibc`, `libstdc++`, `openssl`) are linked dynamically to ensure compatibility with the host kernel.

> **Verification**: Run `ldd ./rover_daemon | grep -E "strophe|xml2|z"` to ensure these are not listed as dynamic dependencies.

---

## 4. Environment Setup (Fedora)
The build machine must have both development headers (for compilation) and static archives (for portable linking) installed:

```bash
# Development Headers and Metadata
sudo dnf install libstrophe-devel libxml2-devel zlib-devel openssl-devel

# Static Archives for Portable Binaries
sudo dnf install libxml2-static zlib-static glibc-static libstdc++-static
```

---

## 5. Build Instructions
1.  **Configure**: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
2.  **Compile All**: `cmake --build build`
3.  **Target Specific**: `cmake --build build --target rover_daemon`

---

## 6. Handoff Notes
* **Linker Order**: Ensure `shared_xmpp` is linked before its dependencies (`libxml2`, `zlib`) in `target_link_libraries`.
* **Visibility**: All include paths and library links in `shared_xmpp` must be marked **`PUBLIC`** so that `client` and `rover` targets inherit them automatically.
* **Saucer Backend**: Uses **WebKitGTK** on Linux and **WebView2** on Windows.
* Successors must ensure `libstrophe` is available in `~/.local` or system paths for the wrapper to compile.

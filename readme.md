# Helelani Rover Technical Notes

This document summarizes the unified C++ architecture for the Helelani Rover software redesign.

---

## 1. Project Architecture (Flat Structure)
The codebase is divided into three primary modules to ensure separation of concerns and easy maintenance:

* **`client/`**: Operator interface using **Saucer**. Embeds `webview/` assets directly into the binary.
* **`rover/`**: Headless daemon for hardware logic and XMPP communication.
* **`shared_xmpp/`**: Static library wrapping **libstrophe**. Shared by both `client` and `rover` to maintain a single source of truth for communication logic.

---

## 2. Build System
A root `CMakeLists.txt` orchestrates the entire build.

### Build Command
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Dependency Resolution
* **Saucer**: Fetched automatically via `FetchContent`.
* **libstrophe**: Prioritizes `~/.local` then system paths.
* **Expat**: Required for XMPP parsing; must be installed on the build machine.

---

## 3. Portability & Static Linking
To ensure the `rover_bin` runs on the hardware (Raspbian) without missing libraries, a **"Mostly Static"** strategy is used:

* **Statically Linked**: `libstrophe`, `expat`. Baked into the binary using `.a` archives.
* **Dynamically Linked**: `glibc`, `libstdc++`, `openssl`. Relies on the target OS to provide these for better kernel compatibility.

### Verification
Use `ldd` to confirm `libstrophe` is not a dynamic dependency:
```bash
ldd build/rover/rover_bin | grep strophe
# Should return no results.
```

---

## 4. Environment Setup (Fedora)
Install the following to support both local development and static cross-compilation:

```bash
# Development Headers
sudo dnf install libstrophe-devel expat-devel openssl-devel

# Static Archives for Portability
sudo dnf install expat-static glibc-static libstdc++-static
```

---

## 5. Deployment Notes
* **Saucer Backend**: Uses **WebKitGTK** on Linux and **WebView2** on Windows.
* **Handoff**: Successors must ensure `libstrophe` is available in `~/.local` or system paths for the wrapper to compile.
# Helelani Rover Technical Notes

<img width="850" height="650" alt="image" src="https://github.com/user-attachments/assets/08948c8e-a8c3-40a1-b6a7-018aae2fa745" />


This document provides a concise summary of the C++ architecture, dependency chain, and build requirements for the
Helelani Rover redesign.

---

## Project Architecture

The repository uses a flat directory structure to simplify navigation and modularize the build targets:

* **`client/`**: Operator interface using the **Saucer** library. It uses native backends (WebKitGTK on Linux, WebView2
  on Windows).
* **`rover/`**: Headless daemon for hardware logic.
* **`shared_xmpp/`**: A static wrapper around **libstrophe**. Both the client and rover link to this to ensure
  consistent communication logic.

C++ was chosen as the primary language since most of the hardware libraries are written in C or C++, and it provides a
robust and efficient environment for developing networked applications. Additionally, C++ offers a wide range of
libraries and frameworks that can be leveraged for various tasks, such as graphics, audio, and networking.

### XMPP

This project creates a common C++ wrapper around **libstrophe** to simplify the XMPP communication logic. We use XMPP
Info/Query (IQ) requests akin to HTTP GET/POST to simplify the communication logic. A "connection" is initialized by the
client repeatedly attempting to send a get request with the XML NameSpace (xmlns) set to `rover::getopts`. The rover
catches this request, and responds with a list of command IDs and the pretty name to display in the UI, as well as an
http url to embed a camera webview.

The rover caches the JID (Jabber ID) and will periodically send back `rover::telemetry` set IQs to the client, and if
these IQs are not received and responded to within a certain time, the rover will assume the connection is lost and stop
all activities. The client keeps track of the last time it received a `rover::telemetry` IQ, and will pause the UI and
retry `rover::getopts` until the connection is restored.

More implementation details can be found [here](shared_xmpp/readme.md).

### Client

The client is a relatively simple [Saucer](https://saucer.app/) webview based application. It uses the `shared_xmpp`
libstrophe wrapper to communicate with the rover daemon. A webview was chosen due to the ability to embed a camera
webview in the UI. Any browser compatible webui or video stream can easily be embedded in an iframe. Using a webview
also allows the client to easily be cross-platform. *Generally*, the client is designed to be as dynamic as possible, so
that you can easily add features to the rover without recompiling the client.

Important note, Windows users will need to make sure 
[the latest Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170#latest-supported-redistributable-version)
is installed.

#### Important Note! Sometimes after you edit the webview files, and compile, the CSS or js will get corrupted and break. To fix this, you simply need to restart and compile again, and it will work fine. I am not sure why this happens, but I suspect it is due to some quirk of how saucer injects the webview files, likely as a postbuild script.

More details on the client implementation can be found [here](client/readme.md).

### Rover

Due to the lack of availability of the rover & hardware, currently the rover_daemon is a dummy placeholder that simply
prints out requested commands and sends randomized telemetry. More details on the rover implementation can be
found [here](rover/readme.md).

---

## Build Information

### Dependency Chain

A root `CMakeLists.txt` orchestrates the build. Dependencies are managed as follows:

* **Saucer**: Fetched automatically via `FetchContent`.
* **XMPP Core**: Uses a local build of [libstrophe](https://github.com/strophe/libstrophe) (prioritizing `~/.local`).
* **Parser & Compression**: The current `libstrophe.a` build is configured to use **libxml2** (not Expat) and requires *
  *zlib** for stream compression.

### Portability (The "Mostly Static" Build)

To ensure the `rover_daemon` is portable across different Linux environments (like Raspbian), the following linking
strategy is used:

* **Statically Linked**: `libstrophe`, `libxml2`, and `zlib` are baked into the binary using `.a` archives to avoid "
  missing library" errors at runtime.
* **Dynamically Linked**: Core system libraries (`glibc`, `libstdc++`, `openssl`) are linked dynamically to ensure
  compatibility with the host kernel.

> **Verification**: Run `ldd ./rover_daemon | grep -E "strophe|xml2|z"` to ensure these are not listed as dynamic
> dependencies.

### Environment Setup

The build machine must have both development headers (for compilation) and static archives (for portable linking)
installed. For example, on Fedora:

```bash
# Development Headers and Metadata
sudo dnf install libstrophe-devel libxml2-devel zlib-devel openssl-devel

# Static Archives for Portable Binaries
sudo dnf install libxml2-static zlib-static glibc-static libstdc++-static
```

### Build Instructions

Honestly, I just recommend you open this project with clion, as it automatically handles most of the cmake stuff. If you
have a @hawaii.edu email, you have a free Clion education license, which I highly recommend for easier development.

1. **Configure**: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
2. **Compile All**: `cmake --build build`
3. **Target Specific**: `cmake --build build --target rover_daemon`

I am not a windows user or developer, so I don't know all the nuances of building on windows. I've had success with 
using a Github Actions workflow to build the project on Windows, which you can find [here](.github/workflows/windows.yml).
[Running it](https://github.com/PISCESHawaii/Heleani-Rover/actions/workflows/windows-build.yml) takes 5 ages but it works!
Do note that for the windows build, the [CMakeLists.txt](shared_xmpp/CMakeLists.txt) for the libstrophe wrapper is set to
use  [my fork](https://github.com/jjj333-p/libstrophe/) for libstrophe, as the schannel wrapper was broken and I've 
fixed it. This should be changed back to [The official libstrophe](https://github.com/strophe/libstrophe) once 
[this pr](https://github.com/strophe/libstrophe/pull/270) is resolved. Note that this bundle includes the libraries 
(.dll/.lib) as well as the debug symbols (.pdb).

### Handoff Notes

* **Linker Order**: Ensure `shared_xmpp` is linked before its dependencies (`libxml2`, `zlib`) in
  `target_link_libraries`.
* **Visibility**: All include paths and library links in `shared_xmpp` must be marked **`PUBLIC`** so that `client` and
  `rover` targets inherit them automatically.
* **Saucer Backend**: Uses **WebKitGTK** on Linux and **WebView2** on Windows.
* Successors must ensure [libstrophe](https://github.com/strophe/libstrophe) is available in `~/.local` or system paths
  for the wrapper to compile.

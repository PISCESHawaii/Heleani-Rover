# Controller Client

Webview taken
from [private repo](https://github.com/jamituliao/roverfront-end/tree/4d219bba0a7293ca12b60f1f5fe17352fcde7ec8)
and pivoted to c++ & saucer for more code overlap. Go to [that readme](./webview.md) for more info. It could not be 
included in that directory as anything in `./webview` is embeddeed by saucer. 

The client is a relatively simple [Saucer](https://saucer.app/) webview-based application. It uses the `shared_xmpp`
libstrophe wrapper to communicate with the rover daemon. A webview was chosen due to the ability to embed a camera
webview in the UI. Any browser-compatible webui or video stream can easily be embedded in an iframe. Using a webview
also allows the client to easily be cross-platform. *Generally*, the client is designed to be as dynamic as possible so
that you can hopefully easily add features to the rover without recompiling the client.

***Important Note! Sometimes after you edit the webview files, and compile, the CSS or js will get corrupted and break.
To fix this, you simply need to restart and compile again, and it will work fine. I am not sure why this happens, but I
suspect it is due to some quirk of how saucer injects the webview files, likely as a postbuild script.***

## Project Structure

The controller client is built as the `client_saucer` executable. It is one of the top-level CMake subprojects and
depends on the shared XMPP communication layer used by the rest of the rover system.

At a high level, the client project is organized as follows:

- **`client/`**: Contains the native controller client target and client-specific C++ glue code.
- **`client/webview/`**: Contains the embedded frontend assets used by the client application.
- **`shared_xmpp/`**: Provides the reusable XMPP wrapper around `libstrophe`. The client links against this library for
  rover communication.
- **Root `CMakeLists.txt`**: Fetches common dependencies, configures project-wide compiler options, and adds the client,
  rover, and shared XMPP subdirectories.
- **`.github/workflows/windows-build.yml`**: Defines the manual Windows/MSVC build used to produce the Windows client
  artifact.

The client target links against:

- `saucer::saucer`
- `saucer::embedded`
- `shared_xmpp`

The embedded frontend assets are bundled into the native executable through the Saucer embed step during the CMake
build.

## Code Layout

The client is split into a small native C++ layer and an embedded web frontend. The C++ side owns the native window,
XMPP connection, rover handshake, telemetry monitoring, and command routing. The webview side owns the visible UI and calls
back into C++ through Saucer bindings.

### Native C++ Files

- **`main.cpp`**: Main entry point for the controller client.
    - Creates the Saucer application, native window, and webview.
    - Loads the embedded frontend assets.
    - Exposes C++ functions to JavaScript, including login, command sending, and devtools toggling.
    - Owns the high-level connection lifecycle:
        - user login
        - XMPP connection setup
        - rover options handshake
        - telemetry timeout monitoring
        - reconnection/retry behavior
    - Spawns background threads for blocking or long-running work so the UI thread remains responsive.

- **`conn_wrapper.h`**: Thread-safe holder for the current XMPP client connection.
    - Stores the active `libstrophe_cpp` instance.
    - Replaces old connections when a new login occurs.
    - Disconnects stale clients when they are no longer current.
    - Allows background retry/monitor threads to check whether they are still working with the active connection.

- **`misc_routing.h` / `misc_routing.cpp`**: XMPP routing helpers used by the client.
    - Registers the telemetry IQ handler.
    - Sends the initial rover options request.
    - Parses rover-provided camera feed URLs and command definitions.
    - Sends rover command IQs when UI buttons are clicked.
    - Logs server and rover communication details back into the webview UI.

- **`CMakeLists.txt`**: Defines the `client_saucer` executable.
    - Builds the C++ client files.
    - Embeds the `webview/` directory into the executable.
    - Links against Saucer and `shared_xmpp`.

### Embedded Webview Files

- **`webview/index.html`**: Defines the main UI structure.
    - Login dialog.
    - Top navigation/window controls.
    - Telemetry/status panel.
    - Dynamic rover control button grid.
    - Camera feed area.
    - Command log panel.

- **`webview/js/index.js`**: Browser-side controller logic.
    - Handles login form behavior.
    - Calls C++ bindings exposed through `window.saucer.exposed`.
    - Provides JavaScript functions that C++ calls to update the UI.
    - Dynamically creates rover command buttons.
    - Updates telemetry rows and staleness timers.
    - Manages camera iframes.
    - Handles theme switching, log copying, custom window controls, and dashboard resizing.

- **`webview/stylesheets/rover.css`**: Styling and layout for the embedded UI.
    - Defines light/dark theme variables.
    - Lays out the dashboard columns and panels.
    - Styles telemetry, controls, camera feed, login modal, log box, and window controls.
    - Provides resizable panel/column UI styling.

### C++ / JavaScript Boundary

Saucer is used as the bridge between the native C++ application and the embedded web UI.

JavaScript calls into C++ through exposed bindings such as:

- `Login(jid, password)`
- `SendCommand(command)`
- `toggleDevTools(enabled)`

C++ calls back into JavaScript to update the UI after XMPP events, for example:

- `addLog(...)`
- `markRoverWaiting()`
- `markRoverReachable()`
- `markRoverUnreachable()`
- `clearControlButtons()`
- `addControlButton(...)`
- `updateTelemetry(...)`
- `setCameraIframe(...)`
- `clearAllCameraIframes()`

This keeps most UI rendering logic in JavaScript while keeping XMPP, threading, and native window setup in C++.

### Connection Flow

The general runtime flow is:

1. The user enters their XMPP JID and password in the webview login dialog.
2. JavaScript calls the C++ `Login` binding.
3. C++ creates a `libstrophe_cpp` client and starts the XMPP event loop on a background thread.
4. After XMPP login succeeds, the client repeatedly sends a `rover::getopts` IQ request until the rover responds.
5. When rover options are received, C++ populates the webview with:
    - camera feed iframe URLs
    - rover command buttons
6. The client registers a telemetry handler for `rover::telemetry` IQs.
7. Incoming telemetry is forwarded to JavaScript and displayed in the status panel.
8. If telemetry stops for too long, the client marks the rover unreachable and restarts the rover options retry loop.
9. When a user clicks a rover command button, JavaScript calls `SendCommand(...)`, and C++ sends the corresponding IQ command
   to the rover.

### Threading Model

The client intentionally separates UI work from blocking network work:

- The main thread runs the Saucer application and webview event loop.
- The XMPP client runs on a background thread because the libstrophe event loop is blocking.
- A rover options retry thread repeatedly attempts the initial rover handshake until it succeeds.
- A telemetry monitor thread watches for stale telemetry and marks the rover unreachable when needed.
- A login timeout watchdog thread rejects the login attempt if the XMPP connection takes too long.

The `XmppClientState` wrapper is used so these detached background threads can tell whether their connection is still the
current one. If the user logs in again and a new XMPP client replaces the old one, stale retry/monitor threads exit instead
of continuing to update the UI for an old connection.

## Dependencies

The client depends on both UI/runtime libraries and the shared XMPP communication stack.

### Common Build Requirements

- CMake 3.31 or newer
- A C++23-capable compiler
- Git
- A supported native build tool, such as Ninja, Make, or MSBuild depending on the platform

### Client Dependencies

- **Saucer**: Native webview application framework used by the controller client.
    - go to [saucer.app](https://saucer.app/getting-started/) for more info.
    - Saucer is fetched automatically by the root CMake project through `FetchContent`, so it does not need to be
      installed
      manually before configuring the project.
- **Saucer embedded support**: Used to package frontend assets into the native client binary.
- **shared_xmpp**: Internal static library used for rover/client XMPP communication.
    - this is a submodule of this project and should be built and linked automatically by CMake.

Important note, Windows users will need to make sure
[the latest Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170#latest-supported-redistributable-version)
is installed.

Linux users need to install libwebkitgtk-6.0-4 libadwaita-1-0

### XMPP / Networking Dependencies

Because the client links against `shared_xmpp`, it also inherits the XMPP wrapper's dependencies:

- **libstrophe**: XMPP client library used by the wrapper.
- **libxml2**: XML parser used by libstrophe.
- **zlib**: Compression support used by libstrophe.
- **TLS backend**:
    - On Linux, this is OpenSSL.
    - On Windows, this is the native Windows Schannel stack.

The exact dependency resolution differs by platform.

### Linux Dependencies

On Linux, the project expects the XMPP stack to be available through the local system or user installation paths. In
particular, `shared_xmpp` expects a static `libstrophe.a` to be available.

Typical Linux development packages include:

```bash
sudo dnf install libstrophe-devel libxml2-devel zlib-devel openssl-devel pkgconf-pkg-config
```

For more portable Linux binaries, static archives may also be needed:

```bash
sudo dnf install libxml2-static zlib-static glibc-static libstdc++-static
```

Depending on distribution and architecture, `libstrophe.a` may need to be installed manually under `~/.local/lib` or
another system library path searched by the project.

Honestly, once you have libstrophe installed, as per
[the build instructions](https://github.com/strophe/libstrophe#build-instructions)
on linux, you can build it and a llm should be able to help you install the dependencies. One of the decent uses of
LLMs.

### Windows Dependencies

Welcome to hell, good luck soldier 🫡

On Windows, the GitHub Actions build uses MSVC and vcpkg-provided dependencies.

The Windows build installs:

- `libxml2:x64-windows`
- `zlib:x64-windows`

The Windows build does not require OpenSSL. Instead, the project builds `libstrophe` with Windows Schannel support and
links
against native Windows networking/security libraries:

- `ws2_32`
- `secur32`
- `crypt32`

Windows users should also make sure the
[latest Microsoft Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)
is installed before running the built client.

## Windows GitHub Actions Build

A dedicated GitHub Actions workflow is available for building the Windows native client using MSVC.

Workflow name:

```text
Windows Native Build (MSVC)
```

The workflow is manually triggered using the GitHub Actions UI.

### Workflow Summary

The Windows build workflow performs the following steps:

1. Checks out the repository.
2. Restores or creates a vcpkg binary cache.
3. Installs required Windows dependencies through vcpkg:
    - `libxml2:x64-windows`
    - `zlib:x64-windows`
4. Configures CMake with `CMAKE_PREFIX_PATH` pointing to the vcpkg installation.
5. Builds the `client_saucer` target in `RelWithDebInfo` mode.
6. Copies vcpkg DLLs into the client executable output directory.
7. Uploads the Windows build outputs as a GitHub Actions artifact.

### Dependency Installation

The workflow installs dependencies with:

```powershell
vcpkg install libxml2:x64-windows zlib:x64-windows
```

The vcpkg binary cache is stored under the GitHub workspace:

```
${{ github.workspace }}/vcpkg_cache
```

This helps reduce rebuild time by reusing previously compiled vcpkg packages.

### CMake Configuration

The workflow configures the project using:

```powershell
cmake -B build -DCMAKE_PREFIX_PATH="C:/vcpkg/installed/x64-windows" -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

It also provides `PKG_CONFIG_PATH` for packages that expose pkg-config metadata:

```text
C:/vcpkg/installed/x64-windows/lib/pkgconfig
```

The build intentionally uses `CMAKE_PREFIX_PATH` instead of injecting the vcpkg toolchain file. This keeps the Windows
configuration less invasive while still allowing CMake to locate vcpkg-installed packages. Allowing vcpkg to inject the
toolchain file caused significant issues with dependencies and the project structure.

### Build Target

The workflow currently builds the Saucer client target:

```powershell
cmake --build build --config RelWithDebInfo --target client_saucer
```

### Runtime DLL Bundling

After building, the workflow copies vcpkg-provided runtime DLLs into the client output directory:

```powershell
Copy-Item -Path "C:/vcpkg/installed/x64-windows/bin/*.dll" -Destination "build/client/RelWithDebInfo/"
```

This ensures the uploaded client binary has the dynamic libraries it needs to run on Windows.

### Uploaded Artifact

The final artifact is uploaded as:

```text
HelelaniRover-Windows-Binaries
```

The artifact includes the Windows build output directories for both the client and rover build locations, including any
copied DLLs and debug symbols (`.pdb`).


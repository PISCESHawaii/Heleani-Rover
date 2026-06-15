# Dummy Rover Daemon

The `rover_daemon` target is the current rover-side placeholder application for the PICSES Helelani Rover system.

It is not connected to real rover hardware yet. Instead, it acts as a simulated rover that connects to the XMPP server,
responds to controller commands, advertises dummy rover capabilities, and sends randomized telemetry values back to the
controller client.

This makes it useful for testing the client UI, XMPP communication flow, command routing, telemetry handling, and
connection-loss behavior without needing access to the physical rover.

## Overview

The dummy rover uses the shared `shared_xmpp` wrapper around `libstrophe` to communicate with the controller client over
XMPP.

It supports:

- Connecting to an XMPP server using credentials from a local config file.
- Responding to the controller's `rover::getopts` handshake.
- Advertising available movement commands to the controller.
- Returning dummy camera feed URLs.
- Receiving movement command IQs.
- Sending simulated telemetry values.
- Detecting when the controller stops acknowledging telemetry.
- Gracefully disconnecting on `Ctrl+C`.

## Build Target

The rover daemon is built as `rover_daemon` and it links against the shared XMPP wrapper:
```cmake
target_link_libraries(rover_daemon PRIVATE shared_xmpp)
```
From the root project, it can be built with:
```bash
cmake --build build --target rover_daemon
```
Or, when using CLion, select the `rover_daemon` target from the run/build configuration list.

## Runtime Credentials

The dummy rover reads its XMPP credentials from:
```text
../../rover/db/login.txt
```
The file is expected to contain two lines:
```text
jid@example.com
password
```
Line 1 is the rover's JID, and line 2 is the password.

If the file is missing or cannot be opened, the daemon exits immediately.

## Connection Flow

The general runtime flow is:

1. The daemon reads its XMPP JID and password from `rover/db/login.txt`.
2. It creates a `libstrophe_cpp` XMPP client.
3. It registers handlers for:
    - generic chat messages
    - XMPP version queries
    - rover movement commands
    - rover options requests
    - XMPP pings
4. It connects to the XMPP server.
5. Once connected, it starts the telemetry loop.
6. The controller repeatedly sends a `rover::getopts` IQ request until the rover responds.
7. When the rover receives `rover::getopts`, it:
    - stores the controller JID
    - enables telemetry
    - returns available commands
    - returns dummy camera feed URLs
8. The controller uses that response to populate its UI.
9. The rover begins sending telemetry IQs to the registered controller.
10. If telemetry acknowledgments stop, the rover disables telemetry and waits for a new `rover::getopts` request.

## Supported XMPP Handlers

### `rover::getopts`

The controller uses this request as the initial handshake.

When the dummy rover receives a `get` IQ with namespace:
```
text
rover::getopts
```
it treats the sender as the active controller and responds with:

- available camera feeds
- available rover commands

It also enables telemetry for that controller.

### Movement Commands

The dummy rover currently supports the following command namespaces:
```text
rover::movements::forward
rover::movements::turn_right
rover::movements::backward
rover::movements::left
rover::movements::stop
rover::movements::right
```
Each command is handled as an IQ `set` request.

Because this is a dummy rover, these commands do not control hardware. They simply:

1. print the received command to standard output
2. return an IQ `result` response
3. include a short status message, such as `"Moving forward"` or `"Stopping"`

These command IDs are also advertised in the `rover::getopts` response so the controller can dynamically build its control
buttons.

### `rover::telemetry`

Telemetry is sent from the rover to the controller as an IQ `set` request using the namespace:
```text
rover::telemetry
```
The controller is expected to respond with an IQ `result`.

The dummy rover sends telemetry every 3 seconds when telemetry is enabled.

Currently simulated telemetry fields include:

- `battery`
- `signal`
- `speed`

Each value is randomly generated from `0` to `99`.

### XMPP Ping

The daemon responds to XMPP ping requests using the namespace:
```text
urn:xmpp:ping
```
A ping request receives an empty IQ `result` response.

### XMPP Version Query

The daemon also responds to standard XMPP version requests using:
```text
jabber:iq:version
```
This is mainly useful for testing and diagnostics. The current dummy response identifies itself as a small test application.

### Generic Chat Messages

A fallback chat message handler is registered for standard XMPP chat messages.

This is not part of the normal rover/client protocol, but it helps during debugging by printing unexpected message bodies to
standard output.

## Dummy Camera Feeds

The `rover::getopts` response includes two placeholder video feeds:

- `main_camera`
- `secondary_camera`

These are intended to let the client test dynamic iframe creation and multi-camera layout behavior.

They are not live rover camera streams.

## Telemetry and Safety Behavior

The telemetry loop is intentionally written to model a future safety mechanism.

After a controller completes the `rover::getopts` handshake, telemetry is enabled and sent periodically. Each telemetry IQ
expects a response from the controller.

Timing constants:
```text
Telemetry send interval: 3 seconds
Telemetry response timeout: 15 seconds
```
If a telemetry response is not received within the timeout window, the dummy rover assumes the controller is unreachable.

When this happens, it:

1. prints a timeout warning
2. states that it would abort current rover actions
3. disables telemetry
4. clears the active controller JID
5. waits for another `rover::getopts` request before sending telemetry again

Since this is only a dummy rover, no hardware is stopped. In the real rover implementation, this is where movement,
actuators, or other active operations should be safely halted.

## Threading Model

The dummy rover uses a few concurrent execution paths:

- **Main XMPP event loop**
    - Runs through `libstrophe_cpp::connect_noexcept(...)`.
    - Handles incoming XMPP traffic and outgoing queued stanzas.

- **Telemetry thread**
    - Starts after the XMPP connection succeeds.
    - Sends randomized telemetry to the active controller.
    - Tracks whether the controller has acknowledged the previous telemetry packet.
    - Disables telemetry if the controller times out.

- **Shutdown thread**
    - Waits for `Ctrl+C`.
    - Sets the shared running flag to false.
    - Requests a clean XMPP disconnect.

Shared state is protected with mutexes and atomics. The active controller JID is guarded by a mutex, and telemetry flags are
tracked with atomic values plus a telemetry-state mutex for grouped updates.

## Graceful Shutdown

Pressing `Ctrl+C` triggers a clean shutdown.

The daemon will:

1. stop the telemetry loop
2. disconnect from the XMPP server
3. join worker threads
4. return the XMPP connection result code

## Current Limitations

This daemon is a simulation and has several intentional limitations:

- It does not control real motors or hardware.
- Telemetry values are randomly generated.
- Camera feed URLs are static placeholders.
- Only one active controller JID is tracked.
- If a controller times out, telemetry stops until another `rover::getopts` request is received.
- Some command names and behavior are placeholders for future hardware integration.

The codebase also assumes it will be on a posix-like system and probably won't be easy to make work on Windows, though 
it might work in wsl due to the lack of UI. The rover is expected to run linux anyway, so it should be fine.

/**
 * Main client application for the PICSES Helelani Rover control interface.
 * 
 * This file implements the client-side GUI application that connects to the rover via XMPP.
 * It uses the Saucer library to create a native webview-based interface, allowing users to:
 * - Login to the XMPP server
 * - View live rover telemetry and camera feed
 * - Send control commands to the rover
 * - Monitor rover connectivity and automatically retry failed connections
 * 
 * The application manages several concurrent threads:
 * - Main GUI thread (webview event loop)
 * - XMPP client thread (libstrophe event loop)
 * - Rover options retry thread (handles initial handshake)
 * - Telemetry monitor thread (detects rover disconnection)
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <thread>
#include <unistd.h>

// saucer webview shenanigans
#include <saucer/smartview.hpp>
#include <saucer/embedded/all.hpp>

// shared xmpp code
#include "libstrophe_cpp.h"
#include "xmpp_iq.h"

// other client code
#include "conn_wrapper.h"
#include "misc_routing.h"

// Debug flag to auto-display the webview devtools on startup.
// Useful when the JavaScript breaks and we need to inspect/debug the web interface.
// Set to true during development, false for production builds.
constexpr bool WEBVIEW_DEBUG_FLAG = false;

// Initial window dimensions for the application.
// Chosen to provide comfortable viewing of camera feed, controls, and telemetry simultaneously.
constexpr saucer::size DEFAULT_WINDOW_SIZE = {1000, 720};

// How long to wait between retry attempts when fetching rover options fails.
// A 5-second interval prevents overwhelming the server while still being responsive.
constexpr auto ROVER_OPTIONS_RETRY_INTERVAL = std::chrono::seconds(5);

// Maximum time without receiving telemetry before marking the rover as unreachable.
// 15 seconds allows for occasional network hiccups without false positives.
constexpr auto TELEMETRY_UNREACHABLE_TIMEOUT = std::chrono::seconds(15);

// How frequently to check if telemetry has timed out.
// 2-second interval provides responsive detection without excessive CPU usage.
constexpr auto TELEMETRY_MONITOR_INTERVAL = std::chrono::seconds(2);

// this gets deduced as a long, but im afraid to specify that since i know longs are different on windows
// so good luck compiler, I believe in you
constexpr auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    TELEMETRY_UNREACHABLE_TIMEOUT
).count();

// some bs to make clang-tidy more happy with the way saucer does js format strings
using saucer_serializer = saucer::serializers::glaze::serializer;
template<typename... Args>
using saucer_format_string = saucer::format_string<saucer_serializer, Args...>;


namespace {
    // Returns current time in milliseconds since epoch using steady_clock.
    // We use steady_clock (not system_clock) because it's monotonic and won't
    // jump backwards if the system time is adjusted, making it ideal for measuring timeouts.
    int64_t steady_now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
}

/**
 * Populates the webview UI with rover-specific configuration.
 * 
 * Called after successfully receiving rover options from the XMPP handshake.
 * Updates the camera feed URL and creates buttons for all available rover commands.
 * 
 * @param webview The smartview instance to update
 * @param video_url URL for the rover's camera stream (empty string if unavailable)
 * @param commands Vector of command ID/name pairs to create control buttons for
 */
void populate_rover_ui(
    saucer::smartview &webview,
    const std::string &video_url,
    const std::vector<std::pair<std::string, std::string> > &commands
) {
    // Update the camera feed iframe with the rover's video stream URL.
    // If the rover doesn't provide a video URL, fall back to a placeholder YouTube video (for now).
    // This prevents showing a broken iframe when camera is unavailable.
    if (!video_url.empty()) {
        webview.execute(
            saucer_format_string<const std::string &>{"setCameraIframe({})"},
            video_url
        );
    } else {
        webview.execute(
            saucer_format_string<const std::string &>{"setCameraIframe({})"},
            "https://www.youtube.com/embed/txTRZh_tiYA"
        );
    }

    // Log all available commands to the console for debugging purposes.
    // Helps developers verify that the rover is advertising the expected command set.
    for (const auto &[cmd_id, cmd_name]: commands) {
        std::cout << "Command: " << cmd_id << " -> " << cmd_name << std::endl;
    }

    // Dynamically create control buttons in the UI for each available rover command.
    // This allows different rovers to have different command sets without UI changes.
    if (!commands.empty()) {
        // Clear any existing buttons first to avoid duplicates on reconnection
        webview.execute(saucer_format_string<>{"clearControlButtons()"});

        // Create a button for each command with its ID and display name
        for (const auto &[cmd_id, cmd_name]: commands) {
            webview.execute(
                saucer_format_string<const std::string &, const std::string &>{"addControlButton({}, {})"},
                cmd_id,
                cmd_name
            );
        }

        // Log success message to the UI's command log so users know commands are ready
        webview.execute(
            saucer_format_string<const std::string &>{"addLog(new Date().toLocaleTimeString(), {})"},
            std::format("Loaded {} control commands", commands.size())
        );
    }
}

void start_telemetry_monitor(
    saucer::smartview &webview,
    std::shared_ptr<XmppClientState> xmpp_state,
    std::shared_ptr<libstrophe_cpp> client,
    std::shared_ptr<std::atomic<int64_t> > last_telemetry_ms,
    std::shared_ptr<std::atomic<bool> > rover_marked_unreachable
);

/**
 * Continuously attempts to fetch rover options until successful or disconnected.
 * 
 * This function runs in a separate thread and implements the initial handshake with the rover.
 * It sends an IQ request to discover the rover's capabilities (camera URL and available commands).
 * If the request fails, it automatically retries after ROVER_OPTIONS_RETRY_INTERVAL.
 * 
 * Once successful, it populates the UI and starts the telemetry monitor thread.
 * The loop exits if the XMPP connection is replaced (user logs in again) or on success.
 * 
 * @param webview The smartview instance for UI updates
 * @param xmpp_state Shared state tracking the current XMPP connection
 * @param client The XMPP client to use for communication
 * @param last_telemetry_ms Shared atomic timestamp of last received telemetry
 * @param rover_marked_unreachable Shared atomic flag indicating rover reachability status
 */
void start_rover_options_retry_loop(
    saucer::smartview &webview,
    std::shared_ptr<XmppClientState> xmpp_state,
    std::shared_ptr<libstrophe_cpp> client,
    std::shared_ptr<std::atomic<int64_t> > last_telemetry_ms,
    std::shared_ptr<std::atomic<bool> > rover_marked_unreachable
) {
    // Spawn a detached thread so this doesn't block the main UI thread.
    // We capture webview by reference (&) and shared_ptrs by value to ensure proper lifetime.
    std::thread([&, xmpp_state, client, last_telemetry_ms, rover_marked_unreachable]() {
        // Update UI to show we're waiting for the rover to respond
        webview.execute(saucer_format_string<>{"markRoverWaiting()"});
        webview.execute(
            saucer_format_string<const std::string &>{"addLog(new Date().toLocaleTimeString(), {})"},
            "Waiting for rover options handshake..."
        );

        // Keep retrying until we succeed or the user logs out (xmpp_state changes)
        while (xmpp_state->get() == client) {
            // Atomic flags to coordinate between the async callback and this thread.
            // We need atomics because the callback runs in the XMPP thread.
            auto request_finished = std::make_shared<std::atomic<bool> >(false);
            auto request_succeeded = std::make_shared<std::atomic<bool> >(false);

            // Send the rover options IQ request with an async callback
            fetch_rover_options(
                webview,
                client.get(),
                [&, request_finished, request_succeeded, last_telemetry_ms, rover_marked_unreachable](
            bool success,
            std::string video_url,
            std::vector<std::pair<std::string, std::string> > commands
        ) {
                    // If the request failed, just mark as finished and return.
                    // The retry loop will handle scheduling another attempt.
                    if (!success) {
                        request_finished->store(true, std::memory_order_release);
                        return;
                    }

                    // Request succeeded! Mark both flags so the retry loop knows to exit.
                    request_succeeded->store(true, std::memory_order_release);
                    request_finished->store(true, std::memory_order_release);

                    // Update the UI with the rover's camera feed and available commands
                    populate_rover_ui(webview, video_url, commands);

                    // Reset the unreachable flag and update telemetry timestamp since we just heard from the rover
                    rover_marked_unreachable->store(false, std::memory_order_relaxed);
                    last_telemetry_ms->store(steady_now_ms(), std::memory_order_relaxed);

                    // Update UI to show rover is now reachable
                    webview.execute(saucer_format_string<>{"markRoverReachable()"});
                    webview.execute(
                        saucer_format_string<const std::string &>{"addLog(new Date().toLocaleTimeString(), {})"},
                        "Rover options handshake succeeded"
                    );

                    // Start monitoring telemetry to detect if the rover goes offline
                    start_telemetry_monitor(
                        webview,
                        xmpp_state,
                        client,
                        last_telemetry_ms,
                        rover_marked_unreachable
                    );
                }
            );

            // Wait for the async request to complete, checking every 100ms.
            // Also check if the connection changed (user logged out) to exit early.
            while (!request_finished->load(std::memory_order_acquire) && xmpp_state->get() == client) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // If the request succeeded, exit the retry loop
            if (request_succeeded->load(std::memory_order_acquire)) {
                return;
            }

            // If the connection changed (user logged in again), exit the retry loop
            if (xmpp_state->get() != client) {
                return;
            }

            // Request failed and we're still connected, so log the failure and retry after a delay
            webview.execute(
                saucer_format_string<const std::string &>{"addLog(new Date().toLocaleTimeString(), {})"},
                "Rover options handshake failed; retrying..."
            );

            std::this_thread::sleep_for(ROVER_OPTIONS_RETRY_INTERVAL);
        }
    }).detach();
}

/**
 * Monitors incoming telemetry to detect when the rover becomes unreachable.
 * 
 * This function runs in a separate thread after the initial handshake succeeds.
 * It periodically checks if telemetry has been received recently. If too much time
 * passes without telemetry (TELEMETRY_UNREACHABLE_TIMEOUT), it marks the rover as
 * unreachable and restarts the options retry loop to attempt reconnection.
 * 
 * The telemetry timestamp is updated by the telemetry message handler in misc_routing.cpp.
 * 
 * @param webview The smartview instance for UI updates
 * @param xmpp_state Shared state tracking the current XMPP connection
 * @param client The XMPP client being monitored
 * @param last_telemetry_ms Shared atomic timestamp of last received telemetry (updated externally)
 * @param rover_marked_unreachable Shared atomic flag to prevent duplicate unreachable handling
 */
void start_telemetry_monitor(
    saucer::smartview &webview,
    std::shared_ptr<XmppClientState> xmpp_state,
    std::shared_ptr<libstrophe_cpp> client,
    std::shared_ptr<std::atomic<int64_t> > last_telemetry_ms,
    std::shared_ptr<std::atomic<bool> > rover_marked_unreachable
) {
    // Spawn a detached thread to avoid blocking the UI or XMPP threads
    std::thread([&, xmpp_state, client, last_telemetry_ms, rover_marked_unreachable]() {
        // Continue monitoring until the XMPP connection changes (logout/new login)
        while (xmpp_state->get() == client) {
            // Sleep between checks to avoid excessive CPU usage
            std::this_thread::sleep_for(TELEMETRY_MONITOR_INTERVAL);

            // Calculate how long it's been since we last received telemetry
            const auto elapsed_ms = steady_now_ms() - last_telemetry_ms->load(std::memory_order_relaxed);

            // If we're still receiving telemetry within the timeout window, continue monitoring
            if (elapsed_ms <= timeout_ms) {
                continue;
            }

            // Telemetry has timed out! Use atomic exchange to check if we've already handled this.
            // If the flag was already true, another thread beat us to it, so exit.
            // This prevents spawning multiple retry loops for the same timeout event.
            if (rover_marked_unreachable->exchange(true)) {
                return;
            }

            // Log to console for debugging
            std::cout << "Rover marked unreachable: telemetry timeout" << std::endl;

            // Update UI to show the rover is no longer reachable
            webview.execute(saucer_format_string<>{"markRoverUnreachable()"});
            webview.execute(
                saucer_format_string<const std::string &>{"addLog(new Date().toLocaleTimeString(), {})"},
                "Telemetry timeout; waiting for rover to return"
            );

            // Start retrying the rover options handshake to re-establish connection
            start_rover_options_retry_loop(
                webview,
                xmpp_state,
                client,
                last_telemetry_ms,
                rover_marked_unreachable
            );

            // Exit this monitor thread since the retry loop will spawn a new monitor on success
            return;
        }
    }).detach();
}

/**
 * Main application entry point and setup coroutine.
 * 
 * This coroutine is called by the Saucer framework to initialize the application.
 * It creates the main window, sets up the webview, exposes JavaScript bindings,
 * and runs until the user closes the application.
 * 
 * @param app The Saucer application instance
 * @return A coroutine that completes when the application exits
 */
coco::stray start(saucer::application *app) {
    // Create the native window that will host our webview.
    // The .value() unwraps the optional; we check for failure below.
    auto window = saucer::window::create(app).value();

    // Verify window creation succeeded; exit with error if not
    if (!window) {
        std::cerr << "Failed to create window\n";
        exit(1);
    }
    // Set the initial window size to our configured default
    window->set_size(DEFAULT_WINDOW_SIZE);

    // Create the webview component inside the window.
    // This embeds a browser engine (WebView2 on Windows, WebKit on macOS, etc.)
    auto webview = saucer::smartview::create({.window = window});

    // Verify webview creation succeeded; exit with error if not
    if (!webview) {
        std::cerr << "Failed to create webview\n";
        exit(1);
    }

    // ===== Window Configuration =====

    // Set the window title that appears in the taskbar
    window->set_title("PICSES Helelani Rover");
    // Use partial decorations to allow custom title bar while keeping system boarder for resizing
    window->set_decorations(saucer::window::decoration::partial);

    // Set initial devtools state based on the debug flag.
    // The devtools provide JavaScript console, DOM inspector, network tab, etc.
    webview->set_dev_tools(WEBVIEW_DEBUG_FLAG);

    // Expose a function to JavaScript to toggle devtools on/off at runtime.
    // This allows the user to open devtools from a button in the UI.
    webview->expose("toggleDevTools", [&](const bool devtoolsShown) -> void {
        webview->set_dev_tools(devtoolsShown);
    });

    // Create shared state to track the current XMPP connection.
    // This allows us to detect when the user logs out/in and cancel old operations.
    auto xmpp_state = std::make_shared<XmppClientState>();

    // ===== JavaScript Bindings =====

    /**
     * Login binding: Called from JavaScript when the user submits login credentials.
     * 
     * This function handles the entire login process:
     * 1. Creates an XMPP client with the provided credentials
     * 2. Spawns a thread to run the libstrophe event loop
     * 3. Sets up telemetry listeners
     * 4. Starts the rover options handshake on successful connection
     * 5. Handles connection timeouts and errors
     * 
     * The function uses a Promise-like pattern with resolve/reject callbacks.
     */
    webview->expose(
        "Login", [&, xmpp_state](std::string jid, std::string password, saucer::executor<std::string> exec) -> void {
            const auto &[resolve, reject] = exec;

            std::cout << "Login attempt: " << jid << std::endl;

            // Create XMPP client with debug logging enabled.
            // The client is wrapped in a shared_ptr so it stays alive across threads.
            const auto client = std::make_shared<libstrophe_cpp>(XMPP_LEVEL_DEBUG, jid, password);

            // Initialize telemetry timestamp to now to avoid immediate timeout
            const auto last_telemetry_ms = std::make_shared<std::atomic<int64_t> >(steady_now_ms());

            // Track whether we've marked the rover as unreachable (starts false)
            const auto rover_marked_unreachable = std::make_shared<std::atomic<bool> >(false);

            // Register this client as the current connection (invalidates any previous client)
            xmpp_state->replace(client);

            // Set up the handler that updates last_telemetry_ms when telemetry arrives
            initialize_telemetry_listener(webview.value(), client.get(), last_telemetry_ms);

            // Atomic flag to track connection status: 0=pending, 1=success, -1=failure.
            // We use atomic because multiple threads (connection thread and timeout thread) access it.
            const auto success = std::make_shared<std::atomic<char> >(0);

            // Spawn the XMPP client thread to run libstrophe's event loop.
            // This must be in a separate thread because libstrophe blocks.
            std::thread([=, &webview]() {
                // Attempt to connect to the XMPP server
                client->connect_noexcept(
                    // Success callback: invoked when XMPP connection is established
                    [=, &webview]() {
                        // Use atomic exchange to ensure only one thread handles success.
                        // If exchange returns non-zero, another thread already handled this.
                        if (success->exchange(1) != 0) return;

                        // Log server details (features, roster, etc.) to the UI
                        log_server_details(webview.value(), client.get());

                        // Update UI to show we're waiting for rover handshake
                        webview->execute(saucer_format_string<>{"markRoverWaiting()"});

                        // Start attempting to handshake with the rover
                        start_rover_options_retry_loop(
                            webview.value(),
                            xmpp_state,
                            client,
                            last_telemetry_ms,
                            rover_marked_unreachable
                        );

                        // Resolve the JavaScript promise with a success message
                        resolve("Connected to XMPP; waiting for rover");
                    },
                    // Error callback: invoked if XMPP connection fails
                    [=](const int error, const std::string &detail) {
                        // Use atomic exchange to ensure only one thread handles failure
                        if (success->exchange(-1) != 0) return;

                        // Reject the JavaScript promise with error details
                        reject(std::format("Connection failed with error: {}\n{}", error, detail));

                        // Ensure the client disconnects cleanly
                        client->disconnect();
                    }
                );

                std::cout << "client done\n";

                // Clean up: remove this client from xmpp_state if it's still current.
                // This signals any running retry/monitor threads to exit.
                xmpp_state->clear_if_current(client);
            }).detach();

            // Spawn a watchdog thread to handle DNS resolution timeouts.
            // libstrophe can block indefinitely on DNS lookups, so we need this timeout.
            std::thread([=]() {
                // Wait 10 seconds for connection to succeed
                // TODO: make a constant config
                sleep(10);

                // If connection hasn't completed, mark it as failed
                if (success->exchange(-1) != 0) return;

                // Reject the JavaScript promise with timeout message
                reject("Connection timed out");

                // Force disconnect to unblock the XMPP thread
                client->disconnect();
            }).detach();
        });

    /**
     * SendCommand binding: Called from JavaScript when the user clicks a control button.
     * 
     * This retrieves the current XMPP client (if any) and sends the command to the rover.
     * If no client is connected, the command is silently ignored (user shouldn't have buttons anyway).
     */
    webview->expose("SendCommand", [&, xmpp_state](const std::string &command) {
        // Safely retrieve the current XMPP client (returns nullptr if disconnected)
        const auto client = xmpp_state->get();

        // Only send the command if we have an active connection
        if (client) {
            send_command(webview.value(), client.get(), command);
        }
    });

    // ===== Webview Content Setup =====

    // Load the embedded web assets (HTML, CSS, JS) into the webview.
    // These were compiled into the binary by Saucer's build system.
    webview->embed(saucer::embedded::all());

    // Set the initial page to load. This should match the path in the embedded assets.
    webview->serve("/index.html");

    // Make the window visible to the user
    window->show();

    // Wait for the application to finish (user closes window or app exits).
    // This is a coroutine await point that suspends until the app quits.
    co_await app->finish();
}

/**
 * Application entry point.
 * 
 * Creates the Saucer application instance and runs it with our start() coroutine.
 * The application ID "rover-fe-cpp" is used by the OS for identifying the app
 * (e.g., for app data directories, system tray, etc.).
 * 
 * @return Exit code (0 for success, non-zero for error)
 */
int main() {
    return saucer::application::create({.id = "rover-fe-cpp"})->run(start);
}

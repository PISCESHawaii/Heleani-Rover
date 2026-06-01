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

// auto-display the webview devtools incase the javascript breaks and we need to break in
constexpr bool WEBVIEW_DEBUG_FLAG = false;

constexpr saucer::size DEFAULT_WINDOW_SIZE = {1000, 720};

constexpr auto ROVER_OPTIONS_RETRY_INTERVAL = std::chrono::seconds(5);
constexpr auto TELEMETRY_UNREACHABLE_TIMEOUT = std::chrono::seconds(15);
constexpr auto TELEMETRY_MONITOR_INTERVAL = std::chrono::seconds(2);

namespace {
    int64_t steady_now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
}

// Helper to populate UI with rover options
void populate_rover_ui(
    saucer::smartview &webview,
    const std::string &video_url,
    const std::vector<std::pair<std::string, std::string> > &commands
) {
    // Set camera iframe with fetched video URL or fallback
    if (!video_url.empty()) {
        webview.execute("setCameraIframe({})", video_url);
    } else {
        webview.execute("setCameraIframe({})", "https://www.youtube.com/embed/txTRZh_tiYA");
    }

    // Log available commands
    for (const auto &[cmd_id, cmd_name]: commands) {
        std::cout << "Command: " << cmd_id << " -> " << cmd_name << std::endl;
    }

    // Populate control buttons with fetched commands
    if (!commands.empty()) {
        webview.execute("clearControlButtons()");

        for (const auto &[cmd_id, cmd_name]: commands) {
            webview.execute("addControlButton({}, {})", cmd_id, cmd_name);
        }

        webview.execute(
            "addLog(new Date().toLocaleTimeString(), {})",
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

void start_rover_options_retry_loop(
    saucer::smartview &webview,
    std::shared_ptr<XmppClientState> xmpp_state,
    std::shared_ptr<libstrophe_cpp> client,
    std::shared_ptr<std::atomic<int64_t> > last_telemetry_ms,
    std::shared_ptr<std::atomic<bool> > rover_marked_unreachable
) {
    std::thread([&, xmpp_state, client, last_telemetry_ms, rover_marked_unreachable]() {
        webview.execute("markRoverWaiting()");
        webview.execute(
            "addLog(new Date().toLocaleTimeString(), {})",
            "Waiting for rover options handshake..."
        );

        while (xmpp_state->get() == client) {
            auto request_finished = std::make_shared<std::atomic<bool> >(false);
            auto request_succeeded = std::make_shared<std::atomic<bool> >(false);

            fetch_rover_options(
                webview,
                client.get(),
                [&, request_finished, request_succeeded, last_telemetry_ms, rover_marked_unreachable](
            bool success,
            std::string video_url,
            std::vector<std::pair<std::string, std::string> > commands
        ) {
                    if (!success) {
                        request_finished->store(true, std::memory_order_release);
                        return;
                    }

                    request_succeeded->store(true, std::memory_order_release);
                    request_finished->store(true, std::memory_order_release);

                    populate_rover_ui(webview, video_url, commands);

                    rover_marked_unreachable->store(false, std::memory_order_relaxed);
                    last_telemetry_ms->store(steady_now_ms(), std::memory_order_relaxed);

                    webview.execute("markRoverReachable()");
                    webview.execute(
                        "addLog(new Date().toLocaleTimeString(), {})",
                        "Rover options handshake succeeded"
                    );

                    start_telemetry_monitor(
                        webview,
                        xmpp_state,
                        client,
                        last_telemetry_ms,
                        rover_marked_unreachable
                    );
                }
            );

            while (!request_finished->load(std::memory_order_acquire) && xmpp_state->get() == client) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (request_succeeded->load(std::memory_order_acquire)) {
                return;
            }

            if (xmpp_state->get() != client) {
                return;
            }

            webview.execute(
                "addLog(new Date().toLocaleTimeString(), {})",
                "Rover options handshake failed; retrying..."
            );

            std::this_thread::sleep_for(ROVER_OPTIONS_RETRY_INTERVAL);
        }
    }).detach();
}

void start_telemetry_monitor(
    saucer::smartview &webview,
    std::shared_ptr<XmppClientState> xmpp_state,
    std::shared_ptr<libstrophe_cpp> client,
    std::shared_ptr<std::atomic<int64_t> > last_telemetry_ms,
    std::shared_ptr<std::atomic<bool> > rover_marked_unreachable
) {
    std::thread([&, xmpp_state, client, last_telemetry_ms, rover_marked_unreachable]() {
        while (xmpp_state->get() == client) {
            std::this_thread::sleep_for(TELEMETRY_MONITOR_INTERVAL);

            const auto elapsed_ms = steady_now_ms() - last_telemetry_ms->load(std::memory_order_relaxed);
            const auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                TELEMETRY_UNREACHABLE_TIMEOUT
            ).count();

            if (elapsed_ms <= timeout_ms) {
                continue;
            }

            if (rover_marked_unreachable->exchange(true)) {
                return;
            }

            std::cout << "Rover marked unreachable: telemetry timeout" << std::endl;

            webview.execute("markRoverUnreachable()");
            webview.execute(
                "addLog(new Date().toLocaleTimeString(), {})",
                "Telemetry timeout; waiting for rover to return"
            );

            start_rover_options_retry_loop(
                webview,
                xmpp_state,
                client,
                last_telemetry_ms,
                rover_marked_unreachable
            );

            return;
        }
    }).detach();
}

// saucer startup logic
coco::stray start(saucer::application *app) {
    auto window = saucer::window::create(app).value();

    if (!window) {
        std::cerr << "Failed to create window\n";
        exit(1);
    }
    window->set_size(DEFAULT_WINDOW_SIZE);

    auto webview = saucer::smartview::create({.window = window});

    if (!webview) {
        std::cerr << "Failed to create webview\n";
        exit(1);
    }

    // 1. Window Configuration
    window->set_title("PICSES Helelani Rover");
    window->set_decorations(saucer::window::decoration::partial);

    // constant flag controls default state
    // this is referring to the webview's browser devtools popout, aka inspect element
    webview->set_dev_tools(WEBVIEW_DEBUG_FLAG);
    webview->expose("toggleDevTools", [&](const bool devtoolsShown) -> void {
        webview->set_dev_tools(devtoolsShown);
    });

    auto xmpp_state = std::make_shared<XmppClientState>();

    // 2. Bindings
    webview->expose(
        "Login", [&, xmpp_state](std::string jid, std::string password, saucer::executor<std::string> exec) -> void {
            const auto &[resolve, reject] = exec;

            std::cout << "Login attempt: " << jid << std::endl;

            auto client = std::make_shared<libstrophe_cpp>(XMPP_LEVEL_DEBUG, jid, password);
            auto last_telemetry_ms = std::make_shared<std::atomic<int64_t> >(steady_now_ms());
            auto rover_marked_unreachable = std::make_shared<std::atomic<bool> >(false);

            xmpp_state->replace(client);

            initialize_telemetry_listener(webview.value(), client.get(), last_telemetry_ms);

            // 1=success -1=failure 0=pending
            auto success = std::make_shared<std::atomic<char> >(0);

            // run libstrophe in a thread
            std::thread([=, &webview]() {
                // connect
                client->connect_noexcept(
                    // when the client connects this will be called
                    [=, &webview]() {
                        if (success->exchange(1) != 0) return;

                        log_server_details(webview.value(), client.get());

                        webview->execute("markRoverWaiting()");

                        start_rover_options_retry_loop(
                            webview.value(),
                            xmpp_state,
                            client,
                            last_telemetry_ms,
                            rover_marked_unreachable
                        );

                        resolve("Connected to XMPP; waiting for rover");
                    },
                    // if there's a capturable error
                    [=](const int error, const std::string &detail) {
                        if (success->exchange(-1) != 0) return;

                        reject(std::format("Connection failed with error: {}\n{}", error, detail));
                        client->disconnect();
                    }
                );

                std::cout << "client done\n";
                xmpp_state->clear_if_current(client);
            }).detach();

            // separate thread to deal with libstrophe blocking on failing dns
            std::thread([=]() {
                sleep(10);
                if (success->exchange(-1) != 0) return;

                reject("Connection timed out");
                client->disconnect();
            }).detach();
        });

    webview->expose("SendCommand", [&, xmpp_state](std::string command) {
        const auto client = xmpp_state->get();

        if (client) {
            send_command(webview.value(), client.get(), command);
        }
    });

    // include embedded webview assets and know where to start them
    webview->embed(saucer::embedded::all());
    webview->serve("/index.html");

    // show, and wait until the app is done before exiting
    window->show();
    co_await app->finish();
}

int main() {
    return saucer::application::create({.id = "rover-fe-cpp"})->run(start);
}

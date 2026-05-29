#include <iostream>
#include <string>
#include <unistd.h>
#include <memory>

// saucer webview shenanigans
#include <saucer/smartview.hpp>
#include <saucer/embedded/all.hpp>

// shared xmpp code
#include "libstrophe_cpp.h"
#include "xmpp_iq.h"

// other client code
#include "misc_routing.h"

// auto-display the webview devtools incase the javascript breaks and we need to break in
constexpr bool WEBVIEW_DEBUG_FLAG = false;

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

// saucer startup logic
coco::stray start(saucer::application *app) {
    auto window = saucer::window::create(app).value();
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

    // Shared pointer for RAII cleanup
    auto xmpp_client = std::make_shared<libstrophe_cpp *>(nullptr);

    // 2. Bindings
    webview->expose(
        "Login", [&, xmpp_client](std::string jid, std::string password, saucer::executor<std::string> exec) -> void {
            const auto &[resolve, reject] = exec;

            std::cout << "Login attempt: " << jid << std::endl;

            *xmpp_client = new libstrophe_cpp(XMPP_LEVEL_DEBUG, jid, password);

            initialize_telemetry_listener(webview.value(), *xmpp_client);

            // 1=success -1=failure 0=pending
            auto success = std::make_shared<std::atomic<char> >(0);

            // run libstrophe in a thread
            std::thread([=, &webview]() {
                auto stale_client_ptr = *xmpp_client;

                // connect
                (*xmpp_client)->connect_noexcept(
                    // when the client connects this will be called
                    [=, &webview]() {
                        if (success->exchange(1) != 0) return;

                        log_server_details(webview.value(), *xmpp_client);

                        // Fetch rover options before resolving login
                        fetch_rover_options(
                            webview.value(),
                            *xmpp_client,
                            [=, &webview](std::string video_url,
                                          std::vector<std::pair<std::string, std::string> > commands) {
                                populate_rover_ui(webview.value(), video_url, commands);
                                resolve("Connected");
                            }
                        );
                    },
                    // if there's a capturable error
                    [=](const int error, const std::string &detail) {
                        if (success->exchange(-1) != 0) return;

                        reject(std::format("Connection failed with error: {}\n{}", error, detail));

                        if (*xmpp_client) {
                            (*xmpp_client)->disconnect();
                        }
                    }
                );

                std::cout << "client done\n";
                // cleanup closed client
                delete stale_client_ptr;
            }).detach();

            // separate thread to deal with libstrophe blocking on failing dns
            std::thread([=]() {
                sleep(10);
                if (success->exchange(-1) != 0) return;

                reject("Connection timed out");

                if (*xmpp_client) {
                    (*xmpp_client)->disconnect();
                }
            }).detach();
        });

    webview->expose("SendCommand", [&, xmpp_client](std::string command) {
        if (*xmpp_client) {
            send_command(webview.value(), *xmpp_client, command);
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

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
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
#include "misc_routing.h"

// auto-display the webview devtools incase the javascript breaks and we need to break in
constexpr bool WEBVIEW_DEBUG_FLAG = false;

class XmppClientState {
public:
    void replace(std::shared_ptr<libstrophe_cpp> next) {
        std::shared_ptr<libstrophe_cpp> old; {
            std::lock_guard lock(mutex_);
            old = std::exchange(client_, std::move(next));
        }

        if (old) {
            old->disconnect();
        }
    }

    std::shared_ptr<libstrophe_cpp> get() const {
        std::lock_guard lock(mutex_);
        return client_;
    }

    void clear_if_current(const std::shared_ptr<libstrophe_cpp> &client) {
        std::lock_guard lock(mutex_);
        if (client_ == client) {
            client_.reset();
        }
    }

    void disconnect_current() {
        std::shared_ptr<libstrophe_cpp> client; {
            std::lock_guard lock(mutex_);
            client = client_;
        }

        if (client) {
            client->disconnect();
        }
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<libstrophe_cpp> client_;
};

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

    auto xmpp_state = std::make_shared<XmppClientState>();

    // 2. Bindings
    webview->expose(
        "Login", [&, xmpp_state](std::string jid, std::string password, saucer::executor<std::string> exec) -> void {
            const auto &[resolve, reject] = exec;

            std::cout << "Login attempt: " << jid << std::endl;

            auto client = std::make_shared<libstrophe_cpp>(XMPP_LEVEL_DEBUG, jid, password);
            xmpp_state->replace(client);

            initialize_telemetry_listener(webview.value(), client.get());

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

                        // Fetch rover options before resolving login
                        fetch_rover_options(
                            webview.value(),
                            client.get(),
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

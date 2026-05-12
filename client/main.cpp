#include <saucer/smartview.hpp>
#include <saucer/embedded/all.hpp>
#include <iostream>
#include <string>

#include "libstrophe_cpp.h"

// auto-display the webview devtools incase the javascript breaks and we need to break in
#define WEBVIEW_DEBUG_FLAG false

// saucer startup logic
coco::stray start(saucer::application *app) {
    auto window = saucer::window::create(app).value();
    auto webview = saucer::smartview::create({.window = window});

    // 1. Window Configuration
    window->set_title("PICSES Helelani Rover");
    window->set_decorations(saucer::window::decoration::partial);

    // constant flag controls default state
    // this is referrring to the webview's browser devtools popout, aka inspect element
    webview->set_dev_tools(WEBVIEW_DEBUG_FLAG);
    webview->expose("toggleDevTools", [&](const bool devtoolsShown) -> void {
        webview->set_dev_tools(devtoolsShown);
    });

    // TODO: free this on window close or whatever else
    libstrophe_cpp *xmpp_client = nullptr;

    // 2. Bindings
    webview->expose("Login", [&](std::string jid, std::string password, saucer::executor<std::string> exec) -> void {
        const auto &[resolve, reject] = exec;
        std::cout << "Login attempt: " << jid << " " << password << std::endl;

        xmpp_client = new libstrophe_cpp(XMPP_LEVEL_DEBUG, jid, password);

        auto connected = std::make_shared<std::atomic<bool> >(false);
        auto failed = std::make_shared<std::atomic<bool> >(false);

        // run libstrophe in a thread
        std::thread t([=]() {
            xmpp_client->connect_noexcept(
                [=]() {
                    if (failed->load()) return;
                    connected->store(true);
                    resolve("Connected");
                },
                [=](const int error, const std::string &detail) {
                    if (connected->load()) return;
                    // failed->compare_exchange_strong()
                    reject(std::format("Connection failed with error: {}\n{}", error,
                                       detail));
                }
            );

            std::cout << "client done\n";
        });
        t.detach();

        // separate thread to deal with libstrophe blocking on failing dns
        std::thread timeout([=, &xmpp_client]() {
            sleep(10);
            if (!connected->load() && !failed->load()) {
                failed->store(true);
                reject("Connection timed out");
                // if (!xmpp_client) return;
                // auto old_client = xmpp_client;
                // xmpp_client = nullptr;
                // old_client->disconnect();
            }
        });
        timeout.detach();
    });
    webview->expose("SendCommand", [](std::string command) -> coco::task<void> {
        std::cout << "Sending command: " << command << std::endl;
        co_return;
    });

    // include embedded webview assets and know where to start them
    webview->embed(saucer::embedded::all());
    webview->serve("/index.html");

    // show, and wait until the app is done before exiting
    window->show();
    co_await app->finish();
}

// run the app with the defined start loop
int main() {
    return saucer::application::create({.id = "rover-fe-cpp"})->run(start);
}

#include <iostream>
#include <string>

// saucer webview shenanigans
#include <saucer/smartview.hpp>
#include <saucer/embedded/all.hpp>

// shared xmpp code
#include "libstrophe_cpp.h"
#include "xmpp_iq.h"

// auto-display the webview devtools incase the javascript breaks and we need to break in
#define WEBVIEW_DEBUG_FLAG false

void log_server_details(saucer::smartview &webview, libstrophe_cpp *xmpp_client) {
    // --- NEW: Version Request IQ Test ---
    // We send this once to verify our new send_iq logic works.
    auto version_req = make_iq_query("get", "query", "jabber:iq:version");
    version_req.attributes["to"] = xmpp_client->domain;


    xmpp_client->send_iq(version_req, [&](libstrophe_cpp *c, XmppNode response) {
        std::stringstream versionLog;
        versionLog << "Server Version:";

        if (response.attributes["type"] == "result") {
            // Find the query child and its children (name, version, os)
            for (const auto &query: response.children) {
                if (query.name == "query") {
                    for (const auto &info: query.children) {
                        versionLog << ' ' << info.name << ": " << info.text_content << ';';
                    }
                }
            }
        } else {
            versionLog << " Version request failed or was not supported.\n";
        }

        // log the freshly fetched details
        std::cout << versionLog.str() << std::endl;
        webview.execute("addLog(new Date().toLocaleTimeString(), {})", versionLog.str());
    });
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

        // 1=success -1=failure 0=pending
        auto success = std::make_shared<std::atomic<char> >(0);

        // run libstrophe in a thread
        std::thread t([=, &xmpp_client, &webview]() {
            // keep a stale pointer so that we can clean it up once exited
            auto stale_client_ptr = xmpp_client;

            // connect
            xmpp_client->connect_noexcept(
                // when the client connects this will be called
                [=, &webview]() {
                    if (success->exchange(1) != 0) return;
                    resolve("Connected");
                    log_server_details(webview.value(), xmpp_client);
                },
                // if theres a capturable error, idk how to reliably get out the error
                [=, &xmpp_client](const int error, const std::string &detail) {
                    if (success->exchange(-1) != 0) return;
                    reject(
                        std::format("Connection failed with error: {}\n{}", error, detail)
                    );
                    if (!xmpp_client) return;
                    auto old_client = xmpp_client;
                    xmpp_client = nullptr;
                    old_client->disconnect();
                }
            );

            std::cout << "client done\n";
            // cleanup closed client
            if (stale_client_ptr) delete stale_client_ptr;
        });
        t.detach();

        // separate thread to deal with libstrophe blocking on failing dns
        std::thread timeout([=, &xmpp_client]() {
            sleep(10);
            if (success->exchange(-1)) return;
            reject("Connection timed out");
            if (!xmpp_client) return;
            auto old_client = xmpp_client;
            xmpp_client = nullptr;
            old_client->disconnect();
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

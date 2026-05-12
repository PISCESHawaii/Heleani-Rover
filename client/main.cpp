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

std::string COMMAND_REQEST_TYPE = "set";
#define ROVER_LOCALPART "testing"
#define ROVER_RESOURCE "helelani"

void log_server_details(saucer::smartview &webview, libstrophe_cpp *xmpp_client) {
    // --- NEW: Version Request IQ Test ---
    // We send this once to verify our new send_iq logic works.
    auto version_req = make_iq_query("get", "query", "jabber:iq:version");
    version_req.attributes["to"] = xmpp_client->domain;


    xmpp_client->send_iq(version_req, [&webview](libstrophe_cpp *c, XmppNode response) {
        std::stringstream versionLog;
        versionLog << "Server Version:";

        if (response.attributes["type"] == "result") {
            // Find the query child and its children (name, version, os)
            for (const auto &query: response.children) {
                if (query->name == "query") {
                    for (const auto &info: query->children) {
                        versionLog << ' ' << info->name << ": " << info->text_content << ';';
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

void send_command(saucer::smartview &webview, libstrophe_cpp *xmpp_client, std::string command_id) {
    auto command = make_iq_query(COMMAND_REQEST_TYPE, "query", command_id);
    command.attributes["to"] = std::format("{}@{}/{}", ROVER_LOCALPART, xmpp_client->domain, ROVER_RESOURCE);

    xmpp_client->send_iq(command, [&webview, command_id](libstrophe_cpp *c, XmppNode response) {
        std::stringstream responseLog;
        responseLog << command_id;

        auto query_node = response.find_child("query");
        std::string status = "(null)";
        if (query_node.has_value()) {
            auto status_node = query_node.value()->find_child("status");
            if (status_node.has_value()) {
                status = status_node.value()->text_content;
            }
        }
        // auto status = status_node.has_value() ? status_node.value().text_content : "(null)";

        if (response.attributes["type"] == "result") {
            responseLog << " sent successfully, got back status \"" << status << "\"\n";
        } else {
            responseLog << " got error status \"" << status << "\"\n";
        }

        // log the freshly fetched details
        std::cout << responseLog.str() << std::endl;
        webview.execute("addLog(new Date().toLocaleTimeString(), {})", responseLog.str());
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

        xmpp_client->set_iq_handler(
            "set", "rover::telemetry",
            [&webview](libstrophe_cpp *c, XmppNode request) {
                std::cout << "=== Telemetry Update Received ===" << std::endl;

                // Extract and log the battery, signal, and speed values
                for (const auto &query: request.children) {
                    if (query->name == "query") {
                        for (const auto &stat: query->children) {
                            if (stat->name == "battery") {
                                webview->execute("updateBattery({})", stat->text_content);
                            } else if (stat->name == "signal") {
                                webview->execute("updateSignal({})", stat->text_content);
                            } else if (stat->name == "speed") {
                                webview->execute("updateSpeed({})", stat->text_content);
                            } else {
                                webview->execute(
                                    "addLog(new Date().toLocaleTimeString(), {})",
                                    std::format("Unknown telemetry stat \"{}\": {}", stat->name,
                                                stat->text_content)
                                );
                            }
                        }
                    }
                }
                // Build the result response
                XmppNode resp_query("query");
                resp_query.attributes["xmlns"] = "rover::telemetry";

                XmppNode response("iq");
                response.attributes["type"] = "result";
                response.attributes["to"] = request.attributes["from"];
                response.attributes["id"] = request.attributes["id"];
                response.children.emplace_back(std::make_shared<XmppNode>(resp_query));

                return response;
            }
        );

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

    webview->expose("SendCommand", [&](std::string command) {
        send_command(webview.value(), xmpp_client, command);
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

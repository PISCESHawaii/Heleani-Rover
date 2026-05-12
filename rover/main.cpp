#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "libstrophe_cpp.h"
#include "xmpp_node.h"
#include "xmpp_iq.h"

// TODO: get this from an iq set
#define client_jid "testing@pain.agency/client"

/**
 * Message Handler (Echo Bot Logic)
 * Now uses the clean XmppNode instead of raw pointers.
 */
void handle_message(libstrophe_cpp *client, XmppNode stanza) {
    // 1. Find the message body
    std::string message_text = "no body";
    for (const auto &child: stanza.children) {
        if (child->name == "body") {
            message_text = child->text_content;
            break;
        }
    }

    std::cout << "Received Message: " << message_text << std::endl;

    // 2. Build the Echo Reply using the Fluent API
    XmppNode reply("message");
    reply.attributes["to"] = stanza.attributes["from"];
    reply.attributes["type"] = "chat";
    XmppNode body("body");
    body.text_content = "Echo: \"" + message_text + "\"";
    reply.children.emplace_back(std::make_shared<XmppNode>(body));

    client->send(reply);

    // --- NEW: Version Request IQ Test ---
    // We send this once to verify our new send_iq logic works.
    auto version_req = make_iq_query("get", "query", "jabber:iq:version");
    // version_req.attributes["to"] = stanza.attributes["from"];
    version_req.attributes["to"] = "pain.agency";

    std::cout << "Sending Version Request [ID: " << version_req.attributes["id"] << "]..." << std::endl;

    client->send_iq(version_req, [](libstrophe_cpp *c, XmppNode response) {
        std::cout << "=== Version Response Received ===" << std::endl;
        if (response.attributes["type"] == "result") {
            // Find the query child and its children (name, version, os)
            for (const auto &query: response.children) {
                if (query->name == "query") {
                    for (const auto &info: query->children) {
                        std::cout << info->name << ": " << info->text_content << std::endl;
                    }
                }
            }
        } else {
            std::cerr << "Version request failed or was not supported." << std::endl;
        }
    });
}

int main() {
    // Load login info from file
    std::ifstream file("../../rover/db/login.txt");
    if (!file.is_open()) {
        std::cerr << "Could not open login.txt" << std::endl;
        return 1;
    }

    std::string jid, password;
    std::getline(file, jid);
    std::getline(file, password);
    file.close();

    // Initialize the client on Fedora
    libstrophe_cpp lsc(XMPP_LEVEL_DEBUG, jid, password);

    // Register our pattern-matched handler for messages
    // Using nullopt for the namespace to catch all standard client messages
    lsc.set_handler(std::nullopt, "message", "chat", handle_message);

    lsc.set_iq_handler("get", "jabber:iq:version",
                       [](libstrophe_cpp *c, XmppNode request) {
                           XmppNode query("query");
                           query.attributes["xmlns"] = "jabber:iq:version";

                           XmppNode name("name");
                           name.text_content = "libstrophe_cpp_test";
                           XmppNode version("version");
                           version.text_content = "0.1";
                           XmppNode os("os");
                           os.text_content = "Linux";

                           query.children.emplace_back(std::make_shared<XmppNode>(name));
                           query.children.emplace_back(std::make_shared<XmppNode>(version));
                           query.children.emplace_back(std::make_shared<XmppNode>(os));

                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           return response;
                       });

    // Rover movement handlers
    lsc.set_iq_handler("set", "rover::movements::forward",
                       [](libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Rover command: FORWARD" << std::endl;
                           XmppNode query("query");
                           query.attributes["xmlns"] = "rover::movements::forward";
                           XmppNode status("status");
                           status.text_content = "Moving forward";
                           query.children.emplace_back(std::make_shared<XmppNode>(status));

                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           return response;
                       });

    lsc.set_iq_handler("set", "rover::movements::turn_right",
                       [](libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Rover command: TURN RIGHT" << std::endl;
                           XmppNode query("query");
                           query.attributes["xmlns"] = "rover::movements::turn_right";
                           XmppNode status("status");
                           status.text_content = "Turning right";
                           query.children.emplace_back(std::make_shared<XmppNode>(status));

                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           return response;
                       });

    lsc.set_iq_handler("set", "rover::movements::backward",
                       [](libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Rover command: BACKWARD" << std::endl;
                           XmppNode query("query");
                           query.attributes["xmlns"] = "rover::movements::backward";
                           XmppNode status("status");
                           status.text_content = "Moving backward";
                           query.children.emplace_back(std::make_shared<XmppNode>(status));

                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           return response;
                       });

    lsc.set_iq_handler("set", "rover::movements::left",
                       [](libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Rover command: LEFT" << std::endl;
                           XmppNode query("query");
                           query.attributes["xmlns"] = "rover::movements::left";
                           XmppNode status("status");
                           status.text_content = "Turning left";
                           query.children.emplace_back(std::make_shared<XmppNode>(status));

                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           return response;
                       });

    lsc.set_iq_handler("set", "rover::movements::stop",
                       [](libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Rover command: STOP" << std::endl;
                           XmppNode query("query");
                           query.attributes["xmlns"] = "rover::movements::stop";
                           XmppNode status("status");
                           status.text_content = "Stopping";
                           query.children.emplace_back(std::make_shared<XmppNode>(status));

                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           return response;
                       });

    lsc.set_iq_handler("set", "rover::movements::right",
                       [](libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Rover command: RIGHT" << std::endl;
                           XmppNode query("query");
                           query.attributes["xmlns"] = "rover::movements::right";
                           XmppNode status("status");
                           status.text_content = "Turning right";
                           query.children.emplace_back(std::make_shared<XmppNode>(status));

                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           return response;
                       });


    std::cout << "Connecting to XMPP server as " << jid << "..." << std::endl;

    return lsc.connect_noexcept(
        [&]() {
            std::cout << "Connected!" << std::endl;

            std::thread pseudo_telemetry_loop([&]() {
                while (true) {
                    sleep(3);

                    auto telemetry_iq = make_iq_query("set", "query", "rover::telemetry");
                    telemetry_iq.attributes["to"] = client_jid;
                    auto querypart = telemetry_iq.find_child("query").value();

                    auto battery_node = std::make_shared<XmppNode>(XmppNode("battery"));
                    battery_node->text_content = std::to_string(rand() % 100);
                    querypart->children.emplace_back(battery_node);

                    auto signal_node = std::make_shared<XmppNode>(XmppNode("signal"));
                    signal_node->text_content = std::to_string(rand() % 100);
                    querypart->children.emplace_back(signal_node);

                    auto speed_node = std::make_shared<XmppNode>(XmppNode("speed"));
                    speed_node->text_content = std::to_string(rand() % 100);
                    querypart->children.emplace_back(speed_node);

                    std::cout << "Sending Version Request [ID: " << telemetry_iq.attributes["id"] << "]..." <<
                            std::endl;

                    lsc.send_iq(telemetry_iq, [](libstrophe_cpp *c, XmppNode response) {
                        std::cout << "=== Version Response Received ===" << std::endl;
                        if (response.attributes["type"] == "result") {
                            // Find the query child and its children (name, version, os)
                            for (const auto &query: response.children) {
                                if (query->name == "query") {
                                    for (const auto &info: query->children) {
                                        std::cout << info->name << ": " << info->text_content << std::endl;
                                    }
                                }
                            }
                        } else {
                            std::cerr << "Version request failed or was not supported." << std::endl;
                        }
                    });
                }
            });
            pseudo_telemetry_loop.detach();
        },
        nullptr
    );
}

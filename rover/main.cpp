#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

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

    std::cout << "Received Unknown Message from " << stanza.attributes["from"] << ": \"" << message_text << "\"\n";
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

    std::mutex client_jid_mutex;
    std::optional<std::string> client_jid_opt = std::nullopt;

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

    lsc.set_iq_handler("get", "rover::getopts",
                       [&](libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Rover options requested" << std::endl;

                           /* TODO: if theres already one associated, error */ {
                               std::lock_guard lock(client_jid_mutex);
                               client_jid_opt = request.attributes["from"];
                           }

                           XmppNode query("query");
                           query.attributes["xmlns"] = "rover::getopts";

                           // Video URL
                           XmppNode video_url("video_url");
                           video_url.text_content = "https://dl.4d2.sh/tQvzglBcFLAk.mp4";
                           query.children.emplace_back(std::make_shared<XmppNode>(video_url));

                           // Commands container
                           XmppNode commands("commands");

                           // Add each available command
                           std::vector<std::pair<std::string, std::string> > available_commands = {
                               {"rover::movements::forward", "Forward"},
                               {"rover::movements::turn_right", "Turn Right"},
                               {"rover::movements::backward", "Backwards"},
                               {"rover::movements::left", "Left"},
                               {"rover::movements::stop", "Stop"},
                               {"rover::movements::right", "Right"}
                           };

                           for (const auto &[cmd_id, cmd_name]: available_commands) {
                               XmppNode command("command");
                               command.attributes["id"] = cmd_id;
                               command.text_content = cmd_name;
                               commands.children.emplace_back(std::make_shared<XmppNode>(command));
                           }

                           query.children.emplace_back(std::make_shared<XmppNode>(commands));

                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           std::cout << "Rover options sent" << std::endl;
                           return response;
                       });

    lsc.set_iq_handler("get", "urn:xmpp:ping",
                       [](libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Ping request received from " << request.attributes["from"] << std::endl;
                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
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

                    /* check if its been updated, if no client attached yet, ignore */ {
                        std::lock_guard lock(client_jid_mutex);
                        if (!client_jid_opt) continue;
                        telemetry_iq.attributes["to"] = client_jid_opt.value();
                    }

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

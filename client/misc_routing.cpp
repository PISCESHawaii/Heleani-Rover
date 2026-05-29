//
// Created by joseph on 5/27/26.
//
#include "misc_routing.h"

void initialize_telemetry_listener(saucer::smartview &webview, libstrophe_cpp *xmpp_client) {
    xmpp_client->set_iq_handler(
        "set", "rover::telemetry",
        [&webview](libstrophe_cpp *c, XmppNode request) {
            std::cout << "=== Telemetry Update Received ===" << std::endl;

            // Extract and log the battery, signal, and speed values
            if (auto query = request.find_child("query"); query.has_value()) {
                for (const auto &stat: query.value()->children) {
                    if (stat->name == "battery") {
                        webview.execute("updateBattery({})", stat->text_content);
                    } else if (stat->name == "signal") {
                        webview.execute("updateSignal({})", stat->text_content);
                    } else if (stat->name == "speed") {
                        webview.execute("updateSpeed({})", stat->text_content);
                    } else {
                        webview.execute(
                            "addLog(new Date().toLocaleTimeString(), {})",
                            std::format("Unknown telemetry stat \"{}\": {}", stat->name,
                                        stat->text_content)
                        );
                    }
                }
            }

            // Build the result response using helper
            XmppNode response = make_iq_query("result", "query", "rover::telemetry");
            response.attributes["to"] = request.attributes["from"];
            response.attributes["id"] = request.attributes["id"];

            return response;
        }
    );
}

void log_server_details(saucer::smartview &webview, libstrophe_cpp *xmpp_client) {
    auto version_req = make_iq_query("get", "query", "jabber:iq:version");
    version_req.attributes["to"] = xmpp_client->domain;


    xmpp_client->send_iq(version_req, [&webview](libstrophe_cpp *c, XmppNode response) {
        std::stringstream versionLog;
        versionLog << "Server Version:";

        if (response.attributes["type"] == "result") {
            if (auto query = response.find_child("query"); query.has_value()) {
                for (const auto &info: query.value()->children) {
                    versionLog << ' ' << info->name << ": " << info->text_content << ';';
                }
            }
        } else {
            versionLog << " Version request failed or was not supported.";
        }

        std::cout << versionLog.str() << std::endl;
        webview.execute("addLog(new Date().toLocaleTimeString(), {})", versionLog.str());
    });
}

void fetch_rover_options(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client,
    std::function<
        void(
            std::string video_url,
            std::vector<std::pair<std::string, std::string> > commands
        )> callback
) {
    auto opts_req = make_iq_query("get", "query", "rover::getopts");
    opts_req.attributes["to"] = std::format("{}@{}/{}", ROVER_LOCALPART, SERVER_TMP, ROVER_RESOURCE);

    std::cout << "Sending rover::getopts request to: " << opts_req.attributes["to"] << std::endl;

    xmpp_client->send_iq(opts_req, [&webview, callback](libstrophe_cpp *c, XmppNode response) {
        std::cout << "Received response type: " << response.attributes["type"] << std::endl;

        if (response.attributes["type"] != "result") {
            std::cout << "Failed to fetch rover options - response type: " << response.attributes["type"] << std::endl;
            if (auto error_node = response.find_child("error"); error_node.has_value()) {
                std::cout << "Error details found" << std::endl;
            }
            callback("", {});
            return;
        }

        std::string video_url;
        std::vector<std::pair<std::string, std::string> > commands;

        std::cout << "Processing result response..." << std::endl;
        if (auto query = response.find_child("query"); query.has_value()) {
            std::cout << "Found query node" << std::endl;

            for (const auto &child: query.value()->children) {
                std::cout << "Query child: " << child->name << std::endl;

                if (child->name == "video_url") {
                    video_url = child->text_content;
                    std::cout << "Found video_url: " << video_url << std::endl;
                } else if (child->name == "commands") {
                    std::cout << "Found commands container with " << child->children.size() << " children" << std::endl;

                    for (const auto &cmd: child->children) {
                        if (cmd->name == "command") {
                            std::string cmd_id = cmd->attributes.contains("id") ? cmd->attributes.at("id") : "";
                            std::string cmd_name = cmd->text_content;
                            std::cout << "Found command: id=" << cmd_id << ", name=" << cmd_name << std::endl;

                            if (!cmd_id.empty()) {
                                commands.emplace_back(cmd_id, cmd_name);
                            }
                        }
                    }
                }
            }
        }

        std::cout << "Rover options fetched: video_url=" << video_url << ", commands count=" << commands.size() <<
                std::endl;
        callback(video_url, commands);
    });
}

constexpr std::string COMMAND_REQEST_TYPE = "set";

void send_command(saucer::smartview &webview, libstrophe_cpp *xmpp_client, std::string command_id) {
    auto command = make_iq_query(COMMAND_REQEST_TYPE, "query", command_id);
    command.attributes["to"] = std::format("{}@{}/{}", ROVER_LOCALPART, SERVER_TMP, ROVER_RESOURCE);

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

        if (response.attributes["type"] == "result") {
            responseLog << " sent successfully, got back status \"" << status << "\"";
        } else {
            responseLog << " got error status \"" << status << "\"";
        }

        std::cout << responseLog.str() << std::endl;
        webview.execute("addLog(new Date().toLocaleTimeString(), {})", responseLog.str());
    });
}

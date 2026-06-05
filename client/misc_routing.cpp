//
// Created by joseph on 5/27/26.
//
// This file contains routing functions for handling XMPP communication between
// the rover control client and the rover itself, including telemetry updates,
// server version queries, rover options fetching, and command sending.
//
#include <iostream>

#include "misc_routing.h"
#include "xmpp_iq.h"

// some bs to make clang-tidy more happy with the way saucer does js format strings
using saucer_serializer = saucer::serializers::glaze::serializer;
template<typename... Args>
using saucer_format_string = saucer::format_string<saucer_serializer, Args...>;

// Anonymous namespace for internal helper functions
// Keeps steady_now_ms() private to this translation unit
namespace {
    // Returns current time in milliseconds using steady clock
    // Uses steady_clock instead of system_clock because it's monotonic
    // and not affected by system time adjustments
    int64_t steady_now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
}

// Sets up a listener for incoming telemetry data from the rover
// Telemetry includes battery level, signal strength, and speed
// Updates are pushed to the webview UI and the last update timestamp is tracked
void initialize_telemetry_listener(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client,
    std::shared_ptr<std::atomic<int64_t> > last_telemetry_ms
) {
    // Register handler for XMPP IQ stanzas of type "set" with namespace "rover::telemetry"
    // The rover sends telemetry updates as IQ-set stanzas
    xmpp_client->set_iq_handler(
        "set", "rover::telemetry",
        [&webview, last_telemetry_ms]([[maybe_unused]] libstrophe_cpp *c, XmppNode request) {
            // Update the timestamp to track when we last received telemetry
            // Uses relaxed memory ordering since exact ordering isn't critical for this timestamp
            last_telemetry_ms->store(steady_now_ms(), std::memory_order_relaxed);

            std::cout << "=== Telemetry Update Received ===" << std::endl;

            // Parse the query element containing telemetry data
            // The query contains child elements for each telemetry metric
            if (auto query = request.find_child("query"); query.has_value()) {
                // Iterate through each telemetry stat (battery, signal, speed)
                for (const auto &stat: query.value()->children) {
                    webview.execute(
                        saucer_format_string<
                            const std::string &, const std::string &>{
                            "updateTelemetry({}, {})"
                        },
                        stat->name, stat->text_content
                    );
                }
            }

            // Build and return an IQ result response to acknowledge receipt
            // XMPP IQ stanzas of type "set" require a response
            XmppNode response = make_iq_query("result", "query", "rover::telemetry");
            // Set the response destination to the original sender
            response.attributes["to"] = request.attributes["from"];
            // Echo back the request ID so the sender can match request/response
            response.attributes["id"] = request.attributes["id"];

            return response;
        }
    );
}

// Queries the XMPP server for version information and logs it
// Uses the standard jabber:iq:version namespace defined in XEP-0092
// This helps verify server connectivity and identify the server software
void log_server_details(saucer::smartview &webview, libstrophe_cpp *xmpp_client) {
    // Create an IQ-get query using the standard jabber:iq:version namespace
    XmppNode version_req = make_iq_query("get", "query", "jabber:iq:version");
    // Address the query to the server domain itself (not a specific user)
    version_req.attributes["to"] = xmpp_client->domain;

    // Send the query and handle the response asynchronously
    xmpp_client->send_iq(version_req, [&webview]([[maybe_unused]] libstrophe_cpp *c, XmppNode response) {
        std::stringstream versionLog;
        versionLog << "Server Version:";

        // Check if the server successfully responded with version info
        if (response.attributes["type"] == "result") {
            // Parse the query element containing version details
            if (auto query = response.find_child("query"); query.has_value()) {
                // Iterate through version info fields (typically: name, version, os)
                for (const auto &info: query.value()->children) {
                    versionLog << ' ' << info->name << ": " << info->text_content << ';';
                }
            }
        } else {
            // Server doesn't support version queries or returned an error
            versionLog << " Version request failed or was not supported.";
        }

        // Log the result both to console and the webview UI
        std::cout << versionLog.str() << std::endl;
        webview.execute(
            saucer_format_string<const std::string &>{"addLog(new Date().toLocaleTimeString(), {})"},
            versionLog.str()
        );
    });
}

// Fetches rover configuration including video stream URL and available commands
// This is typically called once when connecting to populate the UI with controls
// The callback receives the parsed configuration data for UI initialization
void fetch_rover_options(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client,
    std::function<
        void(
            bool success,
            std::string video_url,
            std::vector<std::pair<std::string, std::string> > commands
        )> callback
) {
    // Create an IQ-get query using custom rover::getopts namespace
    XmppNode opts_req = make_iq_query("get", "query", "rover::getopts");
    // Address the query to the specific rover's full JID (localpart@domain/resource)
    // This ensures we target the correct rover instance
    // TODO: properly fetch these values
    opts_req.attributes["to"] = std::format("{}@{}/{}", ROVER_LOCALPART, xmpp_client->domain, ROVER_RESOURCE);

    std::cout << "Sending rover::getopts request to: " << opts_req.attributes["to"] << std::endl;

    // Send the query and handle the response asynchronously
    xmpp_client->send_iq(opts_req, [&webview, callback]([[maybe_unused]] libstrophe_cpp *c, XmppNode response) {
        std::cout << "Received response type: " << response.attributes["type"] << std::endl;

        // Check if the response indicates an error or non-result type
        // Only "result" type responses contain the requested data
        if (response.attributes["type"] != "result") {
            std::stringstream errorLog;
            errorLog << "Rover options request failed";

            // Append the response type if available for debugging
            if (response.attributes.contains("type")) {
                errorLog << " with response type \"" << response.attributes["type"] << "\"";
            }

            // Parse any error details from the response for better diagnostics
            if (auto error_node = response.find_child("error"); error_node.has_value()) {
                // Error type indicates the category (cancel, modify, etc.)
                if (error_node.value()->attributes.contains("type")) {
                    errorLog << ", error type \"" << error_node.value()->attributes["type"] << "\"";
                }

                // Error condition is the specific error reason (e.g., item-not-found)
                for (const auto &child: error_node.value()->children) {
                    errorLog << ", condition \"" << child->name << "\"";
                    break; // Only need the first condition
                }
            }

            // Log the error and notify caller via callback with failure status
            std::cout << errorLog.str() << std::endl;
            webview.execute(
                saucer_format_string<const std::string &>{"addLog(new Date().toLocaleTimeString(), {})"},
                errorLog.str()
            );

            callback(false, "", {});
            return;
        }

        // Initialize containers for parsed data
        std::string video_url;
        std::vector<std::pair<std::string, std::string> > commands;

        std::cout << "Processing result response..." << std::endl;
        // Parse the query element containing rover options
        if (auto query = response.find_child("query"); query.has_value()) {
            std::cout << "Found query node" << std::endl;

            // Iterate through options (video_url and commands list)
            for (const auto &child: query.value()->children) {
                std::cout << "Query child: " << child->name << std::endl;

                // Extract the video stream URL if present
                if (child->name == "video_url") {
                    video_url = child->text_content;
                    std::cout << "Found video_url: " << video_url << std::endl;
                } else if (child->name == "commands") {
                    // Parse the list of available commands
                    std::cout << "Found commands container with " << child->children.size() << " children" << std::endl;

                    // Each command has an ID (for sending) and a human-readable name
                    for (const auto &cmd: child->children) {
                        if (cmd->name == "command") {
                            // Extract command ID from attributes (used when sending commands)
                            std::string cmd_id = cmd->attributes.contains("id") ? cmd->attributes.at("id") : "";
                            // Extract display name from text content
                            std::string cmd_name = cmd->text_content;
                            std::cout << "Found command: id=" << cmd_id << ", name=" << cmd_name << std::endl;

                            // Only add valid commands with non-empty IDs
                            if (!cmd_id.empty()) {
                                commands.emplace_back(cmd_id, cmd_name);
                            }
                        }
                    }
                }
            }
        }

        // Log successful parsing and invoke callback with the extracted data
        std::cout << "Rover options fetched: video_url=" << video_url << ", commands count=" << commands.size() <<
                std::endl;
        callback(true, video_url, commands);
    });
}

// Commands are sent as IQ-set stanzas because they trigger actions
// Using "set" rather than "get" since we're commanding the rover, not querying state
constexpr std::string COMMAND_REQEST_TYPE = "set";

// Sends a command to the rover and logs the result
// Commands can be movement controls, camera operations, or any rover action
// The command_id corresponds to IDs from the rover options (e.g., "rover::forward")
void send_command(saucer::smartview &webview, libstrophe_cpp *xmpp_client, std::string command_id) {
    // Create an IQ-set query with the command ID as the namespace
    // This allows the rover to route different commands to appropriate handlers
    XmppNode command = make_iq_query(COMMAND_REQEST_TYPE, "query", command_id);
    // Address the command to the specific rover instance
    command.attributes["to"] = std::format("{}@{}/{}", ROVER_LOCALPART, xmpp_client->domain, ROVER_RESOURCE);

    // Send the command and handle the rover's response
    xmpp_client->send_iq(command, [&webview, command_id]([[maybe_unused]] libstrophe_cpp *c, XmppNode response) {
        std::stringstream responseLog;
        responseLog << command_id;

        // Try to extract the status message from the response
        // The rover may include execution status or error details
        auto query_node = response.find_child("query");
        std::string status = "(null)";
        if (query_node.has_value()) {
            auto status_node = query_node.value()->find_child("status");
            if (status_node.has_value()) {
                status = status_node.value()->text_content;
            }
        }

        // Check if the command was successfully executed
        // "result" means success, other types (e.g., "error") indicate failure
        if (response.attributes["type"] == "result") {
            responseLog << " sent successfully, got back status \"" << status << "\"";
        } else {
            responseLog << " got error status \"" << status << "\"";
        }

        // Log the command result both to console and the webview command log
        std::cout << responseLog.str() << std::endl;
        webview.execute(
            saucer_format_string<const std::string &>{"addLog(new Date().toLocaleTimeString(), {})"},
            responseLog.str()
        );
    });
}

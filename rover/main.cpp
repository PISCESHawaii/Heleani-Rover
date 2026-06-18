/**
 * Main (dummy for now) rover application for the PICSES Helelani Rover system.
 * 
 * This file implements the rover-side application that receives commands via XMPP.
 * It uses the libstrophe library to handle XMPP communication, allowing the rover to:
 * - Connect to the XMPP server using credentials from configuration file
 * - Respond to movement commands (forward, backward, left, right, stop)
 * - Send periodic telemetry data (battery, signal, speed) to the connected controller
 * - Handle rover options requests to establish controller connection
 * - Detect controller disconnection through telemetry acknowledgment timeouts
 * 
 * The application manages several concurrent threads:
 * - Main XMPP thread (libstrophe event loop)
 * - Telemetry generation thread (sends periodic status updates)
 * - Shutdown thread (handles graceful Ctrl+C termination)
 * 
 * Safety features:
 * - Telemetry timeout detection to identify lost controller connections
 * - Automatic telemetry disabling when controller becomes unresponsive
 * - Thread-safe state management with mutexes and atomic variables
 */

// standard library dependencies
#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <random>
#include <chrono>

// shared xmpp logic
#include "libstrophe_cpp.h"
#include "xmpp_node.h"
#include "xmpp_iq.h"

// Define how often the rover should send telemetry data to the controller
// This interval prevents network spam while keeping the controller updated
constexpr auto TELEMETRY_SEND_INTERVAL = std::chrono::seconds(3);

// Define how long to wait for a telemetry acknowledgment before timing out
// If no response is received within this time, the rover assumes connection loss
constexpr auto TELEMETRY_RESPONSE_TIMEOUT = std::chrono::seconds(15);

/**
 * Message Handler (Echo Bot Logic)
 * Now uses the clean XmppNode instead of raw pointers.
 */
void handle_message([[maybe_unused]] libstrophe_cpp *client, XmppNode stanza) {
    // Parse incoming XMPP message to extract the body content
    // Default to "no body" if the message doesn't contain a body element
    std::string message_text = "no body";

    // Iterate through all child elements of the message stanza
    for (const auto &child: stanza.children) {
        // Look for the "body" element which contains the actual message text
        if (child->name == "body") {
            message_text = child->text_content;
            break; // Stop searching once we find the body
        }
    }

    // Log any received messages that weren't handled by specific IQ handlers
    // This helps with debugging unexpected message types
    std::cout << "Received Unknown Message from " << stanza.attributes["from"] << ": \"" << message_text << "\"\n";
}

int main(int argc, char *argv[]) {
    // Check if the file path was passed as an argument
    // argc is at least 1 (the program name itself is argv[0])
    // The first argument passed by the user will be argv[1]
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_login.txt>" << std::endl;
        return 1;
    }

    // Load XMPP credentials from the configuration file path provided via argv[1]
    // This keeps sensitive login information out of the source code
    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        // Exit immediately if credentials file is missing or inaccessible
        std::cerr << "Could not open " << argv[1] << std::endl;
        return 1;
    }

    // Read JID (Jabber ID) from first line and password from second line
    // Format expected: line 1 = JID, line 2 = password
    std::string jid, password;
    std::getline(file, jid);
    std::getline(file, password);
    file.close();

    // Mutex to protect access to the controller's JID when multiple threads need it
    // Prevents race conditions when telemetry loop and IQ handlers access client_jid_opt
    std::mutex client_jid_mutex;

    // Stores the JID of the currently connected controller client
    // nullopt indicates no controller is connected, preventing telemetry from being sent
    std::optional<std::string> client_jid_opt = std::nullopt;

    // Flag indicating whether telemetry should be actively sent to the controller
    // Atomic to allow safe access from multiple threads without locking
    std::atomic_bool telemetry_enabled = false;

    // Tracks whether we're waiting for a telemetry acknowledgment from the controller
    // Used to implement timeout detection for lost connections
    std::atomic_bool telemetry_response_pending = false;

    // Timestamp of when the last telemetry packet was sent
    // Used to calculate elapsed time for timeout detection
    std::chrono::steady_clock::time_point last_telemetry_sent_at{};

    // Mutex to protect telemetry state variables as a group
    // Ensures atomic updates to all telemetry-related state together
    std::mutex telemetry_state_mutex;

    // Initialize the XMPP client with debug logging enabled
    // This handles all low-level XMPP protocol communication
    libstrophe_cpp lsc(XMPP_LEVEL_DEBUG, jid, password);

    // Global flag to coordinate shutdown across all threads
    // When set to false, all worker threads should terminate
    std::atomic_bool running = true;

    // Thread handle for the telemetry generation loop
    // Declared here but started later after connection is established
    std::thread pseudo_telemetry_loop;

    // Configure signal handling for graceful shutdown on Ctrl+C
    // Block SIGINT in the main thread so only the shutdown thread receives it
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);

    if (pthread_sigmask(SIG_BLOCK, &signal_set, nullptr) != 0) {
        // Critical failure if we can't set up signal handling
        std::cerr << "Failed to configure SIGINT handling" << std::endl;
        return 1;
    }

    // Create a dedicated thread to handle Ctrl+C gracefully
    // This allows the main XMPP event loop to continue while handling shutdown
    std::thread shutdown_thread([&]() {
        int signal = 0;
        // Wait for SIGINT (Ctrl+C) in this thread only
        if (sigwait(&signal_set, &signal) == 0 && signal == SIGINT) {
            std::cout << "\nCtrl+C received, disconnecting..." << std::endl;
            // Signal all threads to stop and disconnect from XMPP server
            running = false;
            lsc.disconnect();
        }
    });

    // Register handler for generic chat messages
    // Using nullopt for namespace catches all standard chat messages
    // This provides a fallback for any messages not handled by IQ handlers
    lsc.set_handler(std::nullopt, "message", "chat", handle_message);

    // Handle XMPP version query requests (XEP-0092)
    // This allows other clients to discover what software this rover is running
    // this is not currently used by anything
    lsc.set_iq_handler("get", "jabber:iq:version",
                       []([[maybe_unused]] libstrophe_cpp *c, XmppNode request) {
                           // Create the query element with the version namespace
                           XmppNode query("query");
                           query.attributes["xmlns"] = "jabber:iq:version";

                           // Build the response with software name, version, and OS
                           // These fields identify this rover to XMPP clients
                           XmppNode name("name");
                           name.text_content = "libstrophe_cpp_test";
                           XmppNode version("version");
                           version.text_content = "0.1";
                           XmppNode os("os");
                           os.text_content = "Linux";

                           // Assemble the query element with all version info
                           query.children.emplace_back(std::make_shared<XmppNode>(name));
                           query.children.emplace_back(std::make_shared<XmppNode>(version));
                           query.children.emplace_back(std::make_shared<XmppNode>(os));

                           // Create the IQ response stanza with required attributes
                           // The 'to' and 'id' must match the request for proper routing
                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           return response;
                       });

    // Register handlers for all rover movement commands
    // Each handler follows the same pattern: log command, create response, return acknowledgment

    // Handle forward movement command
    // In a real rover, this would activate forward motor control
    lsc.set_iq_handler("set", "rover::movements::forward",
                       []([[maybe_unused]] libstrophe_cpp *c, XmppNode request) {
                           // Log the received command for debugging and audit trail
                           std::cout << "Rover command: FORWARD" << std::endl;

                           // Build the response query with the same namespace as the command
                           XmppNode query("query");
                           query.attributes["xmlns"] = "rover::movements::forward";

                           // Include status message to confirm command execution
                           XmppNode status("status");
                           status.text_content = "Moving forward";
                           query.children.emplace_back(std::make_shared<XmppNode>(status));

                           // Create IQ result to acknowledge successful command processing
                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           return response;
                       });

    // Handle turn right command
    // This would control the rover's turning mechanism to rotate right
    lsc.set_iq_handler("set", "rover::movements::turn_right",
                       []([[maybe_unused]] libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Rover command: TURN RIGHT" << std::endl;

                           // Follow same response pattern as other movement commands
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

    // Handle backward movement command
    // This reverses the rover's direction
    lsc.set_iq_handler("set", "rover::movements::backward",
                       []([[maybe_unused]] libstrophe_cpp *c, XmppNode request) {
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

    // Handle left turn command
    // Rotates the rover counter-clockwise
    lsc.set_iq_handler("set", "rover::movements::left",
                       []([[maybe_unused]] libstrophe_cpp *c, XmppNode request) {
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

    // Handle stop command - critical for safety
    // should immediately halt all rover movement
    lsc.set_iq_handler("set", "rover::movements::stop",
                       []([[maybe_unused]] libstrophe_cpp *c, XmppNode request) {
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

    // Handle right turn command (clockwise rotation)
    // Similar to turn_right, but may have different implementation details
    lsc.set_iq_handler("set", "rover::movements::right",
                       []([[maybe_unused]] libstrophe_cpp *c, XmppNode request) {
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

    // Handle rover options request - this is the controller's handshake
    // When a controller requests options, it's registering itself for telemetry
    lsc.set_iq_handler("get", "rover::getopts",
                       [&]([[maybe_unused]] libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Rover options requested" << std::endl;

                           /* TODO: if theres already one associated, error */

                           // Store the requesting controller's JID for telemetry targeting
                           // This ensures telemetry is only sent to the active controller
                           {
                               std::lock_guard lock(client_jid_mutex);
                               client_jid_opt = request.attributes["from"];
                           }

                           // Activate telemetry and reset all state for the new controller session
                           // This ensures clean state when a new controller connects
                           {
                               std::lock_guard lock(telemetry_state_mutex);
                               telemetry_enabled = true;
                               telemetry_response_pending = false;
                               last_telemetry_sent_at = {};
                           }

                           std::cout << "Telemetry enabled for controller: " << request.attributes["from"] << std::endl;

                           // Build the response with rover capabilities and configuration
                           XmppNode query("query");
                           query.attributes["xmlns"] = "rover::getopts";

                           // Provide the video stream URL and name for the controller's camera feed
                           // This allows the UI to display live video from the rover with a descriptive name
                           XmppNode video_feed("video_feed");

                           XmppNode video_url("url");
                           video_url.attributes["id"] = "main_camera";
                           video_url.text_content = "https://dl.4d2.sh/tQvzglBcFLAk.mp4";
                           video_feed.children.emplace_back(std::make_shared<XmppNode>(video_url));

                           XmppNode video_url2("url");
                           video_url2.attributes["id"] = "secondary_camera";
                           video_url2.text_content =
                                   "https://dl.4d2.sh/USwyo5zmfw0P.mp4";
                           video_feed.children.emplace_back(std::make_shared<XmppNode>(video_url2));

                           query.children.emplace_back(std::make_shared<XmppNode>(video_feed));


                           // Container for all available movement commands
                           XmppNode commands("commands");

                           // List of all supported movement commands with their IDs and display names
                           // The controller will use this to build its UI controls
                           std::vector<std::pair<std::string, std::string> > available_commands = {
                               {"rover::movements::forward", "Forward"},
                               {"rover::movements::turn_right", "Turn Right"},
                               {"rover::movements::backward", "Backwards"},
                               {"rover::movements::left", "Left"},
                               {"rover::movements::stop", "Stop"},
                               {"rover::movements::right", "Right"}
                           };

                           // Convert the command list into XML nodes for the response
                           for (const auto &[cmd_id, cmd_name]: available_commands) {
                               XmppNode command("command");
                               command.attributes["id"] = cmd_id;
                               command.text_content = cmd_name;
                               commands.children.emplace_back(std::make_shared<XmppNode>(command));
                           }

                           query.children.emplace_back(std::make_shared<XmppNode>(commands));

                           // Assemble and send the complete options response
                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           response.children.emplace_back(std::make_shared<XmppNode>(query));
                           std::cout << "Rover options sent" << std::endl;
                           return response;
                       });

    // Handle XMPP ping requests (XEP-0199)
    // Pings are used to verify the connection is alive and measure latency
    lsc.set_iq_handler("get", "urn:xmpp:ping",
                       []([[maybe_unused]] libstrophe_cpp *c, XmppNode request) {
                           std::cout << "Ping request received from " << request.attributes["from"] << std::endl;

                           // Respond with an empty result IQ (a "pong")
                           // The empty response is sufficient to acknowledge the ping
                           XmppNode response("iq");
                           response.attributes["type"] = "result";
                           response.attributes["to"] = request.attributes["from"];
                           response.attributes["id"] = request.attributes["id"];
                           return response;
                       });


    std::cout << "Connecting to XMPP server as " << jid << "..." << std::endl;

    // Connect to the XMPP server and start the main event loop
    // The callback is invoked once connection is successfully established
    const int result = lsc.connect_noexcept(
        [&]() {
            // on success callback
            std::cout << "Connected!" << std::endl;

            // Start the telemetry generation thread after successful connection
            // This thread runs independently, generating and sending rover status updates
            pseudo_telemetry_loop = std::thread([&]() {
                // Initialize random number generator for simulated telemetry data
                // In a real rover, this would read actual sensor values
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution distrib(0, 99);

                // Main telemetry loop - runs until shutdown
                while (running) {
                    // Wait for the configured interval before sending next telemetry
                    std::this_thread::sleep_for(TELEMETRY_SEND_INTERVAL);

                    // Check if shutdown was requested during sleep
                    if (!running) break;

                    // Check telemetry state and handle timeout detection
                    {
                        std::lock_guard lock(telemetry_state_mutex);

                        // Skip this cycle if no controller has requested telemetry yet
                        if (!telemetry_enabled) {
                            continue;
                        }

                        // If waiting for a response, check if we've timed out
                        if (telemetry_response_pending) {
                            const auto elapsed = std::chrono::steady_clock::now() - last_telemetry_sent_at;

                            // If timeout hasn't occurred yet, skip this cycle
                            if (elapsed < TELEMETRY_RESPONSE_TIMEOUT) {
                                continue;
                            }

                            // Timeout detected - assume connection to controller is lost
                            // For safety, we should stop all rover movement here
                            std::cerr << "Telemetry response timed out." << std::endl;
                            std::cerr << "Would abort all current rover actions now." << std::endl;
                            std::cerr << "Stopping telemetry until a new rover::getopts request is received." <<
                                    std::endl;

                            // Disable telemetry and clear controller association
                            telemetry_enabled = false;
                            telemetry_response_pending = false; {
                                std::lock_guard client_lock(client_jid_mutex);
                                client_jid_opt = std::nullopt;
                            }

                            continue;
                        }
                    }

                    // Build the telemetry IQ stanza with rover status data
                    XmppNode telemetry_iq = make_iq_query("set", "query", "rover::telemetry");

                    // Set the destination to the registered controller
                    // Skip if no controller is registered (shouldn't happen due to earlier check)
                    {
                        std::lock_guard lock(client_jid_mutex);
                        if (!client_jid_opt) continue;
                        telemetry_iq.attributes["to"] = client_jid_opt.value();
                    }

                    // Add telemetry data fields to the query
                    const std::shared_ptr<XmppNode> querypart = telemetry_iq.find_child("query").value();

                    // Battery level (0-99) - simulated with random value
                    auto battery_node = std::make_shared<XmppNode>(XmppNode("battery"));
                    battery_node->text_content = std::to_string(distrib(gen));
                    querypart->children.emplace_back(battery_node);

                    // Signal strength (0-99) - simulated with random value
                    auto signal_node = std::make_shared<XmppNode>(XmppNode("signal"));
                    signal_node->text_content = std::to_string(distrib(gen));
                    querypart->children.emplace_back(signal_node);

                    // Current speed (0-99) - simulated with random value
                    auto speed_node = std::make_shared<XmppNode>(XmppNode("speed"));
                    speed_node->text_content = std::to_string(distrib(gen));
                    querypart->children.emplace_back(speed_node);


                    std::cout << "Sending telemetry [ID: " << telemetry_iq.attributes["id"] << "]..." <<
                            std::endl;

                    // Mark that we're waiting for a response and record the send time
                    {
                        std::lock_guard lock(telemetry_state_mutex);
                        telemetry_response_pending = true;
                        last_telemetry_sent_at = std::chrono::steady_clock::now();
                    }

                    // Send the telemetry IQ and register a callback for the response
                    lsc.send_iq(telemetry_iq, [&]([[maybe_unused]] libstrophe_cpp *c, XmppNode response) {
                        std::lock_guard lock(telemetry_state_mutex);

                        // Ignore late responses if telemetry was disabled due to timeout
                        if (!telemetry_enabled) {
                            std::cout << "Ignoring telemetry response because telemetry is disabled." << std::endl;
                            return;
                        }

                        // Clear the pending flag since we received a response
                        telemetry_response_pending = false;

                        std::cout << "=== Telemetry Response Received ===" << std::endl;

                        // Check if the controller acknowledged the telemetry successfully
                        if (response.attributes["type"] == "result") {
                            std::cout << "Telemetry acknowledged by controller." << std::endl;
                        } else {
                            // Controller rejected or couldn't process telemetry
                            // This indicates a protocol error or unsupported controller
                            std::cerr << "Telemetry request failed or was not supported." << std::endl;
                            std::cerr << "Would abort all current rover actions now." << std::endl;
                            std::cerr << "Stopping telemetry until a new rover::getopts request is received." <<
                                    std::endl;

                            // Disable telemetry and disconnect from this controller
                            telemetry_enabled = false; {
                                std::lock_guard client_lock(client_jid_mutex);
                                client_jid_opt = std::nullopt;
                            }
                        }
                    });
                }
            });
        },
        nullptr
    );

    // Ensure all threads are stopped after XMPP disconnection
    running = false;

    // Wait for the telemetry thread to finish its current cycle and exit
    // This ensures clean shutdown without cutting off mid-transmission
    if (pseudo_telemetry_loop.joinable()) {
        pseudo_telemetry_loop.join();
    }

    // Signal the shutdown thread to exit if it's still waiting for Ctrl+C
    // This handles the case where we disconnected without user intervention
    if (shutdown_thread.joinable()) {
        pthread_kill(shutdown_thread.native_handle(), SIGINT);
        shutdown_thread.join();
    }

    // Return the connection result code (0 for success, non-zero for errors)
    return result;
}

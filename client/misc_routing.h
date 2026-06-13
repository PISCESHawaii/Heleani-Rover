#ifndef HELELANIROVER_MISC_ROUTING_H
#define HELELANIROVER_MISC_ROUTING_H

#include <atomic>
#include <chrono>
#include <memory>

#include "saucer/smartview.hpp"
#include "libstrophe_cpp.h"

/**
 * @brief Registers an XMPP telemetry listener and forwards rover telemetry updates to the UI.
 *
 * Installs an IQ handler for incoming `set` stanzas in the `rover::telemetry`
 * namespace. When telemetry is received, the handler updates the provided
 * timestamp and forwards recognized telemetry fields to JavaScript functions in
 * the webview:
 *
 * - `battery` -> `updateBattery(...)`
 * - `signal` -> `updateSignal(...)`
 * - `speed` -> `updateSpeed(...)`
 *
 * Unknown telemetry fields are logged to the webview log. The handler returns an
 * IQ `result` response to acknowledge receipt of the telemetry stanza.
 *
 * @param webview The Saucer smartview used to execute UI update JavaScript.
 * @param xmpp_client The active XMPP client used to register the IQ handler.
 * @param last_telemetry_ms Shared atomic timestamp updated with the most recent
 * telemetry receive time, in monotonic milliseconds.
 */
void initialize_telemetry_listener(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client,
    std::shared_ptr<std::atomic<int64_t> > last_telemetry_ms
);

/**
 * @brief Queries the connected XMPP server for version information and logs the result.
 *
 * Sends an IQ `get` request to the server domain using the standard
 * `jabber:iq:version` namespace, as defined by XEP-0092. If the server responds
 * successfully, the returned version fields are formatted and logged to both
 * standard output and the webview log. If the request fails or is unsupported, a
 * failure message is logged instead.
 *
 * @param webview The Saucer smartview used to append log messages to the UI.
 * @param xmpp_client The active XMPP client used to send the server version IQ request.
 */
void log_server_details(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client
);

/**
 * @brief Fetches rover startup options, including the video stream URL and available commands.
 *
 * Sends an IQ `get` request in the `rover::getopts` namespace to the rover. On a
 * successful response, the function parses the returned options, including:
 *
 * - A `video_url` value used by the UI for the rover video stream.
 * - A list of available command IDs and display names.
 *
 * The operation is asynchronous. Results are returned through the supplied
 * callback. If the rover responds with an error or a non-`result` stanza, the
 * error is logged and the callback is invoked with `success == false`.
 *
 * @param webview The Saucer smartview used to append diagnostic log messages to the UI.
 * @param xmpp_client The active XMPP client used to send the rover options request.
 * @param callback Callback invoked when the request completes.
 * The callback receives:
 * - `success`: Whether the options were fetched successfully.
 * - `video_urls`: A list of video feed URL/name pairs, or an empty list on failure.
 * - `commands`: A list of command ID/display-name pairs, or an empty list on failure.
 */
void fetch_rover_options(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client,
    std::function<
        void(
            bool success,
            std::vector<std::pair<std::string, std::string> > video_urls,
            std::vector<std::pair<std::string, std::string> > commands
        )> callback
);

/**
 * @brief Sends a command IQ stanza to the rover and logs the response.
 *
 * Creates and sends an IQ `set` request whose namespace is the provided command
 * ID. This is used for rover actions such as movement, camera control, or other
 * commands advertised by the rover options response.
 *
 * The rover response is inspected for a `query/status` value. Successful
 * responses are logged as sent successfully; non-`result` responses are logged
 * as errors.
 *
 * @param webview The Saucer smartview used to append command result messages to the UI.
 * @param xmpp_client The active XMPP client used to send the command request.
 * @param command_id The rover command identifier, typically obtained from
 * `fetch_rover_options()`.
 */
void send_command(
    saucer::smartview &webview,
    libstrophe_cpp *xmpp_client,
    std::string command_id
);

#endif // HELELANIROVER_MISC_ROUTING_H

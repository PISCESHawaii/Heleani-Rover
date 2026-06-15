#ifndef LIBSTROPHE_CPP_TEST_LIBSTROPHE_CPP_H
#define LIBSTROPHE_CPP_TEST_LIBSTROPHE_CPP_H

#include <string>
#include <strophe.h>
#include <optional>
#include <functional>
#include <vector>
#include <tuple>
#include <mutex>
#include <queue>
#include <atomic>

#include "xmpp_node.h"

constexpr std::string_view ROVER_LOCALPART = "testing";
constexpr std::string_view ROVER_RESOURCE = "helelani";

/**
 * Represents a C++ wrapper for the libstrophe library to facilitate XMPP protocol handling.
 *
 * The `libstrophe_cpp` class provides an interface for creating and managing XMPP client connections,
 * handling events, and managing the lifecycle of XMPP streams.
 *
 * This class is designed to simplify interaction with the libstrophe library while ensuring a
 * user-friendly API for incorporating XMPP functionality into C++ applications.
 *
 * Key responsibilities include:
 * - Establishing and managing XMPP connections.
 * - Registering and invoking user-defined handlers for XMPP events.
 * - Abstracting libstrophe internals for ease of use in a modern C++ environment.
 *
 * Refer to the specific methods of this class for further details on the API.
 */
class libstrophe_cpp {
public:
    using StanzaHandler = std::function<void(libstrophe_cpp *, XmppNode)>;
    using IQHandler = std::function<XmppNode(libstrophe_cpp *, XmppNode)>;

    // The "Pattern": (Namespace, Name, Type)
    using HandlerCriteria = std::tuple<
        std::optional<std::string>,
        std::optional<std::string>,
        std::optional<std::string>
    >;

private:
    std::mutex queue_lock;
    std::mutex iq_lock;
    std::mutex lifecycle_lock;

    std::atomic<bool> disconnected{false};
    std::atomic<bool> should_disconnect{false};

    std::queue<XmppNode> outgoing_queue;

    struct HandlerEntry {
        HandlerCriteria criteria;
        StanzaHandler callback;
    };

    // internal constants of the xmpp connection
    const xmpp_log_t *log;
    xmpp_conn_t *conn;
    xmpp_ctx_t *ctx;
    std::string jid, pass;

    int conn_err = 0;
    int iq_id_counter = 0;

    std::function<void()> connect_callback_on_success;
    std::function<void(int, std::string)> connect_callback_on_failure;

    std::vector<HandlerEntry> handlers;
    std::unordered_map<std::string, IQHandler> iq_handlers;
    std::unordered_map<std::string, StanzaHandler> iq_response_handlers;

    //internal handler for things
    /**
     * Handles XMPP connection events such as connection, disconnection, and errors.
     *
     * @param conn Pointer to the XMPP connection object.
     * @param status The connection event status, e.g., connected, disconnected, or connection error.
     * @param error The error code, if any, during the connection event.
     * @param stream_error Pointer to additional stream error information, if present.
     * @param userdata User-defined data passed to the handler, typically the application context or object.
     */
    static void conn_handler(xmpp_conn_t *conn, xmpp_conn_event_t status, int error,
                             xmpp_stream_error_t *stream_error, void *userdata);

    static int global_stanza_handler([[maybe_unused]] xmpp_conn_t *conn, xmpp_stanza_t *stanza, void *userdata);

    static int internal_iq_handler([[maybe_unused]] xmpp_conn_t *conn, xmpp_stanza_t *stanza, void *userdata);

public:
    std::string localpart;
    std::string domain;

    libstrophe_cpp(xmpp_log_level_t log_level, const std::string &jid, const std::string &pass);

    //libstrophe specific deallocation
    ~libstrophe_cpp() {
        disconnect();
    }

    /**
     * Initiates an XMPP connection using the configured connection instance and runs the associated context.
     *
     * This method establishes a client connection to the XMPP server using the pre-configured connection object
     * and connection handler. Once the connection is established or attempted, the method executes the main
     * event loop for processing XMPP stanzas and events.
     *
     * returns the error state of the connection, 0 is a graceful exit.
     */
    int connect_noexcept(std::function<void()> OnSuccess, std::function<void(int, std::string)> OnFailure);

    void disconnect() {
        std::lock_guard lock(lifecycle_lock);
        if (disconnected || should_disconnect) return;

        // Let the event loop handle the actual teardown safely
        should_disconnect = true;
    }

    void send(const XmppNode &node);

    // The new pattern-matching handler
    void set_handler(std::optional<std::string> ns,
                     std::optional<std::string> name,
                     std::optional<std::string> type,
                     StanzaHandler handler);

    /**
     * Registers a handler for IQ stanzas that match a specific type and namespace.
     *
     * This method allows the user to specify a custom function to handle incoming IQ stanzas
     * based on the provided type and namespace. The handler will be invoked whenever
     * a matching IQ stanza is received.
     *
     * Thread safety is ensured by locking the IQ handler map during the registration process.
     *
     * @param type The type of the IQ stanza to match (e.g., "set" or "get").
     * @param ns The namespace of the IQ stanza to match.
     * @param handler The function to handle matching IQ stanzas. It is a callable that takes
     *                a pointer to the libstrophe_cpp instance and an XmppNode representing the incoming stanza,
     *                and returns an XmppNode as a response.
     */
    void set_iq_handler(std::string type, std::string ns, const IQHandler &handler);

    /**
     * Sends an IQ stanza to the XMPP server and registers a response handler for the associated stanza ID.
     *
     * This method constructs and sends an IQ stanza using the provided XmppNode, automatically assigning
     * a unique ID to the stanza if one is not already specified. The provided handler is registered to handle
     * the response for the corresponding stanza ID. The method ensures thread safety while managing the unique
     * ID generation and response handler registration.
     *
     * @param node The XmppNode representing the IQ stanza to be sent. It contains the attributes and structure
     *             of the stanza, including type, namespace, and payload.
     * @param handler A StanzaHandler callback to handle the response associated with the stanza ID. The handler
     *                will be invoked when a response is received or if an error occurs.
     * @return The unique ID of the IQ stanza that was sent.
     * @throws std::runtime_error If the XMPP connection is not established.
     */
    std::string send_iq(XmppNode node, StanzaHandler handler);
};

#endif

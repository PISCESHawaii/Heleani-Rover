#ifndef LIBSTROPHE_CPP_TEST_LIBSTROPHE_CPP_H
#define LIBSTROPHE_CPP_TEST_LIBSTROPHE_CPP_H

#include <string>
#include <strophe.h>
#include <optional>
#include <functional>
#include <vector>
#include <tuple>

#include "xmpp_node.h"

//foward declaration
class xmpp_stanza;

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
    std::mutex send_lock;
    std::mutex iq_lock;

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

    static int global_stanza_handler(xmpp_conn_t *conn, xmpp_stanza_t *stanza, void *userdata);

    static int internal_iq_handler(xmpp_conn_t *conn, xmpp_stanza_t *stanza, void *userdata);

public:
    std::string localpart;
    std::string domain;

    libstrophe_cpp(xmpp_log_level_t log_level, const std::string &jid, const std::string &pass);

    //libstrophe specific deallocation
    ~libstrophe_cpp();

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
        if (conn) xmpp_disconnect(conn);
        if (ctx) xmpp_stop(ctx);
    }

    void send(const XmppNode &node);

    // The new pattern-matching handler
    void set_handler(std::optional<std::string> ns,
                     std::optional<std::string> name,
                     std::optional<std::string> type,
                     StanzaHandler handler);

    void set_iq_handler(std::string type, std::string ns, const IQHandler &handler);

    std::string send_iq(XmppNode node, StanzaHandler handler);
};

#endif

#include "libstrophe_cpp.h"
#include <iostream>

libstrophe_cpp::libstrophe_cpp(xmpp_log_level_t log_level, const std::string &jid, const std::string &pass)
    : jid(jid), pass(pass) {
    // Initialize the XMPP library
    xmpp_initialize();

    // Set up logging
    log = xmpp_get_default_logger(log_level);

    // create a new xmpp context
    ctx = xmpp_ctx_new(nullptr, log);

    // create a new xmpp connection
    conn = xmpp_conn_new(ctx);

    // Set connection credentials
    xmpp_conn_set_jid(conn, jid.c_str());
    xmpp_conn_set_pass(conn, pass.c_str());
}

// Clean up resources
libstrophe_cpp::~libstrophe_cpp() {
    if (conn) xmpp_conn_release(conn);
    if (ctx) xmpp_ctx_free(ctx);
    xmpp_shutdown();
}

/**
 * The Pattern Matcher:
 * Returns true if the criteria matches the stanza's attributes.
 * nullopt acts as a wildcard (*).
 */
bool is_match(const libstrophe_cpp::HandlerCriteria &criteria, const XmppNode &node) {
    const auto &[req_ns, req_name, req_type] = criteria;

    if (req_name && *req_name != node.name) return false;

    // Check Namespace (stored in attributes or specialized field)
    auto actual_ns = node.attributes.contains("xmlns") ? node.attributes.at("xmlns") : "";
    if (req_ns && *req_ns != actual_ns) return false;

    // Check Type attribute (standard for XMPP message/presence/iq)
    auto actual_type = node.attributes.contains("type") ? node.attributes.at("type") : "";
    if (req_type && *req_type != actual_type) return false;

    return true;
}

int libstrophe_cpp::global_stanza_handler(xmpp_conn_t *conn, xmpp_stanza_t *raw, void *userdata) {
    auto *self = static_cast<libstrophe_cpp *>(userdata);
    XmppNode node = XmppNode::from_libstrophe(raw);

    for (const auto &entry: self->handlers) {
        if (is_match(entry.criteria, node)) {
            entry.callback(self, node);
        }
    }
    return 1; // Keep handler active
}

void libstrophe_cpp::set_handler(std::optional<std::string> ns,
                                 std::optional<std::string> name,
                                 std::optional<std::string> type,
                                 StanzaHandler handler) {
    handlers.push_back({{ns, name, type}, std::move(handler)});

    // Register the catch-all dispatcher with libstrophe if not already done
    // We use NULL filters so libstrophe sends EVERYTHING to our C++ matcher
    xmpp_handler_add(conn, global_stanza_handler,
                     ns ? ns->c_str() : nullptr,
                     name ? name->c_str() : nullptr,
                     nullptr, this);
}

void libstrophe_cpp::send(const XmppNode &node) const {
    if (conn) {
        xmpp_stanza_t *raw = node.to_libstrophe(ctx);
        xmpp_send(conn, raw);
        xmpp_stanza_release(raw);
    }
}

int libstrophe_cpp::connect_noexcept() {
    xmpp_conn_set_jid(conn, jid.c_str());
    xmpp_conn_set_pass(conn, pass.c_str());
    xmpp_connect_client(conn, nullptr, 0, conn_handler, this);
    xmpp_run(ctx);
    return conn_err;
}

void libstrophe_cpp::conn_handler(xmpp_conn_t *conn, xmpp_conn_event_t status, int error,
                                  xmpp_stream_error_t *stream_error, void *userdata) {
    auto *that = static_cast<libstrophe_cpp *>(userdata);
    if (status == XMPP_CONN_CONNECT) {
        xmpp_stanza_t *pres = xmpp_presence_new(that->ctx);
        xmpp_send(conn, pres);
        xmpp_stanza_release(pres);

        /*
         * so if i only specify "iq" it works
         * if i add a type, it only picks up for that type (seemingly inconsistent with normal handlers)
         * if i register a handler multiple times with different types they conflict and only the last one is kept
         * so i just leave it as an iq with everything else as nullptr and i will personally beat
         * anyone who changes this (incl. myself) over the head with a frying pan.
         */
        xmpp_handler_add(conn, internal_iq_handler, nullptr, "iq", nullptr, that);
    } else {
        xmpp_stop(that->ctx);
    }
}

int libstrophe_cpp::internal_iq_handler(xmpp_conn_t *, xmpp_stanza_t *raw, void *userdata) {
    auto *self = static_cast<libstrophe_cpp *>(userdata);
    const std::string id = xmpp_stanza_get_id(raw);
    const std::string type = xmpp_stanza_get_type(raw);

    if (id == "") return 1;

    // swallow response iqs and pass to the callback lambda
    if (
        (type == "result" || type == "error") &&
        self->iq_response_handlers.contains(id)
    ) {
        self->iq_response_handlers[id](self, XmppNode::from_libstrophe(raw));
        self->iq_response_handlers.erase(id);
        return 1;
    }

    // get the namespace to call the right handler
    xmpp_stanza_t *child = xmpp_stanza_get_children(raw);
    const std::string ns = child ? xmpp_stanza_get_ns(child) : nullptr;

    // find the appropriate iq handler
    if (self->iq_handlers.contains(std::format("{}:{}", type, ns))) {
        const XmppNode response = self->iq_handlers[std::format("{}:{}", type, ns)]
                (self, XmppNode::from_libstrophe(raw));
        self->send(response);
    }

    return 1;
}

void libstrophe_cpp::set_iq_handler(std::string type, std::string ns, const IQHandler &handler) {
    iq_handlers[std::format("{}:{}", type, ns)] = handler;
}

std::string libstrophe_cpp::send_iq(XmppNode node, StanzaHandler handler) {
    if (node.attributes["id"].empty()) {
        node.attributes["id"] = "iq_" + std::to_string(++iq_id_counter);
    }
    std::string id = node.attributes["id"];
    iq_response_handlers[id] = std::move(handler);
    send(node);
    return id;
}

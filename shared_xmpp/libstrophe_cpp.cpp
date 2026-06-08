#include "libstrophe_cpp.h"

#include <iostream>
#include <ranges>

libstrophe_cpp::libstrophe_cpp(xmpp_log_level_t log_level, const std::string &jid, const std::string &pass)
    : jid(jid), pass(pass) {
    if (jid.empty() || pass.empty()) {
        throw std::invalid_argument("JID and password must be provided");
    }
    auto jidParts = jid | std::views::split('@');
    int part_idx = 0;

    for (const auto &part: jidParts) {
        switch (part_idx) {
            case 0:
                localpart = std::string{part.begin(), part.end()};
                break;
            case 1:
                domain = std::string{part.begin(), part.end()};
                break;
            default:
                throw std::invalid_argument("JID must be in the form <localpart>@<domain>");
        }
        part_idx++;
    }

    if (localpart.empty() || domain.empty()) {
        throw std::invalid_argument("JID must be in the form <localpart>@<domain>");
    }

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

void libstrophe_cpp::send(const XmppNode &node) {
    if (!conn) throw std::runtime_error("Not connected");
    std::lock_guard lock(queue_lock);
    outgoing_queue.push(node);
}

int libstrophe_cpp::connect_noexcept(std::function<void()> OnSuccess, std::function<void(int, std::string)> OnFailure) {
    connect_callback_on_success = std::move(OnSuccess);
    connect_callback_on_failure = std::move(OnFailure);
    xmpp_conn_set_jid(conn, jid.c_str());
    xmpp_conn_set_pass(conn, pass.c_str());

    xmpp_connect_client(conn, nullptr, 0, conn_handler, this);

    // Thread-safe Custom Event Loop
    while (!disconnected) {
        // 1. Process incoming network events
        xmpp_run_once(ctx, 50);

        // 2. Check if a disconnect was requested safely
        {
            std::lock_guard lock(lifecycle_lock);
            if (should_disconnect && conn) {
                xmpp_disconnect(conn);
                should_disconnect = false; // Trigger teardown once
            }
        }

        // 3. Process outgoing message queue ON THE EVENT LOOP THREAD
        {
            std::lock_guard lock(queue_lock);
            while (!outgoing_queue.empty()) {
                XmppNode node = outgoing_queue.front();
                outgoing_queue.pop();

                xmpp_stanza_t *raw = node.to_libstrophe(ctx);
                xmpp_send(conn, raw);
                xmpp_stanza_release(raw);
            }
        }
    }

    // --- LOOP HAS FINISHED. SAFE TO CLEAN UP ---
    if (conn) {
        xmpp_conn_release(conn);
        conn = nullptr;
    }
    if (ctx) {
        xmpp_ctx_free(ctx);
        ctx = nullptr;
    }
    xmpp_shutdown();

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

        if (that->connect_callback_on_success)
            that->connect_callback_on_success();
        else
            std::cout << "Connection successful & no callback provided" << std::endl;
    } else {
        std::string detailed_reason = "Unknown connection error";

        // 1. Check for XMPP Stream Errors (Server-sent reasons)
        if (stream_error) {
            // stream_error->text is the human-readable part
            // stream_error->cnd is the XMPP condition (e.g., "not-authorized")
            detailed_reason = stream_error->text
                                  ? std::string(stream_error->text)
                                  : "Stream error: Unknown condition";
        }
        // 2. Handle specific libstrophe internal failures
        else if (error != 0) {
            detailed_reason = std::format("System error code: {}", error);
        }

        that->conn_err = error;
        xmpp_stop(that->ctx);

        // This will cleanly break the custom event loop on the next iteration
        that->disconnected = true;

        if (that->connect_callback_on_failure) {
            // You can pass the string to your frontend here!
            that->connect_callback_on_failure(error, detailed_reason);
        }
    }
}

int libstrophe_cpp::internal_iq_handler(xmpp_conn_t *, xmpp_stanza_t *raw, void *userdata) {
    auto *self = static_cast<libstrophe_cpp *>(userdata);

    // Safely extract C-strings, falling back to empty strings if NULL
    const char *raw_id = xmpp_stanza_get_id(raw);
    const char *raw_type = xmpp_stanza_get_type(raw);
    const std::string id = raw_id ? raw_id : "";
    const std::string type = raw_type ? raw_type : "";

    if (id.empty()) return 1;

    // swallow response iqs and pass to the callback lambda safely
    if (type == "result" || type == "error") {
        StanzaHandler callback;
        bool found = false;

        // Scope the lock so we don't deadlock if the callback calls send_iq
        {
            std::lock_guard lock(self->iq_lock);
            if (self->iq_response_handlers.contains(id)) {
                callback = std::move(self->iq_response_handlers[id]);
                self->iq_response_handlers.erase(id);
                found = true;
            }
        }

        if (found) {
            callback(self, XmppNode::from_libstrophe(raw));
            return 1;
        }
    }

    // get the namespace to call the right handler SAFELY
    xmpp_stanza_t *child = xmpp_stanza_get_children(raw);
    const char *raw_ns = child ? xmpp_stanza_get_ns(child) : nullptr;
    const std::string ns = raw_ns ? raw_ns : "";

    // find the appropriate iq handler safely
    {
        std::lock_guard lock(self->iq_lock);
        if (self->iq_handlers.contains(std::format("{}:{}", type, ns))) {
            const XmppNode response = self->iq_handlers[std::format("{}:{}", type, ns)]
                    (self, XmppNode::from_libstrophe(raw));
            self->send(response);
        }
    }

    return 1;
}

void libstrophe_cpp::set_iq_handler(std::string type, std::string ns, const IQHandler &handler) {
    std::lock_guard lock(iq_lock);
    iq_handlers[std::format("{}:{}", type, ns)] = handler;
}

std::string libstrophe_cpp::send_iq(XmppNode node, StanzaHandler handler) {
    if (!conn) throw std::runtime_error("Not connected");

    std::string id; {
        std::lock_guard lock(iq_lock);
        if (node.attributes["id"].empty()) {
            node.attributes["id"] = "iq_" + std::to_string(++iq_id_counter);
        }
        id = node.attributes["id"];
        iq_response_handlers[id] = std::move(handler);
    }

    send(node);
    return id;
}

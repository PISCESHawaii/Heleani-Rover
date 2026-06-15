#ifndef LIBSTROPHE_CPP_TEST_XMPP_IQ_H
#define LIBSTROPHE_CPP_TEST_XMPP_IQ_H

#include "xmpp_node.h"
#include <atomic>
#include <iomanip>
#include <sstream>

/**
 * Generates a unique identifier for an XMPP IQ stanza.
 *
 * This method uses an atomic counter to ensure thread-safe incrementing of
 * the sequence number. The resulting identifier is formatted as a string
 * in the form of "iq-0x<hexadecimal_value>", where <hexadecimal_value> is an
 * 8-character, zero-padded hexadecimal representation of the current sequence value.
 *
 * @return A string representing the unique IQ stanza identifier, formatted as "iq-0x<hexadecimal_value>".
 */
inline std::string get_next_iq_id() {
    static std::atomic<uint32_t> sequence{1000};
    const uint32_t val = sequence.fetch_add(1);

    std::stringstream ss;
    ss << "iq-0x" << std::hex << std::setw(8) << std::setfill('0') << val;
    return ss.str();
}

/**
 * A specialized builder for XMPP IQ stanzas.
 * Returns a clean XmppNode pre-configured as a 'get' or 'set' request.
 */
inline XmppNode make_iq_query(const std::string &type,
                              const std::string &query_name,
                              const std::string &xmlns) {
    XmppNode iq("iq");
    iq.attributes["type"] = type;
    iq.attributes["id"] = get_next_iq_id();

    // Add the internal <query> or specialized child node
    XmppNode query(query_name);
    query.attributes["xmlns"] = xmlns;

    iq.children.emplace_back(std::make_shared<XmppNode>(std::move(query)));
    return iq;
}

#endif

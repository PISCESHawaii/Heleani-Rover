#ifndef LIBSTROPHE_CPP_TEST_XMPP_IQ_H
#define LIBSTROPHE_CPP_TEST_XMPP_IQ_H

#include "xmpp_node.h"
#include <atomic>
#include <iomanip>
#include <sstream>

// Keep your clean ID generator!
inline std::string get_next_iq_id() {
    static std::atomic<uint32_t> sequence{1000};
    uint32_t val = sequence.fetch_add(1);

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

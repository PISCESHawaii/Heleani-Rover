//
// Created by joseph on 3/26/26.
//

#include "xmpp_node.h"

std::vector<XmppNode *> XmppNode::find_all(const std::string &tag_name, size_t limit) {
    std::vector<XmppNode *> results;
    for (auto &child: children) {
        if (child.name == tag_name) {
            results.push_back(&child);
            if (limit > 0 && results.size() == limit) break; // Optimization: stop early
        }
    }
    return results;
}

xmpp_stanza_t *XmppNode::to_libstrophe(xmpp_ctx_t *ctx) const {
    xmpp_stanza_t *s = xmpp_stanza_new(ctx);
    if (!name.empty()) xmpp_stanza_set_name(s, name.c_str());

    // Attributes
    for (auto const &[key, val]: attributes) {
        xmpp_stanza_set_attribute(s, key.c_str(), val.c_str());
    }

    // Text Content
    if (!text_content.empty()) {
        xmpp_stanza_t *text_node = xmpp_stanza_new(ctx);
        xmpp_stanza_set_text(text_node, text_content.c_str());
        xmpp_stanza_add_child(s, text_node);
        xmpp_stanza_release(text_node);
    }

    // Children (Recursive)
    for (const auto &child: children) {
        xmpp_stanza_t *c_child = child.to_libstrophe(ctx);
        xmpp_stanza_add_child(s, c_child);
        xmpp_stanza_release(c_child); // Parent now owns it
    }

    return s;
}

XmppNode XmppNode::from_libstrophe(xmpp_stanza_t *s) {
    if (!s) throw std::invalid_argument("Stanza cannot be null");

    const char *raw_name = xmpp_stanza_get_name(s);
    XmppNode node(raw_name ? raw_name : "");

    // Attributes
    int attr_count = xmpp_stanza_get_attribute_count(s);
    if (attr_count > 0) {
        auto attrs = std::make_unique<const char *[]>(attr_count * 2);
        xmpp_stanza_get_attributes(s, attrs.get(), attr_count * 2);
        for (int i = 0; i < attr_count * 2; i += 2) {
            node.attributes[attrs[i]] = attrs[i + 1];
        }
    }

    // Children & Text
    xmpp_stanza_t *child = xmpp_stanza_get_children(s);
    while (child) {
        if (xmpp_stanza_is_text(child)) {
            char *text = xmpp_stanza_get_text(child);
            if (text) {
                node.text_content += text;
                xmpp_free(xmpp_stanza_get_context(s), text);
            }
        } else {
            node.children.push_back(XmppNode::from_libstrophe(child));
        }
        child = xmpp_stanza_get_next(child);
    }

    return node;
}

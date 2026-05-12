//
// Created by joseph on 3/26/26.
//

#ifndef XMPP_NODE_H
#define XMPP_NODE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <strophe.h>

/**
 * Represents an XMPP Node with hierarchical structure, attributes, and optional text content.
 * Provides utility methods for transformation and navigation between XMPP nodes.
 */
class XmppNode {
public:
    std::string name;
    std::string text_content;
    std::unordered_map<std::string, std::string> attributes;
    std::vector<XmppNode> children;

    XmppNode(std::string n = "") : name(std::move(n)) {
    }

    /**
     * Searches for the first child node with the specified tag name.
     *
     * Compares the `name` property of each child node in the `children` vector
     * against the given `tag_name`. If a match is found, a pointer to the
     * corresponding child node is returned. If no match is found, `nullptr`
     * is returned.
     *
     * @param tag_name The name of the child node to search for.
     * @return A pointer to the first child node matching the specified tag name,
     *         or nullptr if no match is found.
     */
    std::optional<XmppNode> find_child(const std::string &tag_name) {
        for (auto child: children) {
            if (child.name == tag_name) return child;
        }
        return std::nullopt;
    }

    /**
     * Finds all child nodes with the specified tag name, up to an optional limit.
     *
     * Iterates through the `children` vector and compares the `name` property
     * of each child node to the provided `tag_name`. Matches are added to the
     * result vector. If a non-zero limit is specified, the search stops as
     * soon as the limit is reached.
     *
     * @param tag_name The name of the child nodes to search for.
     * @param limit The maximum number of matching nodes to return.
     *              If set to 0, all matching nodes are returned.
     * @return A vector containing pointers to the matching child nodes.
     *         If no match is found, the vector will be empty.
     */
    std::vector<XmppNode *> find_all(const std::string &tag_name, size_t limit = 0);

    /**
     * Converts the XMPP node and its hierarchical structure into a libstrophe stanza.
     * Constructs a new libstrophe stanza object and recursively sets its name, attributes,
     * text content, and children based on the current XMPP node.
     *
     * @param ctx A pointer to the libstrophe context used for memory allocation and stanza creation.
     * @return A pointer to the newly created libstrophe stanza.
     *         The caller is responsible for releasing the stanza after use.
     */
    xmpp_stanza_t *to_libstrophe(xmpp_ctx_t *ctx) const;

    /**
     * Constructs an XmppNode object from a given libstrophe stanza.
     *
     * This method parses a libstrophe `xmpp_stanza_t` structure and maps its
     * properties, including name, attributes, text content, and child stanzas,
     * into an XmppNode object. If the input stanza pointer is null, an XmppNode
     * with the name "error" is returned.
     *
     * @param s A pointer to the libstrophe stanza to be converted into an XmppNode.
     *          If the pointer is null, a default error node is returned.
     * @return An XmppNode representing the converted structure of the given stanza.
     */
    static XmppNode from_libstrophe(xmpp_stanza_t *s);
};

#endif

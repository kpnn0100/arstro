/*
 *  solaris_model — the song as nodes: canonical `.slp` text read into a table and written back exactly (values kept as
 *  written, quotes and all). The header is the node `project`; every node with an `id` is keyed by it; a rack by its
 *  strip (`rack:<track>`); a formula by its address (`bind:<address>`); a node's id-less lines (a pattern's notes, an
 *  automation's points, a clip's takes) are its BODY, one unit; a rack's devices its children, in order. Shared by the
 *  versions (R-VER, Versions.cpp) and the merge (R-COLLAB-1, Merge.cpp), so the two read a song alike.
 */
#pragma once
#include "Project.h"
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
namespace nodes
{
    struct Node
    {
        std::string key, tag;
        Fields fields;                  // raw values; a bare token has an empty value
        std::string comment;            // an inline comment — kept, never versioned
        std::vector<std::string> remarks; // whole comment lines after it — kept, never versioned
        std::vector<std::string> body;  // its id-less lines, indented as written less its children's indent
        std::vector<std::string> kids;  // its child nodes (a rack's devices), in order
    };

    struct Table
    {
        std::vector<std::string> top;   // top-level keys in order; "project" (the header) first
        std::map<std::string, Node> nodes;
    };

    std::string trimmed(const std::string &s);
    std::string unquoted(const std::string &v);
    /** `#tag k=v k="a b" bare  ; comment` → its tag, raw fields and comment. */
    void splitLine(const std::string &t, std::string &tag, Fields &fields, std::string &comment);
    const std::string *fieldOf(const Fields &f, const std::string &k);
    /** A node's name: its id; a rack by its strip; a formula by its address; else the line itself. */
    std::string keyOf(const std::string &tag, const Fields &f, const std::string &line);
    std::string nodeLine(const Node &n);
    std::string headerLine(const std::string &key, const std::string &value);
    size_t indentOf(const std::string &l);
    Table readTable(const std::string &text);
    void writeNode(const Table &t, const Node &n, const std::string &indent, std::string &out);
    std::string writeTable(const Table &t);
    void eraseNode(Table &t, const std::string &key);
}
}
}

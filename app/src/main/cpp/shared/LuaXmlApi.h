#pragma once

#include <string>

struct lua_State;
namespace WorldRPG::Script {
void InstallXmlLua(lua_State* L);
// Parses an XML document as xmlLoadString does. Returns the root node, the number scripts hold
// as an xml node, or 0 when the text does not parse.
int XmlParseString(const std::string& text);
// Checks a document without retaining temporary nodes in the XML handle table.
bool XmlValidateString(const std::string& text);
// The XML text of the node at this stack index. False when it is not a live xml node.
bool XmlNodeString(lua_State* L, int index, std::string& out);
}

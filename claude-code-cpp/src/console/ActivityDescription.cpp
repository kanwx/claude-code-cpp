#include <claude/console/ActivityDescription.hpp>
#include <nlohmann/json.hpp>

namespace claude {

using json = nlohmann::json;

namespace {

struct ToolDescriptor {
    const char* activeVerb;   // "Reading"
    const char* pastVerb;     // "Read"
    const char* inputKey;     // "file_path"
    const char* secondaryKey; // nullptr or "path", "query", etc.
    bool isQuoted;            // true = wrap value in quotes
    bool appendAgentNoun;     // true = append " agent" (for Agent tool)
};

const std::unordered_map<String, ToolDescriptor> kToolDescriptors = {
    {"Read",          {"Reading",       "Read",          "file_path",  nullptr,      false, false}},
    {"Write",         {"Writing",       "Wrote",         "file_path",  nullptr,      false, false}},
    {"Edit",          {"Editing",       "Edited",        "file_path",  nullptr,      false, false}},
    {"NotebookEdit",  {"Editing",       "Edited",        "notebook_path", nullptr,   false, false}},
    {"Bash",          {"Running",       "Ran",           "command",    nullptr,      false, false}},
    {"Grep",          {"Searching for", "Searched for",  "pattern",    "path",       true,  false}},
    {"Glob",          {"Finding",       "Found",         "pattern",    nullptr,      false, false}},
    {"WebFetch",      {"Fetching",      "Fetched",       "url",        nullptr,      false, false}},
    {"WebSearch",     {"Searching",     "Searched",      "query",      nullptr,      true,  false}},
    {"Agent",         {"Running",       "Ran",           "agent_type", nullptr,      false, true}},
    {"LSP",           {"LSP",           "LSP",           "file_path",  nullptr,      false, false}},
    {"MCP",           {"Calling MCP",   "Called MCP",    "tool_name",  nullptr,      false, false}},
};

String extractJsonString(const String& jsonStr, const String& key) {
    try {
        auto j = json::parse(jsonStr);
        if (j.contains(key) && j[key].is_string()) {
            return j[key].get<String>();
        }
        if (j.contains(key)) {
            return j[key].dump();
        }
    } catch (...) {}
    return {};
}

int extractJsonInt(const String& jsonStr, const String& key) {
    try {
        auto j = json::parse(jsonStr);
        if (j.contains(key) && j[key].is_number_integer()) {
            return j[key].get<int>();
        }
    } catch (...) {}
    return -1;
}

} // anonymous namespace

String getActivityDescription(const String& toolName, const String& jsonInput, bool active) {
    auto it = kToolDescriptors.find(toolName);
    if (it == kToolDescriptors.end()) {
        return active ? ("Running " + toolName) : ("Ran " + toolName);
    }

    const auto& desc = it->second;
    String verb = active ? desc.activeVerb : desc.pastVerb;
    String primaryValue = extractJsonString(jsonInput, desc.inputKey);

    if (primaryValue.empty()) {
        String result = verb + " " + toolName;
        if (desc.appendAgentNoun) result += " agent";
        return result;
    }

    String result;

    // Special case: Read with offset/limit
    if (toolName == "Read") {
        int offset = extractJsonInt(jsonInput, "offset");
        int limit = extractJsonInt(jsonInput, "limit");
        if (offset >= 0 && limit >= 0) {
            result = verb + " " + primaryValue + ":" + std::to_string(offset) + "-" + std::to_string(offset + limit);
        } else if (offset >= 0) {
            result = verb + " " + primaryValue + ":" + std::to_string(offset);
        } else {
            result = verb + " " + primaryValue;
        }
    } else if (desc.isQuoted) {
        result = verb + " \"" + primaryValue + "\"";
    } else {
        result = verb + " " + primaryValue;
    }

    // Append secondary key value if present
    if (desc.secondaryKey) {
        String secondaryValue = extractJsonString(jsonInput, desc.secondaryKey);
        if (!secondaryValue.empty()) {
            if (desc.isQuoted) {
                // For Grep: "Searching for \"TODO\" in src/"
                // Need to rebuild with secondary
                result = verb + " \"" + primaryValue + "\" in " + secondaryValue;
            } else {
                result += " in " + secondaryValue;
            }
        }
    }

    // Append " agent" for Agent tool
    if (desc.appendAgentNoun) {
        result += " agent";
    }

    return result;
}

} // namespace claude

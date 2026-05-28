#include <ontology/LlmBackend.hpp>
#include <httplib.h>
#include <spdlog/spdlog.h>

namespace ontology {

HttpLlmBackend::HttpLlmBackend(const Config& config)
    : config_(config)
{
}

Json HttpLlmBackend::buildTextMessages(const String& prompt, const String& systemPrompt) {
    Json messages = Json::array();
    if (!systemPrompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", systemPrompt}});
    }
    messages.push_back({{"role", "user"}, {"content", prompt}});
    return messages;
}

Json HttpLlmBackend::buildMultimodalMessages(
    const String& prompt,
    const String& imageBase64,
    const String& mediaType
) {
    Json messages = Json::array();
    Json content = Json::array();
    content.push_back({{"type", "text"}, {"text", prompt}});
    content.push_back({
        {"type", "image_url"},
        {"image_url", {{"url", "data:" + mediaType + ";base64," + imageBase64}}}
    });
    messages.push_back({{"role", "user"}, {"content", content}});
    return messages;
}

String HttpLlmBackend::complete(const String& prompt, const String& systemPrompt) {
    Json body;
    body["model"] = config_.model;
    body["messages"] = buildTextMessages(prompt, systemPrompt);
    body["max_tokens"] = config_.maxTokens;
    body["temperature"] = config_.temperature;

    return callApi(body);
}

String HttpLlmBackend::completeWithImage(
    const String& prompt,
    const String& imageBase64,
    const String& mediaType
) {
    if (!config_.enableImage) {
        return complete(prompt);
    }

    Json body;
    body["model"] = config_.model;
    body["messages"] = buildMultimodalMessages(prompt, imageBase64, mediaType);
    body["max_tokens"] = config_.maxTokens;
    body["temperature"] = config_.temperature;

    return callApi(body);
}

bool HttpLlmBackend::isAvailable() const {
    try {
        String host = config_.endpoint;
        int port = 8000;

        if (host.substr(0, 8) == "https://") host = host.substr(8);
        else if (host.substr(0, 7) == "http://") host = host.substr(7);

        auto colonPos = host.find(':');
        if (colonPos != String::npos) {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        }

        httplib::Client client(host, port);
        client.set_read_timeout(5);
        client.set_connection_timeout(5);

        // Try /v1/models or /models endpoint
        auto res = client.Get("/v1/models");
        if (res && res->status == 200) return true;

        // Fallback: try root
        res = client.Get("/");
        return res && (res->status == 200 || res->status == 404);
    } catch (const std::exception& e) {
        spdlog::error("RAG error: {}", e.what());
        return false;
    }
}

String HttpLlmBackend::callApi(const Json& body) {
    try {
        String host = config_.endpoint;
        int port = 8000;

        if (host.substr(0, 8) == "https://") host = host.substr(8);
        else if (host.substr(0, 7) == "http://") host = host.substr(7);

        auto colonPos = host.find(':');
        if (colonPos != String::npos) {
            port = std::stoi(host.substr(colonPos + 1));
            host = host.substr(0, colonPos);
        }

        httplib::Client client(host, port);
        client.set_read_timeout(config_.timeoutMs / 1000);

        httplib::Headers headers = {
            {"Content-Type", "application/json"}
        };
        if (!config_.apiKey.empty()) {
            headers.emplace("Authorization", "Bearer " + config_.apiKey);
        }

        auto res = client.Post(config_.apiPath, headers, body.dump(), "application/json");
        if (!res || res->status != 200) return {};

        Json resp = Json::parse(res->body);
        if (!resp.contains("choices") || resp["choices"].empty()) return {};

        auto& choice = resp["choices"][0];
        if (choice.contains("message") && choice["message"].contains("content")) {
            return choice["message"]["content"].get<String>();
        }

        return {};
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("RAG JSON error: {}", e.what());
        return {};
    } catch (const std::exception& e) {
        spdlog::error("RAG error: {}", e.what());
        return {};
    }
}

} // namespace ontology

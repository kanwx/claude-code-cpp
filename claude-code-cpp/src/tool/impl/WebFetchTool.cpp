#include <claude/tool/impl/WebFetchTool.hpp>
#include <claude/utils/Http.hpp>
#include <regex>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace claude {

namespace {

/// Case-insensitive string comparison helper
bool iEquals(const String& a, const String& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

/// Extract the host portion from a URL (after ://, before port or path)
String extractHost(const String& url) {
    // Find "://"
    auto schemeEnd = url.find("://");
    size_t start = (schemeEnd != String::npos) ? schemeEnd + 3 : 0;

    // Find end of host (port or path)
    size_t end = url.find_first_of(":/", start);
    if (end == String::npos) {
        end = url.size();
    }

    String host = url.substr(start, end - start);

    // Convert to lowercase for comparison
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return host;
}

/// Check if a host is a private/internal network address or metadata endpoint
bool isPrivateNetworkHost(const String& host) {
    // localhost variants
    if (host == "localhost" || host == "localhost.localdomain") {
        return true;
    }

    // 127.x.x.x  (loopback)
    if (host.size() >= 4 && host.substr(0, 4) == "127.") {
        return true;
    }

    // 10.x.x.x  (private class A)
    if (host.size() >= 3 && host.substr(0, 3) == "10.") {
        return true;
    }

    // 172.16.x.x through 172.31.x.x  (private class B)
    if (host.size() >= 7 && host.substr(0, 4) == "172.") {
        // Extract the third octet
        auto firstDot = host.find('.', 4);  // after "172."
        if (firstDot != String::npos) {
            String octet2 = host.substr(4, firstDot - 4);
            try {
                int val = std::stoi(octet2);
                if (val >= 16 && val <= 31) return true;
            } catch (...) {}
        }
    }

    // 192.168.x.x  (private class C)
    if (host.size() >= 8 && host.substr(0, 8) == "192.168") {
        return true;
    }

    // 169.254.x.x  (link-local / metadata)
    if (host.size() >= 8 && host.substr(0, 8) == "169.254.") {
        return true;
    }

    // Cloud metadata endpoints
    if (host == "169.254.169.254" ||
        iEquals(host, "metadata.google.internal") ||
        iEquals(host, "metadata.azure.com")) {
        return true;
    }

    // .internal TLD (cloud-internal DNS)
    if (host.size() > 9 && host.substr(host.size() - 9) == ".internal") {
        return true;
    }

    // .local TLD (mDNS)
    if (host.size() > 6 && host.substr(host.size() - 6) == ".local") {
        return true;
    }

    return false;
}

} // anonymous namespace

PermissionResult WebFetchTool::checkPermission(const Json& input, ToolContext& context) {
    String url = input.value("url", "");
    if (url.empty()) {
        return PermissionResult::deny("No URL specified");
    }

    String host = extractHost(url);
    if (host.empty()) {
        return PermissionResult::deny("Could not parse host from URL: " + url);
    }

    if (isPrivateNetworkHost(host)) {
        return PermissionResult::deny("Fetching private/internal network URLs is blocked: " + url);
    }

    return PermissionResult::allow();
}

namespace {
    // Remove content between tags
    String removeTagContent(const String& html, const String& tagName) {
        String result = html;
        String openTag = "<" + tagName;
        String closeTag = "</" + tagName + ">";

        size_t pos = 0;
        while ((pos = result.find(openTag, pos)) != String::npos) {
            size_t endPos = result.find(closeTag, pos);
            if (endPos != String::npos) {
                result.erase(pos, endPos - pos + closeTag.length());
            } else {
                break;
            }
        }
        return result;
    }

    // HTML to text conversion
    String htmlToText(String html) {
        // Remove script, style, nav, header, footer blocks
        html = removeTagContent(html, "script");
        html = removeTagContent(html, "style");
        html = removeTagContent(html, "nav");
        html = removeTagContent(html, "header");
        html = removeTagContent(html, "footer");

        // Convert block elements to newlines
        html = std::regex_replace(html, std::regex(R"(<br\s*/?\s*>)", std::regex::icase), "\n");
        html = std::regex_replace(html, std::regex(R"(</p>)", std::regex::icase), "\n\n");
        html = std::regex_replace(html, std::regex(R"(</div>)", std::regex::icase), "\n");
        html = std::regex_replace(html, std::regex(R"(</h[1-6]>)", std::regex::icase), "\n\n");
        html = std::regex_replace(html, std::regex(R"(<li[^>]*>)", std::regex::icase), "\n  - ");
        html = std::regex_replace(html, std::regex(R"(</li>)", std::regex::icase), "\n");

        // Preserve links: <a href="url">text</a> → [text](url)
        html = std::regex_replace(html,
            std::regex(R"re(<a\s+[^>]*href="([^"]*)"[^>]*>([^<]*)</a>)re", std::regex::icase),
            "[$2]($1)");

        // Preserve image alt text: <img alt="desc"> → [Image: desc]
        html = std::regex_replace(html,
            std::regex(R"re(<img\s+[^>]*alt="([^"]*)"[^>]*/?\s*>)re", std::regex::icase),
            "[Image: $1]");

        // Preserve code blocks: <pre><code>...</code></pre> → ```\n...\n```
        html = std::regex_replace(html,
            std::regex(R"(<pre[^>]*>\s*<code[^>]*>)", std::regex::icase), "```\n");
        html = std::regex_replace(html,
            std::regex(R"(</code>\s*</pre>)", std::regex::icase), "\n```");

        // Convert headings to markdown format
        html = std::regex_replace(html, std::regex(R"(<h1[^>]*>)", std::regex::icase), "# ");
        html = std::regex_replace(html, std::regex(R"(<h2[^>]*>)", std::regex::icase), "## ");
        html = std::regex_replace(html, std::regex(R"(<h3[^>]*>)", std::regex::icase), "### ");
        html = std::regex_replace(html, std::regex(R"(<h4[^>]*>)", std::regex::icase), "#### ");

        // Remove all remaining HTML tags
        html = std::regex_replace(html, std::regex(R"(<[^>]+>)"), "");

        // Decode common HTML entities
        html = std::regex_replace(html, std::regex(R"(&nbsp;)"), " ");
        html = std::regex_replace(html, std::regex(R"(&amp;)"), "&");
        html = std::regex_replace(html, std::regex(R"(&lt;)"), "<");
        html = std::regex_replace(html, std::regex(R"(&gt;)"), ">");
        html = std::regex_replace(html, std::regex(R"(&quot;)"), "\"");
        html = std::regex_replace(html, std::regex(R"(&apos;)"), "'");
        html = std::regex_replace(html, std::regex(R"(&#39;)"), "'");
        html = std::regex_replace(html, std::regex(R"(&mdash;)"), "--");
        html = std::regex_replace(html, std::regex(R"(&ndash;)"), "-");
        html = std::regex_replace(html, std::regex(R"(&hellip;)"), "...");
        // Numeric entities: &#NNN;
        {
            std::string result;
            std::regex numEntity(R"(&#(\d+);)");
            std::smatch m;
            std::string s = html;
            while (std::regex_search(s, m, numEntity)) {
                result += m.prefix();
                try {
                    int code = std::stoi(m[1].str());
                    if (code >= 32 && code < 127) {
                        result += static_cast<char>(code);
                    } else {
                        result += m[0].str();
                    }
                } catch (...) {
                    result += m[0].str();
                }
                s = m.suffix();
            }
            result += s;
            html = result;
        }

        // Clean up whitespace
        html = std::regex_replace(html, std::regex(R"(\n\s*\n\s*\n)"), "\n\n");
        html = std::regex_replace(html, std::regex(R"(^[ \t]+)"), "", std::regex_constants::format_first_only);
        html = std::regex_replace(html, std::regex(R"([ \t]+$)"), "", std::regex_constants::format_first_only);

        return html;
    }

    // Extract title from HTML
    String extractTitle(const String& html) {
        std::regex titleRegex(R"(<title[^>]*>([^<]+)</title>)", std::regex::icase);
        std::smatch match;
        if (std::regex_search(html, match, titleRegex)) {
            return match[1].str();
        }
        return "";
    }

    // Find body content
    String extractBody(const String& html) {
        size_t bodyStart = html.find("<body");
        if (bodyStart == String::npos) return html;

        size_t bodyEnd = html.find("</body>", bodyStart);
        if (bodyEnd == String::npos) return html;

        // Skip to after the body tag attributes
        size_t contentStart = html.find('>', bodyStart) + 1;
        if (contentStart == String::npos) return html;

        return html.substr(contentStart, bodyEnd - contentStart);
    }
}

String WebFetchTool::execute(const Json& input, ToolContext& context) {
    String url = input["url"];
    String prompt = input.value("prompt", "Extract the main content");

    auto response = Http::get(url);
    if (!response) {
        return "Error: Failed to fetch URL: " + response.error();
    }

    if (response->status != 200) {
        return "Error: HTTP status " + std::to_string(response->status);
    }

    String& html = response->body;

    // Extract title
    String title = extractTitle(html);

    // Extract body content
    String body = extractBody(html);

    // Convert to text
    String text = htmlToText(body);

    // Truncate if too long
    const size_t maxLength = 15000;
    if (text.length() > maxLength) {
        text = text.substr(0, maxLength) + "\n\n... (content truncated)";
    }

    std::ostringstream oss;
    oss << "=== Web Content ===\n\n";
    oss << "URL: " << url << "\n";
    if (!title.empty()) {
        oss << "Title: " << title << "\n";
    }
    oss << "\n--- Content ---\n\n";
    oss << text << "\n";

    return oss.str();
}

} // namespace claude

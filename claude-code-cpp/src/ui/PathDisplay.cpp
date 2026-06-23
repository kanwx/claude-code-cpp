#include "claude/ui/PathDisplay.hpp"

namespace claude {

String truncatePathForDisplay(const String& path, size_t maxWidth) {
    if (path.empty() || path.size() <= maxWidth) return path;

    // Split into components
    std::vector<String> parts;
    size_t start = 0;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (i > start) parts.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() <= 2) return path;  // too few parts to shorten

    bool isAbsolute = !path.empty() && path[0] == '/';
    const String& filename = parts.back();
    const String& parent = parts[parts.size() - 2];
    const String& root = parts[0];

    // Build: root/…/parent/filename
    String result;
    if (isAbsolute) {
        result = "/" + root + "/\xe2\x80\xa6/" + parent + "/" + filename;
    } else {
        result = root + "/\xe2\x80\xa6/" + parent + "/" + filename;
    }

    if (result.size() <= maxWidth) return result;

    // Still too long: drop parent, keep only root/…/filename
    if (isAbsolute) {
        result = "/" + root + "/\xe2\x80\xa6/" + filename;
    } else {
        result = root + "/\xe2\x80\xa6/" + filename;
    }

    if (result.size() <= maxWidth) return result;

    // Still too long: drop root too, just …/filename
    result = "\xe2\x80\xa6/" + filename;
    if (result.size() <= maxWidth) return result;

    // Desperate: truncate the filename itself
    auto dotPos = filename.rfind('.');
    String ext = (dotPos != String::npos && dotPos > 0) ? filename.substr(dotPos) : "";
    String base = filename.substr(0, dotPos == String::npos ? filename.size() : dotPos);
    size_t overhead = 2 + ext.size();  // "…" + ext
    if (maxWidth > overhead + 3) {
        size_t keep = maxWidth - overhead;
        if (keep < base.size()) {
            result = "\xe2\x80\xa6/" + base.substr(base.size() - keep) + ext;
        } else {
            result = "\xe2\x80\xa6/" + filename;
        }
    }

    return result;
}

} // namespace claude

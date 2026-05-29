#include <claude/tool/FileIndex.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace claude {

void FileIndex::buildIndex(const String& rootDir, int maxDepth) {
    clear();

    if (!fs::exists(rootDir)) {
        spdlog::warn("FileIndex: root dir does not exist: {}", rootDir);
        return;
    }

    int count = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(rootDir,
            fs::directory_options::skip_permission_denied)) {
            if (--maxDepth < 0) break;
            if (entry.is_regular_file()) {
                addFile(entry.path().string());
                count++;
                if (count > 100000) {
                    spdlog::debug("FileIndex: hit 100k file limit, stopping");
                    break;
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        spdlog::warn("FileIndex: error during scan: {}", e.what());
    }

    spdlog::debug("FileIndex: indexed {} files from {}", count, rootDir);
}

void FileIndex::addFile(const String& path) {
    String fname = filename(path);
    pathToFilename_[path] = fname;
    filenameToPaths_[fname].push_back(path);
}

void FileIndex::removeFile(const String& path) {
    auto it = pathToFilename_.find(path);
    if (it == pathToFilename_.end()) return;

    String fname = it->second;
    pathToFilename_.erase(it);

    auto& paths = filenameToPaths_[fname];
    paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
    if (paths.empty()) {
        filenameToPaths_.erase(fname);
    }
}

void FileIndex::clear() {
    pathToFilename_.clear();
    filenameToPaths_.clear();
}

size_t FileIndex::size() const {
    return pathToFilename_.size();
}

bool FileIndex::contains(const String& path) const {
    return pathToFilename_.count(path) > 0;
}

std::vector<String> FileIndex::search(const String& query, int maxResults) const {
    std::vector<String> results;

    // Try glob match first
    if (query.find('*') != String::npos || query.find('?') != String::npos) {
        for (const auto& [path, fname] : pathToFilename_) {
            if (globMatch(query, path) || globMatch(query, fname)) {
                results.push_back(path);
                if (static_cast<int>(results.size()) >= maxResults) break;
            }
        }
        return results;
    }

    // Exact filename match
    auto it = filenameToPaths_.find(query);
    if (it != filenameToPaths_.end()) {
        for (const auto& p : it->second) {
            results.push_back(p);
            if (static_cast<int>(results.size()) >= maxResults) break;
        }
        if (!results.empty()) return results;
    }

    // Substring search on filenames
    String lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

    for (const auto& [path, fname] : pathToFilename_) {
        String lowerFname = fname;
        std::transform(lowerFname.begin(), lowerFname.end(), lowerFname.begin(), ::tolower);
        if (lowerFname.find(lowerQuery) != String::npos) {
            results.push_back(path);
            if (static_cast<int>(results.size()) >= maxResults) break;
        }
    }

    // Also search in full paths
    if (static_cast<int>(results.size()) < maxResults) {
        for (const auto& [path, fname] : pathToFilename_) {
            String lowerPath = path;
            std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
            if (lowerPath.find(lowerQuery) != String::npos) {
                // Avoid duplicates
                if (std::find(results.begin(), results.end(), path) == results.end()) {
                    results.push_back(path);
                    if (static_cast<int>(results.size()) >= maxResults) break;
                }
            }
        }
    }

    return results;
}

std::vector<std::pair<String, double>> FileIndex::fuzzySearch(const String& query, int maxResults) const {
    std::vector<std::pair<String, double>> scored;

    String lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

    for (const auto& [path, fname] : pathToFilename_) {
        // Score against filename (higher weight)
        double filenameScore = fuzzyScore(lowerQuery, fname);
        // Score against path (lower weight)
        double pathScore = fuzzyScore(lowerQuery, path) * 0.5;
        double best = std::max(filenameScore, pathScore);

        if (best > 0.1) {  // threshold
            scored.emplace_back(path, best);
        }
    }

    // Sort by score descending
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (static_cast<int>(scored.size()) > maxResults) {
        scored.resize(maxResults);
    }
    return scored;
}

double FileIndex::fuzzyScore(const String& query, const String& target) {
    if (query.empty()) return 1.0;
    if (target.empty()) return 0.0;

    String lowerTarget = target;
    std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::tolower);

    // Exact match
    if (lowerTarget == query) return 1.0;

    // Substring match
    auto pos = lowerTarget.find(query);
    if (pos != String::npos) {
        // Higher score for matches at the start, or shorter targets
        double positionBonus = 1.0 - static_cast<double>(pos) / lowerTarget.length();
        double lengthRatio = static_cast<double>(query.length()) / lowerTarget.length();
        return 0.7 + 0.3 * positionBonus * lengthRatio;
    }

    // Subsequence match (characters in order, not necessarily contiguous)
    size_t qi = 0;
    int matchCount = 0;
    int gapTotal = 0;

    for (size_t ti = 0; ti < lowerTarget.length() && qi < query.length(); ++ti) {
        if (lowerTarget[ti] == query[qi]) {
            matchCount++;
            qi++;
        } else if (matchCount > 0) {
            gapTotal++;
        }
    }

    if (qi < query.length()) return 0.0;  // Not all query chars matched

    double matchRatio = static_cast<double>(matchCount) / query.length();
    double gapPenalty = gapTotal > 0 ? 1.0 / (1.0 + gapTotal) : 1.0;
    return matchRatio * gapPenalty * 0.5;
}

String FileIndex::filename(const String& path) {
    auto pos = path.find_last_of("/\\");
    return pos != String::npos ? path.substr(pos + 1) : path;
}

bool FileIndex::globMatch(const String& pattern, const String& text) {
    // Simple glob: * = any chars, ? = one char
    size_t pi = 0, ti = 0;
    size_t starPos = String::npos;
    size_t matchPos = 0;

    while (ti < text.length()) {
        if (pi < pattern.length() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            pi++; ti++;
        } else if (pi < pattern.length() && pattern[pi] == '*') {
            starPos = pi++;
            matchPos = ti;
        } else if (starPos != String::npos) {
            pi = starPos + 1;
            ti = ++matchPos;
        } else {
            return false;
        }
    }

    while (pi < pattern.length() && pattern[pi] == '*') pi++;
    return pi == pattern.length();
}

} // namespace claude

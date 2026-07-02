#include <claude/tool/impl/FileReadTool.hpp>
#include <claude/tool/MultimodalProcessor.hpp>
#include <claude/utils/FileCache.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <array>
#include <spdlog/spdlog.h>

namespace claude {

/// Check if a file extension indicates a PDF
static bool isPdfFile(const String& path) {
    return path.size() >= 4 &&
           path.compare(path.size() - 4, 4, ".pdf") == 0;
}

/// Read a PDF file using pdftotext subprocess
static String readPdfFile(const std::filesystem::path& path, int maxPages) {
    // Check if pdftotext is available
    auto* pipe = popen("which pdftotext 2>/dev/null", "r");
    if (!pipe) {
        return "Error: Cannot check for pdftotext. Install poppler-utils to read PDF files.";
    }
    std::array<char, 256> buf;
    bool hasPdftotext = fgets(buf.data(), buf.size(), pipe) != nullptr;
    pclose(pipe);

    if (!hasPdftotext) {
        return "Error: pdftotext not found. Install poppler-utils (macOS: brew install poppler) to read PDF files.";
    }

    // Build pdftotext command
    std::ostringstream cmd;
    cmd << "pdftotext -l " << maxPages << " -layout '"
        << path.string() << "' - 2>/dev/null";

    auto* pdfPipe = popen(cmd.str().c_str(), "r");
    if (!pdfPipe) {
        return "Error: Failed to run pdftotext";
    }

    std::string result;
    while (fgets(buf.data(), buf.size(), pdfPipe)) {
        result += buf.data();
    }
    int exitCode = pclose(pdfPipe);

    if (exitCode != 0 || result.empty()) {
        return "Error: pdftotext failed to extract text from: " + path.string();
    }

    return result;
}

String FileReadTool::execute(const Json& input, ToolContext& context) {
    String filePath = input["file_path"];
    int offset = input.value("offset", 0);
    int limit = input.value("limit", 2000);

    std::filesystem::path path(filePath);
    if (!path.is_absolute()) {
        path = context.workDir / path;
    }

    if (!std::filesystem::exists(path)) {
        return "Error: File does not exist: " + path.string();
    }

    // ========== Image file handling (via MultimodalProcessor) ==========
    if (MultimodalProcessor::isImageFile(path)) {
        auto imgResult = MultimodalProcessor::processImage(path);
        if (!imgResult) {
            return "Error: " + imgResult.error();
        }
        // Return a marker that AgentLoop/API client can detect and convert to image block
        std::ostringstream oss;
        oss << "[IMAGE: " << path.string()
            << " | format=" << MultimodalProcessor::formatToMediaType(imgResult->format)
            << " | size=" << imgResult->originalSize
            << " | width=" << imgResult->width
            << " | height=" << imgResult->height
            << " | tokens=" << MultimodalProcessor::estimateImageTokens(imgResult->width, imgResult->height)
            << " | data=" << imgResult->base64Data << "]";
        return oss.str();
    }

    // ========== PDF file handling ==========
    if (isPdfFile(path.string())) {
        return readPdfFile(path, 20);  // max 20 pages
    }

    // ========== Text file handling ==========
    // Stream-based reading to avoid loading very large files entirely into memory.
    // For files under 1MB, use the file cache; for larger files, read line-by-line.
    constexpr size_t MAX_CACHED_SIZE = 1024 * 1024; // 1 MB
    auto fileSize = std::filesystem::file_size(path);

    std::stringstream output;
    output << "Contents of " << path.string() << ":\n\n";

    if (fileSize <= MAX_CACHED_SIZE && offset == 0 && limit >= 2000) {
        // Small file: use cache
        String cacheKey = path.string();
        String content;
        auto cached = FileCache::instance().get(cacheKey);
        if (cached) {
            content = *cached;
        } else {
            std::ifstream file(path);
            if (!file) return "Error: Cannot open file: " + path.string();
            std::stringstream ss;
            ss << file.rdbuf();
            content = ss.str();
            FileCache::instance().set(cacheKey, content);
        }

        std::istringstream contentStream(content);
        String line;
        int lineNum = 0;
        int count = 0;
        while (std::getline(contentStream, line)) {
            lineNum++;
            if (count >= limit) {
                output << "\n... (truncated, showing " << limit << " of " << lineNum << " lines)";
                break;
            }
            output << std::setw(6) << lineNum << "\t" << line << "\n";
            count++;
        }
    } else {
        // Large file or offset-based: stream line-by-line to bound memory
        std::ifstream file(path);
        if (!file) return "Error: Cannot open file: " + path.string();

        String line;
        int lineNum = 0;
        int count = 0;
        // Skip to offset
        while (lineNum < offset && std::getline(file, line)) {
            lineNum++;
        }
        if (lineNum < offset) {
            return "Error: File has only " + std::to_string(lineNum) + " lines, offset " +
                   std::to_string(offset) + " is out of range";
        }
        // Read up to limit lines
        while (std::getline(file, line)) {
            lineNum++;
            if (count >= limit) {
                output << "\n... (truncated at line " << lineNum << ", use offset/limit to read more)";
                break;
            }
            // Truncate very long lines (> 2000 chars) to bound memory
            if (line.size() > 2000) {
                output << std::setw(6) << lineNum << "\t" << line.substr(0, 2000)
                       << " ... (line truncated)\n";
            } else {
                output << std::setw(6) << lineNum << "\t" << line << "\n";
            }
            count++;
        }
    }

    return output.str();
}

String FileReadTool::executeStreaming(const Json& input, ToolContext& context,
                                      ChunkCallback onChunk) {
    String filePath = input["file_path"];
    int offset = input.value("offset", 0);
    int limit = input.value("limit", 2000);

    std::filesystem::path path(filePath);
    if (!path.is_absolute()) {
        path = context.workDir / path;
    }

    if (!std::filesystem::exists(path)) {
        String err = "Error: File does not exist: " + path.string();
        if (onChunk) onChunk(err);
        return err;
    }

    // Images and PDFs don't benefit from chunked streaming
    if (MultimodalProcessor::isImageFile(path)) {
        String result = execute(input, context);
        if (onChunk) onChunk(result);
        return result;
    }
    if (isPdfFile(path.string())) {
        String result = readPdfFile(path, 20);
        if (onChunk) onChunk(result);
        return result;
    }

    // ========== Text file: chunked streaming ==========
    constexpr int CHUNK_LINES = 100;
    constexpr size_t MAX_LINE_LEN = 2000;

    std::ifstream file(path);
    if (!file) {
        String err = "Error: Cannot open file: " + path.string();
        if (onChunk) onChunk(err);
        return err;
    }

    String fullResult;
    String chunk;
    chunk.reserve(CHUNK_LINES * 120); // rough estimate per line

    // Header
    String header = "Contents of " + path.string() + ":\n\n";
    if (onChunk) {
        if (!onChunk(header)) return header;
    }
    fullResult += header;

    String line;
    int lineNum = 0;
    int count = 0;
    int chunkLineCount = 0;

    // Skip to offset
    while (lineNum < offset && std::getline(file, line)) {
        lineNum++;
    }
    if (lineNum < offset) {
        String err = "Error: File has only " + std::to_string(lineNum) +
                     " lines, offset " + std::to_string(offset) + " is out of range";
        if (onChunk) onChunk(err);
        return err;
    }

    // Read lines, flushing chunks periodically
    while (std::getline(file, line)) {
        lineNum++;
        if (count >= limit) {
            String trailer = "\n... (truncated at line " + std::to_string(lineNum) +
                             ", use offset/limit to read more)";
            chunk += trailer;
            fullResult += trailer;
            break;
        }

        if (line.size() > MAX_LINE_LEN) {
            std::ostringstream lined;
            lined << std::setw(6) << lineNum << "\t"
                  << line.substr(0, MAX_LINE_LEN) << " ... (line truncated)\n";
            chunk += lined.str();
        } else {
            std::ostringstream lined;
            lined << std::setw(6) << lineNum << "\t" << line << "\n";
            chunk += lined.str();
        }

        count++;
        chunkLineCount++;

        if (chunkLineCount >= CHUNK_LINES) {
            fullResult += chunk;
            if (onChunk) {
                if (!onChunk(chunk)) return fullResult;
            }
            chunk.clear();
            chunkLineCount = 0;
        }
    }

    // Flush remaining
    if (!chunk.empty()) {
        fullResult += chunk;
        if (onChunk) onChunk(chunk);
    }

    return fullResult;
}

ToolResultSummary FileReadTool::renderToolResult(const String& result, bool isError,
                                      bool isCancelled, bool isRejected) const {
    if (isError) return ToolResultSummary::error("Error reading file");
    if (isCancelled || isRejected) return ToolResultSummary{};
    int lines = 0;
    for (char c : result) { if (c == '\n') lines++; }
    if (!result.empty() && result.back() != '\n') lines++;
    if (lines == 0) lines = 1;

    // Thread-safe: extract file path from result string, not from shared member.
    // Result starts with "Contents of <path>:\n\n" for text files,
    // "[IMAGE: <path> | ..." for images.
    // Downstream formatToolResult expects primaryText = "N lines" (number first)
    // and secondaryText = " from <path>" (with " from " prefix for cleanFilePath).
    String secondary;
    const String contentsPrefix = "Contents of ";
    if (result.size() > contentsPrefix.size() &&
        result.compare(0, contentsPrefix.size(), contentsPrefix) == 0) {
        auto colon = result.find(":\n", contentsPrefix.size());
        if (colon != String::npos) {
            secondary = " from " + result.substr(contentsPrefix.size(),
                                                  colon - contentsPrefix.size());
        }
    } else if (result.size() > 8 && result.compare(0, 8, "[IMAGE: ") == 0) {
        auto pipePos = result.find(" |", 8);
        if (pipePos != String::npos) {
            secondary = " from " + result.substr(8, pipePos - 8);
        }
    }

    // Build content preview: extract body after the "Contents of <path>:\n\n" header
    String contentPreview;
    bool truncated = false;
    int previewLines = 0;
    auto bodyStart = result.find("\n\n");
    if (bodyStart != String::npos && bodyStart + 2 < result.size()) {
        String body = result.substr(bodyStart + 2);
        // Truncate to max 20 lines or 2000 chars
        static constexpr int kMaxPreviewLines = 20;
        static constexpr int kMaxPreviewChars = 2000;
        int lineCount = 0;
        size_t cutPos = 0;
        for (size_t i = 0; i < body.size() && lineCount < kMaxPreviewLines && i < kMaxPreviewChars; ++i) {
            if (body[i] == '\n') lineCount++;
            cutPos = i + 1;
        }
        if (cutPos < body.size()) {
            truncated = true;
        }
        contentPreview = body.substr(0, cutPos);
        // Count actual preview lines
        for (char c : contentPreview) { if (c == '\n') previewLines++; }
        if (!contentPreview.empty() && contentPreview.back() != '\n') previewLines++;
    }

    auto summary = ToolResultSummary::success(std::to_string(lines) + " lines", /*bold=*/true,
                                              /*secondary=*/secondary);
    summary.contentPreview = std::move(contentPreview);
    summary.contentPreviewTruncated = truncated;
    summary.previewLinesShown = previewLines;
    summary.totalLines = lines;
    return summary;
}

} // namespace claude

#include <ontology/DocumentPreprocessor.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <cctype>
#include <cstdlib>

// Minimal ZIP support for Office formats
#include <zlib.h>

namespace ontology {

// ============================================================================
// Base64 encoding
// ============================================================================

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String DocumentPreprocessor::base64Encode(const unsigned char* data, size_t len) {
    String result;
    result.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int n = static_cast<unsigned int>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<unsigned int>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<unsigned int>(data[i + 2]);

        result += kBase64Table[(n >> 18) & 0x3F];
        result += kBase64Table[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? kBase64Table[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? kBase64Table[n & 0x3F] : '=';
    }
    return result;
}

// ============================================================================
// File I/O helpers
// ============================================================================

String DocumentPreprocessor::readFileContent(const String& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return {};
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::vector<String> DocumentPreprocessor::splitPath(const String& path) {
    std::vector<String> parts;
    size_t start = 0;
    for (size_t i = 0; i < path.size(); i++) {
        if (path[i] == '/' || path[i] == '\\') {
            if (i > start) parts.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start < path.size()) parts.push_back(path.substr(start));
    return parts;
}

String DocumentPreprocessor::detectMimeType(const String& filePath) {
    auto parts = splitPath(filePath);
    if (parts.empty()) return "application/octet-stream";
    String ext = parts.back();
    auto dotPos = ext.rfind('.');
    if (dotPos == String::npos) return "application/octet-stream";
    ext = ext.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static const std::unordered_map<String, String> mimeMap = {
        {"txt", "text/plain"}, {"md", "text/markdown"}, {"csv", "text/csv"},
        {"json", "application/json"}, {"xml", "application/xml"},
        {"html", "text/html"}, {"htm", "text/html"},
        {"pdf", "application/pdf"},
        {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
        {"gif", "image/gif"}, {"bmp", "image/bmp"}, {"webp", "image/webp"},
        {"svg", "image/svg+xml"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    };

    auto it = mimeMap.find(ext);
    return it != mimeMap.end() ? it->second : "application/octet-stream";
}

String DocumentPreprocessor::detectLanguage(const String& text) {
    if (text.empty()) return "unknown";
    int zhChars = 0, enChars = 0;
    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 0xE0 && c < 0xF0) {
            zhChars++;
            i += 3;
        } else if (std::isalpha(c)) {
            enChars++;
            i++;
        } else {
            i++;
        }
    }
    if (zhChars == 0 && enChars == 0) return "unknown";
    if (zhChars > enChars * 2) return "zh";
    if (enChars > zhChars * 2) return "en";
    return "mixed";
}

// ============================================================================
// Constructor
// ============================================================================

DocumentPreprocessor::DocumentPreprocessor(
    std::shared_ptr<LlmBackend> llmBackend,
    const Config& config
)
    : llmBackend_(llmBackend)
    , config_(config)
{
}

// ============================================================================
// Main extraction methods
// ============================================================================

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extract(const String& filePath) {
    String mime = detectMimeType(filePath);
    String data = readFileContent(filePath);
    return extractFromBuffer(data, mime, filePath);
}

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractFromBuffer(
    const String& data,
    const String& mimeType,
    const String& fileName
) {
    String mime = mimeType.empty() ? detectMimeType(fileName) : mimeType;

    if (mime.find("text/") == 0 || mime == "application/json" || mime == "application/xml") {
        if (mime == "application/json") return extractJson(data);
        if (mime == "text/csv") return extractCsv(data);
        return extractText(data);
    }
    if (mime == "application/pdf") {
        return fileName.empty() ? extractPdfFromBuffer(data) : extractPdf(fileName);
    }
    if (mime.find("image/") == 0) {
        return extractImageFromBuffer(data, mime);
    }
    if (mime.find("officedocument") != String::npos) {
        if (mime.find("wordprocessing") != String::npos) return extractDocx(data);
        if (mime.find("presentation") != String::npos) return extractPptx(data);
        if (mime.find("spreadsheet") != String::npos) return extractXlsx(data);
    }

    // Fallback: try as plain text
    ExtractResult result = extractText(data);
    result.detectedType = "unknown";
    return result;
}

String DocumentPreprocessor::imageToText(
    const String& imageBase64,
    const String& mediaType
) {
    if (!llmBackend_ || !config_.enableImageToText) return {};

    String prompt = "请详细描述这张图片的内容。如果图片中包含文字，请完整转录所有文字内容。";
    return llmBackend_->completeWithImage(prompt, imageBase64, mediaType);
}

// ============================================================================
// Text format extractors
// ============================================================================

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractText(const String& content) {
    ExtractResult result;
    result.text = content.substr(0, config_.maxExtractLength);
    result.detectedType = "text";
    result.detectedLanguage = detectLanguage(content);
    result.charCount = static_cast<int>(result.text.size());
    return result;
}

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractCsv(const String& content) {
    ExtractResult result;
    std::ostringstream oss;

    // Simple CSV: join cells with space, rows with newline
    std::istringstream iss(content);
    String line;
    while (std::getline(iss, line)) {
        bool inQuote = false;
        String cell;
        for (size_t i = 0; i < line.size(); i++) {
            char c = line[i];
            if (c == '"') { inQuote = !inQuote; continue; }
            if (c == ',' && !inQuote) {
                if (!cell.empty()) oss << cell << " ";
                cell.clear();
                continue;
            }
            cell += c;
        }
        if (!cell.empty()) oss << cell;
        oss << "\n";
    }

    result.text = oss.str().substr(0, config_.maxExtractLength);
    result.detectedType = "text";
    result.detectedLanguage = detectLanguage(result.text);
    result.charCount = static_cast<int>(result.text.size());
    return result;
}

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractJson(const String& content) {
    ExtractResult result;
    try {
        Json j = Json::parse(content);
        // Flatten JSON to text: key=value pairs
        std::function<void(const Json&, const String&)> flatten;
        flatten = [&](const Json& obj, const String& prefix) {
            if (obj.is_object()) {
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    String key = prefix.empty() ? it.key() : prefix + "." + it.key();
                    flatten(it.value(), key);
                }
            } else if (obj.is_array()) {
                for (size_t i = 0; i < obj.size(); i++) {
                    flatten(obj[i], prefix + "[" + std::to_string(i) + "]");
                }
            } else {
                String val;
                if (obj.is_string()) val = obj.get<String>();
                else val = obj.dump();
                result.text += prefix + ": " + val + "\n";
            }
        };
        flatten(j, "");
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("RAG JSON error: {}", e.what());
        result.text = content;
    }
    result.text = result.text.substr(0, config_.maxExtractLength);
    result.detectedType = "text";
    result.detectedLanguage = detectLanguage(result.text);
    result.charCount = static_cast<int>(result.text.size());
    return result;
}

// ============================================================================
// PDF extraction
// ============================================================================

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractPdf(const String& filePath) {
    ExtractResult result;
    result.detectedType = "pdf";

    // Try pdftotext command
    String cmd = config_.pdftotextPath + " -enc UTF-8 -layout \"" + filePath + "\" - 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result.text += buffer;
            if (static_cast<int>(result.text.size()) >= config_.maxExtractLength) break;
        }
        pclose(pipe);
    }

    if (result.text.empty()) {
        // Fallback: extract text from PDF binary (crude)
        String data = readFileContent(filePath);
        result = extractPdfFromBuffer(data);
        result.detectedType = "pdf";
    }

    result.detectedLanguage = detectLanguage(result.text);
    result.charCount = static_cast<int>(result.text.size());
    return result;
}

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractPdfFromBuffer(const String& data) {
    ExtractResult result;
    result.detectedType = "pdf";

    // Crude text extraction from PDF: find text between stream operators
    // This is a fallback; pdftotext is preferred
    std::ostringstream oss;
    size_t i = 0;
    bool inText = false;
    while (i < data.size() && static_cast<int>(oss.str().size()) < config_.maxExtractLength) {
        // Look for BT...ET (text blocks)
        if (i + 1 < data.size() && data[i] == 'B' && data[i + 1] == 'T') {
            inText = true;
            i += 2;
            continue;
        }
        if (i + 1 < data.size() && data[i] == 'E' && data[i + 1] == 'T') {
            inText = false;
            i += 2;
            continue;
        }
        if (inText && static_cast<unsigned char>(data[i]) >= 0x20 &&
            static_cast<unsigned char>(data[i]) < 0x7F) {
            oss << data[i];
        }
        i++;
    }

    result.text = oss.str();
    result.detectedLanguage = detectLanguage(result.text);
    result.charCount = static_cast<int>(result.text.size());
    return result;
}

// ============================================================================
// Image extraction
// ============================================================================

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractImage(const String& filePath) {
    String data = readFileContent(filePath);
    String mime = detectMimeType(filePath);
    return extractImageFromBuffer(data, mime);
}

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractImageFromBuffer(
    const String& data,
    const String& mediaType
) {
    ExtractResult result;
    result.detectedType = "image";

    // Encode image to base64
    String b64 = base64Encode(
        reinterpret_cast<const unsigned char*>(data.data()),
        data.size()
    );

    // Try image2text via LLM
    if (llmBackend_ && config_.enableImageToText) {
        String description = imageToText(b64, mediaType);
        if (!description.empty()) {
            result.text = description;
        }
    }

    result.imageBase64.push_back(b64);
    result.imageMediaTypes.push_back(mediaType);
    result.imageCount = 1;
    result.charCount = static_cast<int>(result.text.size());
    result.detectedLanguage = detectLanguage(result.text);
    return result;
}

// ============================================================================
// Office format extraction (ZIP + XML)
// ============================================================================

// Minimal ZIP local file header parsing
struct ZipEntry {
    String fileName;
    size_t compressedSize;
    size_t uncompressedSize;
    size_t offset;
    int compressionMethod;  // 0=stored, 8=deflate
};

static std::vector<ZipEntry> parseZipEntries(const String& data) {
    std::vector<ZipEntry> entries;
    size_t pos = 0;
    while (pos + 30 < data.size()) {
        // Local file header signature: PK\x03\x04
        if (static_cast<unsigned char>(data[pos]) != 0x50 ||
            static_cast<unsigned char>(data[pos + 1]) != 0x4B ||
            static_cast<unsigned char>(data[pos + 2]) != 0x03 ||
            static_cast<unsigned char>(data[pos + 3]) != 0x04) {
            break;
        }

        ZipEntry e;
        // Skip version(2), flags(2), compression(2), time(2), date(2), crc(4)
        e.compressionMethod = static_cast<unsigned char>(data[pos + 8]) |
                              (static_cast<unsigned char>(data[pos + 9]) << 8);
        e.compressedSize = static_cast<unsigned char>(data[pos + 18]) |
                           (static_cast<unsigned char>(data[pos + 19]) << 8) |
                           (static_cast<unsigned char>(data[pos + 20]) << 16) |
                           (static_cast<unsigned char>(data[pos + 21]) << 24);
        e.uncompressedSize = static_cast<unsigned char>(data[pos + 22]) |
                             (static_cast<unsigned char>(data[pos + 23]) << 8) |
                             (static_cast<unsigned char>(data[pos + 24]) << 16) |
                             (static_cast<unsigned char>(data[pos + 25]) << 24);

        int nameLen = static_cast<unsigned char>(data[pos + 26]) |
                      (static_cast<unsigned char>(data[pos + 27]) << 8);
        int extraLen = static_cast<unsigned char>(data[pos + 28]) |
                       (static_cast<unsigned char>(data[pos + 29]) << 8);

        e.fileName = data.substr(pos + 30, nameLen);
        e.offset = pos + 30 + nameLen + extraLen;
        entries.push_back(e);

        pos = e.offset + e.compressedSize;
    }
    return entries;
}

static String decompressDeflate(const String& compressed) {
    String result;
    result.resize(compressed.size() * 10);  // estimate
    z_stream stream = {};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = reinterpret_cast<Bytef*>(&result[0]);
    stream.avail_out = static_cast<uInt>(result.size());

    if (inflateInit2(&stream, -15) != Z_OK) return {};
    int ret = inflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END && ret != Z_OK) {
        inflateEnd(&stream);
        return {};
    }
    result.resize(stream.total_out);
    inflateEnd(&stream);
    return result;
}

String DocumentPreprocessor::extractXmlText(const String& xmlContent, const String& tag) {
    String result;
    String openTag = "<" + tag + ">";
    String closeTag = "</" + tag + ">";
    size_t pos = 0;
    while (pos < xmlContent.size()) {
        auto start = xmlContent.find(openTag, pos);
        if (start == String::npos) break;
        start += openTag.size();
        auto end = xmlContent.find(closeTag, start);
        if (end == String::npos) break;

        String content = xmlContent.substr(start, end - start);
        // Strip nested XML tags
        std::ostringstream clean;
        bool inTag = false;
        for (char c : content) {
            if (c == '<') { inTag = true; continue; }
            if (c == '>') { inTag = false; clean << ' '; continue; }
            if (!inTag) clean << c;
        }

        String text = clean.str();
        // Trim whitespace
        size_t first = text.find_first_not_of(" \t\n\r");
        if (first != String::npos) {
            size_t last = text.find_last_not_of(" \t\n\r");
            result += text.substr(first, last - first + 1) + "\n";
        }

        pos = end + closeTag.size();
    }
    return result;
}

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractDocx(const String& data) {
    ExtractResult result;
    result.detectedType = "office";

    auto entries = parseZipEntries(data);
    for (const auto& e : entries) {
        if (e.fileName.find("word/document.xml") != String::npos ||
            e.fileName.find("word/header") != String::npos ||
            e.fileName.find("word/footer") != String::npos) {
            String compressed = data.substr(e.offset, e.compressedSize);
            String xmlContent;
            if (e.compressionMethod == 8) {
                xmlContent = decompressDeflate(compressed);
            } else {
                xmlContent = compressed;
            }
            result.text += extractXmlText(xmlContent, "w:t");
            result.text += extractXmlText(xmlContent, "w:p");
        }
        // Extract embedded images
        if (e.fileName.find("word/media/") != String::npos &&
            (e.fileName.find(".png") != String::npos ||
             e.fileName.find(".jpg") != String::npos ||
             e.fileName.find(".jpeg") != String::npos)) {
            String imgData = data.substr(e.offset, e.compressedSize);
            String b64 = base64Encode(
                reinterpret_cast<const unsigned char*>(imgData.data()),
                imgData.size()
            );
            result.imageBase64.push_back(b64);
            String mediaType = e.fileName.find(".png") != String::npos ? "image/png" : "image/jpeg";
            result.imageMediaTypes.push_back(mediaType);
            result.imageCount++;
        }
    }

    result.text = result.text.substr(0, config_.maxExtractLength);
    result.detectedLanguage = detectLanguage(result.text);
    result.charCount = static_cast<int>(result.text.size());
    return result;
}

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractPptx(const String& data) {
    ExtractResult result;
    result.detectedType = "office";

    auto entries = parseZipEntries(data);
    for (const auto& e : entries) {
        if (e.fileName.find("ppt/slides/slide") != String::npos &&
            e.fileName.find(".xml") != String::npos) {
            String compressed = data.substr(e.offset, e.compressedSize);
            String xmlContent;
            if (e.compressionMethod == 8) {
                xmlContent = decompressDeflate(compressed);
            } else {
                xmlContent = compressed;
            }
            result.text += extractXmlText(xmlContent, "a:t") + "\n---\n";
            result.pageCount++;
        }
    }

    result.text = result.text.substr(0, config_.maxExtractLength);
    result.detectedLanguage = detectLanguage(result.text);
    result.charCount = static_cast<int>(result.text.size());
    return result;
}

DocumentPreprocessor::ExtractResult DocumentPreprocessor::extractXlsx(const String& data) {
    ExtractResult result;
    result.detectedType = "office";

    auto entries = parseZipEntries(data);

    // First find shared strings
    String sharedStrings;
    for (const auto& e : entries) {
        if (e.fileName == "xl/sharedStrings.xml") {
            String compressed = data.substr(e.offset, e.compressedSize);
            if (e.compressionMethod == 8) {
                sharedStrings = decompressDeflate(compressed);
            } else {
                sharedStrings = compressed;
            }
            break;
        }
    }

    // Parse shared strings into index → text map
    std::vector<String> stringTable;
    if (!sharedStrings.empty()) {
        String siOpen = "<si>", siClose = "</si>";
        size_t pos = 0;
        while (pos < sharedStrings.size()) {
            auto start = sharedStrings.find(siOpen, pos);
            if (start == String::npos) break;
            start += siOpen.size();
            auto end = sharedStrings.find(siClose, start);
            if (end == String::npos) break;
            String content = sharedStrings.substr(start, end - start);
            stringTable.push_back(extractXmlText(content, "t"));
            pos = end + siClose.size();
        }
    }

    // Then parse worksheets
    for (const auto& e : entries) {
        if (e.fileName.find("xl/worksheets/sheet") != String::npos &&
            e.fileName.find(".xml") != String::npos) {
            String compressed = data.substr(e.offset, e.compressedSize);
            String xmlContent;
            if (e.compressionMethod == 8) {
                xmlContent = decompressDeflate(compressed);
            } else {
                xmlContent = compressed;
            }
            result.pageCount++;

            // Extract cell values
            String cOpen = "<c ", cClose = "</c>";
            size_t pos = 0;
            while (pos < xmlContent.size()) {
                auto start = xmlContent.find(cOpen, pos);
                if (start == String::npos) break;
                auto end = xmlContent.find(cClose, start);
                if (end == String::npos) { pos = start + cOpen.size(); continue; }

                String cell = xmlContent.substr(start, end - start + cClose.size());

                // Check if it's a shared string (t="s")
                bool isShared = cell.find("t=\"s\"") != String::npos;

                if (isShared) {
                    String vText = extractXmlText(cell, "v");
                    int idx = 0;
                    try { idx = std::stoi(vText); } catch (const std::exception&) {}
                    if (idx >= 0 && idx < static_cast<int>(stringTable.size())) {
                        result.text += stringTable[idx] + "\t";
                    }
                } else {
                    String vText = extractXmlText(cell, "v");
                    if (!vText.empty()) result.text += vText + "\t";
                }
                pos = end + cClose.size();
            }
            result.text += "\n";
        }
    }

    result.text = result.text.substr(0, config_.maxExtractLength);
    result.detectedLanguage = detectLanguage(result.text);
    result.charCount = static_cast<int>(result.text.size());
    return result;
}

} // namespace ontology

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <claude/tool/impl/FileReadTool.hpp>
#include <claude/tool/ToolContext.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <vector>

using namespace claude;
using Catch::Matchers::ContainsSubstring;

namespace {

struct TmpFile {
    std::filesystem::path path;
    TmpFile(const String& content) {
        auto tmpDir = std::filesystem::temp_directory_path();
        path = tmpDir / ("test_fileread_" + std::to_string(std::hash<String>{}(content)) + ".txt");
        std::ofstream ofs(path);
        ofs << content;
    }
    ~TmpFile() { std::filesystem::remove(path); }
    String str() const { return path.string(); }
};

struct TmpDir {
    std::filesystem::path path;
    TmpDir() {
        auto tmpDir = std::filesystem::temp_directory_path();
        path = tmpDir / ("test_fileread_dir_" + std::to_string(rand()));
        std::filesystem::create_directories(path);
    }
    ~TmpDir() { std::filesystem::remove_all(path); }
};

}

TEST_CASE("FileReadTool supportsStreaming returns true", "[fileread][streaming]") {
    FileReadTool tool;
    REQUIRE(tool.supportsStreaming());
}

TEST_CASE("FileReadTool streaming produces same output as execute", "[fileread][streaming]") {
    TmpFile tmp("line1\nline2\nline3\nline4\nline5\n");
    FileReadTool tool;
    ToolContext ctx = ToolContext::create(std::filesystem::current_path());
    Json input = {{"file_path", tmp.str()}};

    String normalResult = tool.execute(input, ctx);

    std::vector<String> chunks;
    String streamResult = tool.executeStreaming(input, ctx,
        [&](const String& chunk) -> bool {
            chunks.push_back(chunk);
            return true;
        });

    REQUIRE(normalResult == streamResult);
    REQUIRE_FALSE(chunks.empty());
}

TEST_CASE("FileReadTool streaming delivers multiple chunks for large files", "[fileread][streaming]") {
    // Create a file with 250 lines to ensure multiple chunks (100 lines per chunk)
    std::ostringstream content;
    for (int i = 1; i <= 250; i++) {
        content << "This is line number " << i << " with some content\n";
    }
    TmpFile tmp(content.str());
    FileReadTool tool;
    ToolContext ctx = ToolContext::create(std::filesystem::current_path());
    Json input = {{"file_path", tmp.str()}};

    std::vector<String> chunks;
    String result = tool.executeStreaming(input, ctx,
        [&](const String& chunk) -> bool {
            chunks.push_back(chunk);
            return true;
        });

    // Header chunk + at least 2 data chunks (250 lines / 100 per chunk = 3 data chunks)
    REQUIRE(chunks.size() >= 3);
    REQUIRE_THAT(result, ContainsSubstring("line number 1"));
    REQUIRE_THAT(result, ContainsSubstring("line number 250"));
}

TEST_CASE("FileReadTool streaming respects offset and limit", "[fileread][streaming]") {
    std::ostringstream content;
    for (int i = 1; i <= 200; i++) {
        content << "row " << i << "\n";
    }
    TmpFile tmp(content.str());
    FileReadTool tool;
    ToolContext ctx = ToolContext::create(std::filesystem::current_path());
    Json input = {{"file_path", tmp.str()}, {"offset", 50}, {"limit", 20}};

    String result = tool.executeStreaming(input, ctx,
        [](const String&) -> bool { return true; });

    // offset=50 skips first 50 lines, so first visible is row 51
    REQUIRE_THAT(result, ContainsSubstring("row 51"));
    REQUIRE_THAT(result, ContainsSubstring("row 70"));
    REQUIRE_THAT(result, !ContainsSubstring("row 50"));
    REQUIRE_THAT(result, !ContainsSubstring("row 71"));
}

TEST_CASE("FileReadTool streaming supports cancellation", "[fileread][streaming]") {
    std::ostringstream content;
    for (int i = 1; i <= 300; i++) {
        content << "cancel_line " << i << "\n";
    }
    TmpFile tmp(content.str());
    FileReadTool tool;
    ToolContext ctx = ToolContext::create(std::filesystem::current_path());
    Json input = {{"file_path", tmp.str()}};

    int chunkCount = 0;
    String result = tool.executeStreaming(input, ctx,
        [&](const String& chunk) -> bool {
            chunkCount++;
            return chunkCount < 2; // Cancel after 2 chunks
        });

    // Result should be partial (fewer lines than 300)
    REQUIRE(chunkCount <= 2);
}

TEST_CASE("FileReadTool streaming for non-existent file returns error", "[fileread][streaming]") {
    FileReadTool tool;
    ToolContext ctx = ToolContext::create(std::filesystem::current_path());
    Json input = {{"file_path", "/nonexistent/path/file.txt"}};

    String result = tool.executeStreaming(input, ctx,
        [](const String&) -> bool { return true; });

    REQUIRE_THAT(result, ContainsSubstring("Error"));
}

TEST_CASE("FileReadTool streaming handles long line truncation", "[fileread][streaming]") {
    String longLine(3000, 'X');
    TmpFile tmp(longLine + "\nshort line\n");
    FileReadTool tool;
    ToolContext ctx = ToolContext::create(std::filesystem::current_path());
    Json input = {{"file_path", tmp.str()}};

    String result = tool.executeStreaming(input, ctx,
        [](const String&) -> bool { return true; });

    REQUIRE_THAT(result, ContainsSubstring("line truncated"));
    REQUIRE_THAT(result, ContainsSubstring("short line"));
}

TEST_CASE("FileReadTool executeStreaming fallback for images", "[fileread][streaming]") {
    // A 1x1 PNG file (smallest valid PNG)
    std::filesystem::path imgPath = std::filesystem::temp_directory_path() / "test_stream_img.png";
    {
        // Write minimal PNG header (won't be a valid image but triggers the image path)
        std::ofstream ofs(imgPath, std::ios::binary);
        unsigned char pngHeader[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
        ofs.write(reinterpret_cast<char*>(pngHeader), 8);
        // Write enough to be recognized as image
        ofs << "dummy data";
    }

    FileReadTool tool;
    ToolContext ctx = ToolContext::create(std::filesystem::current_path());
    Json input = {{"file_path", imgPath.string()}};

    // Should not crash - falls back to execute() for image files
    String result = tool.executeStreaming(input, ctx,
        [](const String&) -> bool { return true; });

    std::filesystem::remove(imgPath);
    // Result will contain IMAGE marker or error, either way it's handled
    REQUIRE_FALSE(result.empty());
}

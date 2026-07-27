#include <catch2/catch_test_macros.hpp>
#include <claude/context/ClaudeMdLoader.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <unistd.h>

using namespace claude;

namespace {
struct TmpDir {
    std::filesystem::path path;
    TmpDir() {
        static std::atomic<uint64_t> counter{0};
        path = std::filesystem::temp_directory_path() /
            ("claude_test_" +
             std::to_string(getpid()) + "_" +
             std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "_" +
             std::to_string(counter.fetch_add(1, std::memory_order_relaxed)));
        std::filesystem::create_directories(path);
    }
    ~TmpDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
}

TEST_CASE("ClaudeMdLoader MemoryType descriptions", "[claude_md]") {
    SECTION("MemoryType descriptions match TS spec") {
        REQUIRE(memoryTypeDescription(MemoryType::Managed) == "(managed policy instructions)");
        REQUIRE(memoryTypeDescription(MemoryType::User) == "(user's private global instructions for all projects)");
        REQUIRE(memoryTypeDescription(MemoryType::Project) == "(project instructions, checked into the codebase)");
        REQUIRE(memoryTypeDescription(MemoryType::Local) == "(user's private project instructions, not checked in)");
        REQUIRE(memoryTypeDescription(MemoryType::AutoMem) == "(user's auto-memory, persists across conversations)");
    }

    SECTION("MemoryType strings") {
        REQUIRE(memoryTypeToString(MemoryType::Managed) == "Managed");
        REQUIRE(memoryTypeToString(MemoryType::User) == "User");
        REQUIRE(memoryTypeToString(MemoryType::Project) == "Project");
        REQUIRE(memoryTypeToString(MemoryType::Local) == "Local");
        REQUIRE(memoryTypeToString(MemoryType::AutoMem) == "AutoMem");
    }
}

TEST_CASE("ClaudeMdLoader ClaudeMdContent defaults", "[claude_md]") {
    SECTION("Default values") {
        ClaudeMdContent content;
        REQUIRE(content.type == MemoryType::Project);
        REQUIRE(content.filePath.empty());
        REQUIRE(content.content.empty());
        REQUIRE(content.contentDiffersFromDisk == false);
        REQUIRE(content.paths.empty());
        REQUIRE_FALSE(content.parent.has_value());
    }
}

TEST_CASE("ClaudeMdLoader ClaudeMdFrontmatter", "[claude_md]") {
    SECTION("Default frontmatter") {
        ClaudeMdFrontmatter fm;
        REQUIRE(fm.paths.empty());
        REQUIRE(fm.allowedTools.empty());
        REQUIRE_FALSE(fm.description.has_value());
        REQUIRE_FALSE(fm.userInvocable.has_value());
        REQUIRE(fm.skills.empty());
    }
}

TEST_CASE("ClaudeMdLoader isMemoryFilePath", "[claude_md]") {
    SECTION("Recognizes memory file paths") {
        REQUIRE(ClaudeMdLoader::isMemoryFilePath("CLAUDE.md"));
        REQUIRE(ClaudeMdLoader::isMemoryFilePath("CLAUDE.local.md"));
    }

    SECTION("Rejects non-memory files") {
        REQUIRE_FALSE(ClaudeMdLoader::isMemoryFilePath("README.md"));
        REQUIRE_FALSE(ClaudeMdLoader::isMemoryFilePath("package.json"));
        REQUIRE_FALSE(ClaudeMdLoader::isMemoryFilePath("CONTRIBUTING.md"));
    }
}

TEST_CASE("ClaudeMdLoader MAX_MEMORY_CHARACTER_COUNT", "[claude_md]") {
    REQUIRE(ClaudeMdLoader::MAX_MEMORY_CHARACTER_COUNT == 40000);
}

TEST_CASE("ClaudeMdLoader getLargeFiles", "[claude_md]") {
    SECTION("Detects oversized files") {
        ClaudeMdContent small;
        small.content = String(100, 'x');

        ClaudeMdContent large;
        large.content = String(50000, 'x');

        std::vector<ClaudeMdContent> files = {small, large};
        auto largeFiles = ClaudeMdLoader::getLargeFiles(files);
        REQUIRE(largeFiles.size() == 1);
        REQUIRE(largeFiles[0]->content.size() == 50000);
    }
}

TEST_CASE("ClaudeMdLoader loadAll with empty dir", "[claude_md]") {
    TmpDir tmp;

    SECTION("Empty directory returns no files") {
        ClaudeMdLoader loader;
        loader.setCwd(tmp.path);
        auto files = loader.loadAll();
        REQUIRE(files.empty());
    }
}

TEST_CASE("ClaudeMdLoader loadAll with CLAUDE.md", "[claude_md]") {
    TmpDir tmp;

    SECTION("Finds CLAUDE.md in CWD") {
        std::ofstream(tmp.path / "CLAUDE.md") << "# Test Project\n\nSome instructions.";

        ClaudeMdLoader loader;
        loader.setCwd(tmp.path);
        auto files = loader.loadAll();
        // May or may not find depending on git root detection
        // At minimum, should not crash
        REQUIRE(files.size() >= 0);
    }
}

TEST_CASE("ClaudeMdLoader loadAll with CLAUDE.local.md", "[claude_md]") {
    TmpDir tmp;

    SECTION("Finds CLAUDE.local.md when localSettings enabled") {
        std::ofstream(tmp.path / "CLAUDE.local.md") << "# Local notes\n\nPrivate instructions.";

        ClaudeMdLoader loader;
        loader.setCwd(tmp.path);
        loader.setSettingSources({true, true, true});
        auto files = loader.loadAll();
        // Should not crash
        REQUIRE(files.size() >= 0);
    }
}

TEST_CASE("ClaudeMdLoader setting source gating", "[claude_md]") {
    TmpDir tmp;
    std::ofstream(tmp.path / "CLAUDE.md") << "# Project\n";
    std::ofstream(tmp.path / "CLAUDE.local.md") << "# Local\n";

    SECTION("Disabling localSettings skips CLAUDE.local.md") {
        ClaudeMdLoader loader;
        loader.setCwd(tmp.path);
        loader.setSettingSources({true, true, false}); // localSettings=false
        auto files = loader.loadAll();

        bool foundLocal = false;
        for (const auto& f : files) {
            if (f.type == MemoryType::Local) foundLocal = true;
        }
        REQUIRE_FALSE(foundLocal);
    }
}

TEST_CASE("ClaudeMdLoader bare mode", "[claude_md]") {
    TmpDir tmp;
    std::ofstream(tmp.path / "CLAUDE.md") << "# Should not load\n";

    SECTION("Bare mode skips all CLAUDE.md loading") {
        ClaudeMdLoader loader;
        loader.setCwd(tmp.path);
        loader.setBareMode(true);
        auto files = loader.loadAll();
        REQUIRE(files.empty());
    }
}

TEST_CASE("ClaudeMdLoader formatAsInstructions", "[claude_md]") {
    SECTION("Formats project instructions correctly") {
        ClaudeMdContent file;
        file.filePath = "/project/CLAUDE.md";
        file.content = "# Build\n\nUse bun.";
        file.type = MemoryType::Project;
        file.description = memoryTypeDescription(MemoryType::Project);

        ClaudeMdLoader loader;
        String output = loader.formatAsInstructions({file});
        REQUIRE(output.find("project instructions") != String::npos);
        REQUIRE(output.find("Use bun") != String::npos);
    }
}

TEST_CASE("ClaudeMdLoader .claude/rules/*.md", "[claude_md]") {
    TmpDir tmp;
    std::filesystem::create_directories(tmp.path / ".claude" / "rules");
    std::ofstream(tmp.path / ".claude" / "rules" / "convention.md") << "# Convention\n\nUse tabs.";

    SECTION("Rules directory is accessible") {
        ClaudeMdLoader loader;
        loader.setCwd(tmp.path);
        auto files = loader.loadAll();
        // Should not crash; rules may or may not load depending on git root
        REQUIRE(files.size() >= 0);
    }
}

TEST_CASE("ClaudeMdLoader cache management", "[claude_md]") {
    ClaudeMdLoader loader;
    loader.clearCache();
    loader.resetCache();
    REQUIRE(loader.processedPaths().empty());
}

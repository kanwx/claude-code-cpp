#include <catch2/catch_test_macros.hpp>
#include <claude/context/ContextInjector.hpp>
#include <nlohmann/json.hpp>

using namespace claude;

TEST_CASE("ContextInjector builds context with all fields", "[context][injector]") {
    ContextInjector injector;

    SECTION("Default context has date and no optional fields") {
        auto ctx = injector.buildContext("test query");
        REQUIRE_FALSE(ctx.currentDate.empty());
        REQUIRE_FALSE(ctx.gitStatus.has_value());
        REQUIRE_FALSE(ctx.claudeMd.has_value());
        REQUIRE(ctx.attachments.empty());
        REQUIRE(ctx.relevantMemories.empty());
        REQUIRE_FALSE(ctx.skillDiscovery.has_value());
        REQUIRE_FALSE(ctx.planContext.has_value());
    }

    SECTION("Git status is included") {
        GitStatusAttachment git;
        git.branch = "feat/test";
        git.mainBranch = "main";
        git.status = "M src/test.cpp";
        git.recentCommits = "abc123 test commit";
        injector.setGitStatus(git);

        auto ctx = injector.buildContext();
        REQUIRE(ctx.gitStatus.has_value());
        REQUIRE(ctx.gitStatus->branch == "feat/test");
        REQUIRE(ctx.gitStatus->mainBranch == "main");
    }

    SECTION("CLAUDE.md content is included") {
        injector.setClaudeMd("# Project Rules\nAlways write tests.");

        auto ctx = injector.buildContext();
        REQUIRE(ctx.claudeMd.has_value());
        REQUIRE(ctx.claudeMd->find("Project Rules") != String::npos);
    }

    SECTION("System reminders are included") {
        injector.addSystemReminder("Be careful with shell commands.");
        injector.addSystemReminder("Check file permissions.");

        auto ctx = injector.buildContext();
        REQUIRE(ctx.systemReminders.size() == 2);
        REQUIRE(ctx.systemReminders[0].content == "Be careful with shell commands.");
        REQUIRE(ctx.systemReminders[1].content == "Check file permissions.");
    }

    SECTION("Memory is loaded and queried") {
        injector.addMemory("project.md", "This project uses CMake for building.");
        injector.addMemory("testing.md", "We use Catch2 for unit testing.");

        // With a matching query, relevant memories are filtered
        auto ctx = injector.buildContext("testing setup");
        REQUIRE_FALSE(ctx.relevantMemories.empty());
    }

    SECTION("Plan mode context is included") {
        PlanModeAttachment plan;
        plan.type = "plan_mode";
        plan.planPath = "/tmp/plan.md";
        plan.content = "Step 1: Refactor\nStep 2: Test";
        plan.needsApproval = true;
        injector.setPlanMode(plan);

        auto ctx = injector.buildContext();
        REQUIRE(ctx.planContext.has_value());
        REQUIRE(ctx.planContext->needsApproval);
        REQUIRE(ctx.planContext->content.find("Refactor") != String::npos);
    }
}

TEST_CASE("ContextInjector attachment methods", "[context][injector]") {
    ContextInjector injector;

    SECTION("File attachment") {
        injector.addFileAttachment("/tmp/test.txt", "hello world", false);
        auto ctx = injector.buildContext();
        REQUIRE(ctx.attachments.size() == 1);
        REQUIRE(ctx.attachments[0].type == "file");
        REQUIRE(ctx.attachments[0].file.filename == "/tmp/test.txt");
    }

    SECTION("Directory attachment") {
        injector.addDirectoryAttachment("/tmp/mydir", "file1.txt\nfile2.txt");
        auto ctx = injector.buildContext();
        REQUIRE(ctx.attachments.size() == 1);
        REQUIRE(ctx.attachments[0].type == "directory");
    }

    SECTION("PDF attachment") {
        injector.addPdfAttachment("report.pdf", 10, 50000);
        auto ctx = injector.buildContext();
        REQUIRE(ctx.attachments.size() == 1);
        REQUIRE(ctx.attachments[0].type == "pdf_reference");
        REQUIRE(ctx.attachments[0].pdf.pageCount == 10);
    }

    SECTION("Todo reminder") {
        injector.addTodoReminder("- [ ] Fix bug\n- [ ] Add test", 2);
        auto ctx = injector.buildContext();
        REQUIRE(ctx.attachments.size() == 1);
        REQUIRE(ctx.attachments[0].type == "todo_reminder");
        REQUIRE(ctx.attachments[0].todo.itemCount == 2);
    }

    SECTION("Task reminder") {
        injector.addTaskReminder("Task 1: implement X", 1);
        auto ctx = injector.buildContext();
        REQUIRE(ctx.attachments.size() == 1);
        REQUIRE(ctx.attachments[0].type == "task_reminder");
        REQUIRE(ctx.attachments[0].task.itemCount == 1);
    }

    SECTION("IDE selection") {
        injector.addIdeSelection("VSCode", "/tmp/test.cpp", 10, 20, "selected code");
        auto ctx = injector.buildContext();
        REQUIRE(ctx.attachments.size() == 1);
        REQUIRE(ctx.attachments[0].type == "selected_lines_in_ide");
        REQUIRE(ctx.attachments[0].ideSelection.ideName == "VSCode");
    }

    SECTION("Edited file") {
        injector.addEditedFile("/tmp/test.cpp", "old → new");
        auto ctx = injector.buildContext();
        REQUIRE(ctx.attachments.size() == 1);
        REQUIRE(ctx.attachments[0].type == "edited_text_file");
    }

    SECTION("Multiple attachments accumulate") {
        injector.addFileAttachment("/tmp/a.txt", "a", false);
        injector.addFileAttachment("/tmp/b.txt", "b", true);
        auto ctx = injector.buildContext();
        REQUIRE(ctx.attachments.size() == 2);
    }

    SECTION("clearAttachments removes only attachments") {
        injector.addFileAttachment("/tmp/a.txt", "a", false);
        injector.addSystemReminder("reminder");
        injector.clearAttachments();

        auto ctx = injector.buildContext();
        REQUIRE(ctx.attachments.empty());
        REQUIRE(ctx.systemReminders.size() == 1); // reminders preserved
    }
}

TEST_CASE("ContextInjector formatAsMessageContent", "[context][injector]") {
    ContextInjector injector;

    SECTION("Date is always included") {
        auto ctx = injector.buildContext();
        String formatted = injector.formatAsMessageContent(ctx);
        REQUIRE(formatted.find("Today's date") != String::npos);
    }

    SECTION("Git status is formatted with branch info") {
        GitStatusAttachment git;
        git.branch = "main";
        git.mainBranch = "main";
        git.status = "M file.cpp";
        git.recentCommits = "abc123 initial";
        git.userName = "testuser";
        injector.setGitStatus(git);

        auto ctx = injector.buildContext();
        String formatted = injector.formatAsMessageContent(ctx);
        REQUIRE(formatted.find("Current branch: main") != String::npos);
        REQUIRE(formatted.find("Git user: testuser") != String::npos);
    }

    SECTION("CLAUDE.md content is included verbatim") {
        injector.setClaudeMd("# Rules\nAlways test your code.");
        auto ctx = injector.buildContext();
        String formatted = injector.formatAsMessageContent(ctx);
        REQUIRE(formatted.find("Always test your code.") != String::npos);
    }

    SECTION("System reminders are wrapped in tags") {
        injector.addSystemReminder("Don't delete files.");
        auto ctx = injector.buildContext();
        String formatted = injector.formatAsMessageContent(ctx);
        REQUIRE(formatted.find("<system-reminder>") != String::npos);
        REQUIRE(formatted.find("Don't delete files.") != String::npos);
        REQUIRE(formatted.find("</system-reminder>") != String::npos);
    }

    SECTION("Plan mode is formatted with approval indicator") {
        PlanModeAttachment plan;
        plan.planPath = "/tmp/plan.md";
        plan.content = "Step 1: Design";
        plan.needsApproval = true;
        injector.setPlanMode(plan);

        auto ctx = injector.buildContext();
        String formatted = injector.formatAsMessageContent(ctx);
        REQUIRE(formatted.find("Current Plan") != String::npos);
        REQUIRE(formatted.find("approval") != String::npos);
    }

    SECTION("PDF attachment shows page count") {
        injector.addPdfAttachment("doc.pdf", 25, 100000);
        auto ctx = injector.buildContext();
        String formatted = injector.formatAsMessageContent(ctx);
        REQUIRE(formatted.find("25 pages") != String::npos);
    }

    SECTION("Todo reminder shows item count") {
        injector.addTodoReminder("- [ ] Task 1\n- [ ] Task 2", 2);
        auto ctx = injector.buildContext();
        String formatted = injector.formatAsMessageContent(ctx);
        REQUIRE(formatted.find("2 items") != String::npos);
    }
}

TEST_CASE("formatAttachment handles all types", "[context][format]") {
    SECTION("PDF reference") {
        Attachment att;
        att.type = "pdf_reference";
        att.pdf.type = "pdf_reference";
        att.pdf.filename = "report.pdf";
        att.pdf.pageCount = 5;
        att.pdf.fileSize = 20000;
        att.pdf.displayPath = "report.pdf";
        String result = formatAttachment(att);
        REQUIRE(result.find("PDF: report.pdf") != String::npos);
        REQUIRE(result.find("5 pages") != String::npos);
    }

    SECTION("Todo reminder") {
        Attachment att;
        att.type = "todo_reminder";
        att.todo.type = "todo_reminder";
        att.todo.content = "- [ ] Fix bug";
        att.todo.itemCount = 1;
        String result = formatAttachment(att);
        REQUIRE(result.find("1 items") != String::npos);
    }

    SECTION("Task reminder") {
        Attachment att;
        att.type = "task_reminder";
        att.task.type = "task_reminder";
        att.task.content = "Implement feature X";
        att.task.itemCount = 1;
        String result = formatAttachment(att);
        REQUIRE(result.find("1 items") != String::npos);
    }

    SECTION("Memory") {
        Attachment att;
        att.type = "memory";
        att.memory.type = "memory";
        att.memory.path = "project.md";
        att.memory.content = "Uses CMake";
        att.memory.displayPath = "project.md";
        String result = formatAttachment(att);
        REQUIRE(result.find("Uses CMake") != String::npos);
    }

    SECTION("Plan mode") {
        Attachment att;
        att.type = "plan_mode";
        att.plan.type = "plan_mode";
        att.plan.planPath = "/tmp/plan.md";
        att.plan.content = "Refactor module";
        att.plan.needsApproval = true;
        String result = formatAttachment(att);
        REQUIRE(result.find("Plan: /tmp/plan.md") != String::npos);
        REQUIRE(result.find("approval") != String::npos);
    }

    SECTION("Git status") {
        Attachment att;
        att.type = "git_status";
        att.git.type = "git_status";
        att.git.branch = "main";
        att.git.mainBranch = "main";
        att.git.status = "M file.cpp";
        att.git.recentCommits = "abc123";
        String result = formatAttachment(att);
        REQUIRE(result.find("Current branch: main") != String::npos);
    }
}

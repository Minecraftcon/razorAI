#include "router.hpp"
#include "task_manager.hpp"
#include "file_inspector.hpp"
#include "skill_manager.hpp"
#include "web_search.hpp"
#include "c_router_bridge.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "=== Running C++ Router & Context Cache Tests ===" << std::endl;

    razor::RouterEngine router(100, std::chrono::seconds(300));

    // Test 1: First route call (Cache Miss)
    std::string prompt1 = "Create me a shopping website with Next.js and FastAPI";
    razor::RouteResult res1 = router.RoutePrompt(prompt1);
    std::cout << "[Test 1] Category: " << res1.category
              << " | Cache Hit: " << (res1.cache_hit ? "YES" : "NO") << std::endl;
    assert(res1.category == "Build");
    assert(!res1.cache_hit);

    // Test 2: Identical route call (Context Cache Hit)
    razor::RouteResult res2 = router.RoutePrompt(prompt1);
    std::cout << "[Test 2] Category: " << res2.category
              << " | Cache Hit: " << (res2.cache_hit ? "YES" : "NO") << std::endl;
    assert(res2.category == "Build");
    assert(res2.cache_hit);

    // Test 3: Question / Chat prompt
    std::string prompt3 = "What is the difference between embeddings 1 and 2?";
    razor::RouteResult res3 = router.RoutePrompt(prompt3);
    std::cout << "[Test 3] Category: " << res3.category
              << " | Cache Hit: " << (res3.cache_hit ? "YES" : "NO") << std::endl;
    assert(res3.category == "Question/Chat");

    // Test 4: Small Task prompt
    std::string prompt4 = "fix typo in line 12 and update comment";
    razor::RouteResult res4 = router.RoutePrompt(prompt4);
    std::cout << "[Test 4] Category: " << res4.category
              << " | Cache Hit: " << (res4.cache_hit ? "YES" : "NO") << std::endl;
    assert(res4.category == "Small_Task");

    // Test 5: C Bridge Memory Cleanup Validation
    RazorRouterHandle handle = RazorRouter_Create(50, 60);
    assert(handle != nullptr);

    char* category = nullptr;
    int cache_hit = 0;
    float confidence = 0.0f;

    int ret = RazorRouter_Route(handle, "Explain how context caching works", &category, &cache_hit, &confidence);
    assert(ret == 0);
    assert(category != nullptr);
    std::cout << "[C-Bridge Test] Category: " << category << " | Cache Hit: " << cache_hit << std::endl;

    RazorRouter_FreeString(category);
    RazorRouter_Destroy(handle);

    // Test 6: TaskManager Launch Synchronous Task
    std::cout << "\n=== Running TaskManager Tests ===" << std::endl;
    auto& tm = razor::TaskManager::Instance();
    auto sync_task = tm.LaunchTask("test_session", "echo 'Hello Razor Task'", "echo_test", 3);
    assert(!sync_task.is_background);
    assert(sync_task.output.find("Hello Razor Task") != std::string::npos);
    std::cout << "[TaskManager Test 1] Sync task completed with output: " << sync_task.output;

    // Test 7: TaskManager Launch Background Task (sleep 10s with work_time 1s)
    auto bkg_task = tm.LaunchTask("test_session", "sleep 10", "sleep_test", 1);
    assert(bkg_task.is_background);
    assert(!bkg_task.task_id.empty());
    std::cout << "[TaskManager Test 2] Background task launched: " << bkg_task.task_id << " (name: " << bkg_task.name << ")" << std::endl;

    // Test 8: View Task
    std::string view_res = tm.ViewTask(bkg_task.task_id);
    assert(view_res.find(bkg_task.task_id) != std::string::npos);
    assert(view_res.find("RUNNING") != std::string::npos);
    std::cout << "[TaskManager Test 3] View task returned RUNNING status." << std::endl;

    // Test 9: Format Task Table
    std::string table = tm.FormatTaskTable("test_session");
    assert(table.find("[Tasks:]") != std::string::npos);
    assert(table.find(bkg_task.task_id) != std::string::npos);
    std::cout << "[TaskManager Test 4] Formatted task table:\n" << table;

    // Test 10: Kill Background Task
    std::string kill_msg;
    bool killed = tm.KillTask(bkg_task.task_id, kill_msg);
    assert(killed);
    std::cout << "[TaskManager Test 5] Killed background task: " << kill_msg << std::endl;

    // Test 11: FileInspector native type detection and list_dir
    std::cout << "\n=== Running FileInspector Tests ===" << std::endl;
    std::string cm_type = razor::FileInspector::InspectFileType("CMakeLists.txt");
    assert(cm_type.find("CMake script") != std::string::npos);
    std::cout << "[FileInspector Test 1] CMakeLists.txt identified as: " << cm_type << std::endl;

    auto list_out = razor::FileInspector::ListDirectory(".");
    assert(!list_out.formatted_table.empty());
    assert(list_out.entries.size() > 0);
    std::cout << "[FileInspector Test 2] Directory listing output:\n" << list_out.formatted_table << std::endl;

    // Test 12: SkillManager skill discovery
    std::cout << "\n=== Running SkillManager Tests ===" << std::endl;
    auto skills = razor::SkillManager::Instance().DiscoverSkills();
    std::cout << "[SkillManager Test 1] Discovered " << skills.size() << " global/workspace skills." << std::endl;
    assert(skills.size() > 0);

    std::string skills_table = razor::SkillManager::Instance().FormatSkillsList();
    assert(!skills_table.empty());
    std::cout << "[SkillManager Test 2] Skills list preview:\n" << skills_table.substr(0, 300) << "...\n" << std::endl;

    // Test 13: WebSearch TinyFish tool
    std::cout << "\n=== Running WebSearch Tests ===" << std::endl;
    std::string search_res = razor::WebSearch::Search("speed test");
    assert(!search_res.empty());
    std::cout << "[WebSearch Test 1] Search result preview:\n" << search_res.substr(0, 300) << "...\n" << std::endl;

    std::string fetch_res = razor::WebSearch::Fetch("https://en.wikipedia.org/wiki/Test");
    assert(!fetch_res.empty());
    std::cout << "[WebSearch Test 2] Fetch result preview:\n" << fetch_res.substr(0, 300) << "...\n" << std::endl;

    std::cout << "=== All Router, TaskManager, FileInspector, SkillManager & WebSearch Tests Passed Successfully! ===" << std::endl;
    return 0;
}

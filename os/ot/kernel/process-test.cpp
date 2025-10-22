// process tests.cpp
#include "ot/kernel/kernel.hpp"
#include "vendor/doctest.h"

TEST_CASE("page_recycling") {
  // Note: We rely on the __free_ram and __free_ram_end symbols defined in
  // platform-test.cpp. For this test, we'll allocate directly and verify
  // recycling behavior.

  // Initialize memory system (will use existing __free_ram from platform-test)
  memory_init();

  // Allocate pages for process 1
  PageAddr page1_proc1 = page_allocate(1, 1);
  PageAddr page2_proc1 = page_allocate(1, 1);
  PageAddr page3_proc1 = page_allocate(1, 1);

  // test pinfo lookup
  PageInfo *pinfo = page_info_lookup(PageAddr(0x13456728));
  CHECK(pinfo == nullptr);

  pinfo = page_info_lookup(page1_proc1);
  CHECK(pinfo != nullptr);
  CHECK(pinfo->pid == 1);
  CHECK(pinfo->addr == page1_proc1);

  CHECK(page1_proc1.raw() != 0);
  CHECK(page2_proc1.raw() != 0);
  CHECK(page3_proc1.raw() != 0);
  CHECK(page1_proc1.raw() != page2_proc1.raw());
  CHECK(page2_proc1.raw() != page3_proc1.raw());

  // Free all pages for process 1
  page_free_process(1);

  // Allocate pages for process 2 - should get the same pages back
  PageAddr page1_proc2 = page_allocate(2, 1);
  PageAddr page2_proc2 = page_allocate(2, 1);
  PageAddr page3_proc2 = page_allocate(2, 1);

  // Verify pages are reused (may be in different order due to free list)
  bool page1_reused = (page1_proc2.raw() == page1_proc1.raw() ||
                       page1_proc2.raw() == page2_proc1.raw() ||
                       page1_proc2.raw() == page3_proc1.raw());
  bool page2_reused = (page2_proc2.raw() == page1_proc1.raw() ||
                       page2_proc2.raw() == page2_proc1.raw() ||
                       page2_proc2.raw() == page3_proc1.raw());
  bool page3_reused = (page3_proc2.raw() == page1_proc1.raw() ||
                       page3_proc2.raw() == page2_proc1.raw() ||
                       page3_proc2.raw() == page3_proc1.raw());

  CHECK(page1_reused);
  CHECK(page2_reused);
  CHECK(page3_reused);

  // Free process 2's pages
  page_free_process(2);

  // Test multiple cycles to ensure recycling works consistently
  for (int i = 0; i < 5; i++) {
    PageAddr p1 = page_allocate(i + 10, 1);
    PageAddr p2 = page_allocate(i + 10, 1);

    // Should reuse the freed pages
    bool p1_reused =
        (p1.raw() == page1_proc1.raw() || p1.raw() == page2_proc1.raw() ||
         p1.raw() == page3_proc1.raw());
    bool p2_reused =
        (p2.raw() == page1_proc1.raw() || p2.raw() == page2_proc1.raw() ||
         p2.raw() == page3_proc1.raw());

    CHECK(p1_reused);
    CHECK(p2_reused);

    page_free_process(i + 10);
  }
}

TEST_CASE("process_lookup") {
  memset(procs, 0, sizeof(procs));
  StringView str("proc1");
  // Create a few processes
  process_create(str.ptr, nullptr, 0, false, nullptr);
  process_create("proc2", nullptr, 0, false, nullptr);
  process_create("proc3", nullptr, 0, false, nullptr);
  // Create with conflict
  process_create("proc1", nullptr, 0, false, nullptr);

  // Lookup each process by name
  Process *proc1 = process_lookup(str);
  CHECK(proc1 != nullptr);
  CHECK(proc1->pid == 3);

  Process *proc2 = process_lookup("proc2");
  CHECK(proc2 != nullptr);
  CHECK(proc2->pid == 1);
}
// process tests.cpp
#include "vendor/doctest.h"
#include "ot/kernel/kernel.hpp"

TEST_CASE("page_recycling") {
  // Note: We rely on the __free_ram and __free_ram_end symbols defined in
  // platform-test.cpp. For this test, we'll allocate directly and verify
  // recycling behavior.

  // Initialize memory system (will use existing __free_ram from platform-test)
  memory_init();

  // Allocate pages for process 1
  void *page1_proc1 = page_allocate(1, 1);
  void *page2_proc1 = page_allocate(1, 1);
  void *page3_proc1 = page_allocate(1, 1);

  CHECK(page1_proc1 != nullptr);
  CHECK(page2_proc1 != nullptr);
  CHECK(page3_proc1 != nullptr);
  CHECK(page1_proc1 != page2_proc1);
  CHECK(page2_proc1 != page3_proc1);

  // Free all pages for process 1
  page_free_process(1);

  // Allocate pages for process 2 - should get the same pages back
  void *page1_proc2 = page_allocate(2, 1);
  void *page2_proc2 = page_allocate(2, 1);
  void *page3_proc2 = page_allocate(2, 1);

  // Verify pages are reused (may be in different order due to free list)
  bool page1_reused = (page1_proc2 == page1_proc1 || page1_proc2 == page2_proc1 ||
                       page1_proc2 == page3_proc1);
  bool page2_reused = (page2_proc2 == page1_proc1 || page2_proc2 == page2_proc1 ||
                       page2_proc2 == page3_proc1);
  bool page3_reused = (page3_proc2 == page1_proc1 || page3_proc2 == page2_proc1 ||
                       page3_proc2 == page3_proc1);

  CHECK(page1_reused);
  CHECK(page2_reused);
  CHECK(page3_reused);

  // Free process 2's pages
  page_free_process(2);

  // Test multiple cycles to ensure recycling works consistently
  for (int i = 0; i < 5; i++) {
    void *p1 = page_allocate(i + 10, 1);
    void *p2 = page_allocate(i + 10, 1);

    // Should reuse the freed pages
    bool p1_reused =
        (p1 == page1_proc1 || p1 == page2_proc1 || p1 == page3_proc1);
    bool p2_reused =
        (p2 == page1_proc1 || p2 == page2_proc1 || p2 == page3_proc1);

    CHECK(p1_reused);
    CHECK(p2_reused);

    page_free_process(i + 10);
  }
}
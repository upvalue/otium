#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "vendor/doctest.h"

#include "otk/kernel.hpp"

char test_page_memory[6 * PAGE_SIZE];

TEST_CASE("page_allocator") {

  char *next = (char *)test_page_memory;
  char *page1 = (char *)page_allocate_impl(
      (char *)test_page_memory, test_page_memory + sizeof(test_page_memory),
      &next, 1);

  char *prevnext = next;

  CHECK(next == test_page_memory + PAGE_SIZE);
  CHECK(page1 == test_page_memory);

  char *page2 = (char *)page_allocate_impl(
      (char *)test_page_memory, test_page_memory + sizeof(test_page_memory),
      &next, 1);

  CHECK(next == test_page_memory + 2 * PAGE_SIZE);
  CHECK(page2 == test_page_memory + PAGE_SIZE);
}
TEST_CASE("basic test") { CHECK(true == true); }

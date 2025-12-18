// edit-test.cpp - Unit tests for text editor

#include "vendor/doctest.h"

#include "ot/user/edit.hpp"

using namespace edit;

TEST_CASE("edit: insert mode adds text") {
  Key script[] = {
      key_char('i'), // enter insert mode
      key_char('H'), key_char('e'), key_char('l'), key_char('l'), key_char('o'),
      key_esc(), // back to normal
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]));
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == "Hello");
  }
}

TEST_CASE("edit: backspace deletes character") {
  Key script[] = {
      key_char('i'), key_char('A'), key_char('B'), key_char('C'), key_backspace(), key_esc(),
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]));
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == "AB");
  }
}

TEST_CASE("edit: enter creates new line") {
  Key script[] = {
      key_char('i'), key_char('A'), key_enter(), key_char('B'), key_esc(),
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]));
  CHECK(result.size() >= 2);
  if (result.size() >= 2) {
    CHECK(result[0] == "A");
    CHECK(result[1] == "B");
  }
}

TEST_CASE("edit: arrow keys move cursor") {
  ou::vector<ou::string> initial;
  initial.push_back("ABC");

  Key script[] = {
      key_char('i'),
      key_right(), // move to position 1
      key_char('X'),
      key_esc(),
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), &initial);
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == "AXBC");
  }
}

TEST_CASE("edit: editing existing content") {
  ou::vector<ou::string> initial;
  initial.push_back("Hello");
  initial.push_back("World");

  Key script[] = {
      key_down(), // move to line 2
      key_char('i'),
      key_char('!'),
      key_esc(),
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), &initial);
  CHECK(result.size() >= 2);
  if (result.size() >= 2) {
    CHECK(result[0] == "Hello");
    CHECK(result[1] == "!World");
  }
}

// Motion tests
TEST_CASE("edit: 0 moves to line start") {
  ou::vector<ou::string> initial;
  initial.push_back("Hello");

  Key script[] = {
      key_right(),   key_right(),   key_right(), // Move to middle
      key_char('0'),                             // Move to start
      key_char('i'), key_char('X'), key_esc(),
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), &initial);
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == "XHello");
  }
}

TEST_CASE("edit: $ moves to line end") {
  ou::vector<ou::string> initial;
  initial.push_back("Hello");

  Key script[] = {
      key_char('$'), // Move to end
      key_char('i'),
      key_char('!'),
      key_esc(),
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), &initial);
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == "Hello!");
  }
}

// Operator tests
TEST_CASE("edit: d$ deletes to end of line") {
  ou::vector<ou::string> initial;
  initial.push_back("Hello World");

  Key script[] = {
      key_right(),   key_right(), key_right(), key_right(), key_right(), // After "Hello"
      key_char('d'),
      key_char('$'), // Delete to end
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), &initial);
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == "Hello");
  }
}

TEST_CASE("edit: d0 deletes to start of line") {
  ou::vector<ou::string> initial;
  initial.push_back("Hello World");

  Key script[] = {
      key_right(),   key_right(), key_right(), key_right(), key_right(), // Move to position 5 (after "Hello")
      key_char('d'),
      key_char('0'), // Delete from 0 to 5
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), &initial);
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == " World");
  }
}

TEST_CASE("edit: dd deletes entire line") {
  ou::vector<ou::string> initial;
  initial.push_back("Line 1");
  initial.push_back("Line 2");
  initial.push_back("Line 3");

  Key script[] = {
      key_down(), // Go to Line 2
      key_char('d'),
      key_char('d'), // Delete line
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), &initial);
  CHECK(result.size() == 2);
  if (result.size() == 2) {
    CHECK(result[0] == "Line 1");
    CHECK(result[1] == "Line 3");
  }
}

TEST_CASE("edit: d followed by Esc cancels") {
  ou::vector<ou::string> initial;
  initial.push_back("Hello");

  Key script[] = {
      key_char('d'),
      key_esc(), // Cancel delete
      key_char('i'), key_char('X'), key_esc(),
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), &initial);
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == "XHello");
  }
}

// Simple mode tests
TEST_CASE("edit: simple mode starts in INSERT") {
  Key script[] = {
      key_char('H'), key_char('e'), key_char('l'), key_char('l'), key_char('o'),
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), nullptr, EditorStyle::SIMPLE);
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == "Hello");
  }
}

TEST_CASE("edit: simple mode C-a moves to line start") {
  ou::vector<ou::string> initial;
  initial.push_back("Hello");

  Key script[] = {
      key_right(), key_right(), key_right(), // Move to middle
      key_ctrl('a'),                         // Move to start
      key_char('X'),
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), &initial, EditorStyle::SIMPLE);
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == "XHello");
  }
}

TEST_CASE("edit: simple mode C-e moves to line end") {
  ou::vector<ou::string> initial;
  initial.push_back("Hello");

  Key script[] = {
      key_ctrl('e'), // Move to end
      key_char('!'),
  };
  auto result = edit_test_run(script, sizeof(script) / sizeof(script[0]), &initial, EditorStyle::SIMPLE);
  CHECK(result.size() >= 1);
  if (result.size() >= 1) {
    CHECK(result[0] == "Hello!");
  }
}

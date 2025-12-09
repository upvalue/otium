// tevl-test.cpp - Test backend for TEVL editor
// Allows scripted key input for automated testing without a real terminal

#include "ot/user/tevl.hpp"

// Access to globals defined in tevl.cpp (in global namespace due to 'using namespace tevl')
// These are declared outside any namespace in tevl.cpp
extern tevl::Editor e;
extern bool running;
extern tevl::Backend *be;

// Reset running flag before test
static void reset_running() { running = true; }

namespace tevl {

struct TestBackend : Backend {
  const Key *keys;
  size_t key_count;
  size_t key_pos = 0;
  Coord window_size = {80, 24};

  TestBackend(const Key *keys, size_t count) : keys(keys), key_count(count) { error_msg = ""; }

  Result<Key, EditorErr> readKey() override {
    if (key_pos >= key_count) {
      // No more input - set running to false to exit
      running = false;
      return Result<Key, EditorErr>::ok(Key{});
    }
    return Result<Key, EditorErr>::ok(keys[key_pos++]);
  }

  EditorErr setup() override { return EditorErr::NONE; }
  void teardown() override {}
  void clear() override {}
  void refresh() override {}
  Coord getWindowSize() override { return window_size; }
  void render(const Editor &ed) override {}
  void debug_print(const ou::string &msg) override {}
};

ou::vector<ou::string> tevl_test_run(const Key *keys, size_t count, const ou::vector<ou::string> *initial_lines) {
  // Reset editor state by clearing all fields (use :: to access global)
  ::e.row_offset = 0;
  ::e.col_offset = 0;
  ::e.cx = 0;
  ::e.cy = 0;
  ::e.rx = 0;
  ::e.dirty = 0;
  ::e.mode = EditorMode::NORMAL;
  ::e.pending_operator = Operator::NONE;
  ::e.lines.clear();
  ::e.file_lines.clear();
  ::e.render_lines.clear();
  ::e.file_name.clear();
  ::e.status_line.clear();
  ::e.message_line.clear();
  ::e.command_line.clear();
  ::e.last_message_time = 0;

  ::reset_running();

  // Set initial content if provided
  if (initial_lines) {
    for (size_t i = 0; i < initial_lines->size(); i++) {
      ::e.file_lines.push_back((*initial_lines)[i]);
    }
  }

  // Create and run with test backend
  TestBackend test_backend(keys, count);
  tevl_main(&test_backend, nullptr);

  // Return the final file contents by moving
  return static_cast<ou::vector<ou::string> &&>(::e.file_lines);
}

} // namespace tevl

// edit-test.cpp - Test backend for text editor
// Allows scripted key input for automated testing without a real terminal

#include "ot/user/edit.hpp"
#include "ot/user/tcl.hpp"

namespace edit {

// Global editor instance for tests
static Editor test_editor;

struct TestBackend : Backend {
  const Key *keys;
  size_t key_count;
  size_t key_pos = 0;
  Coord window_size = {80, 24};
  Editor *editor_ptr;

  TestBackend(const Key *keys, size_t count, Editor *ed) : keys(keys), key_count(count), editor_ptr(ed) {
    error_msg = "";
  }

  Result<Key, EditorErr> readKey() override {
    if (key_pos >= key_count) {
      // No more input - set running to false to exit
      editor_ptr->running = false;
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

ou::vector<ou::string> edit_test_run(const Key *keys, size_t count, const ou::vector<ou::string> *initial_lines) {
  // Reset editor state
  test_editor.row_offset = 0;
  test_editor.col_offset = 0;
  test_editor.cx = 0;
  test_editor.cy = 0;
  test_editor.rx = 0;
  test_editor.dirty = 0;
  test_editor.mode = EditorMode::NORMAL;
  test_editor.pending_operator = Operator::NONE;
  test_editor.lines.clear();
  test_editor.file_lines.clear();
  test_editor.render_lines.clear();
  test_editor.file_name.clear();
  test_editor.status_line.clear();
  test_editor.message_line.clear();
  test_editor.command_line.clear();
  test_editor.last_message_time = 0;
  test_editor.running = true;

  // Set initial content if provided
  if (initial_lines) {
    for (size_t i = 0; i < initial_lines->size(); i++) {
      test_editor.file_lines.push_back((*initial_lines)[i]);
    }
  }

  // Create and run with test backend
  TestBackend test_backend(keys, count, &test_editor);
  edit_run(&test_backend, &test_editor, nullptr, nullptr);

  // Return the final file contents by moving
  return static_cast<ou::vector<ou::string> &&>(test_editor.file_lines);
}

} // namespace edit

// tcl-repl.cpp - Standalone TCL REPL using bestline for line editing
#include "ot/vendor/bestline/bestline.h"
#include "ot/user/tcl.hpp"
#include "ot/lib/file.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// Provide ou memory allocation functions using stdlib
void *ou_malloc(size_t size) { return malloc(size); }
void ou_free(void *ptr) { free(ptr); }
void *ou_realloc(void *ptr, size_t size) { return realloc(ptr, size); }

// Global flag for quit command
static bool should_quit = false;

// Action types
enum ActionType { ACTION_REPL, ACTION_FILE };

struct Action {
  ActionType type;
  const char *filename; // Only used for ACTION_FILE
};

// Run the REPL
void run_repl(tcl::Interp &interp) {
  should_quit = false;

  printf("TCL REPL - Type 'quit' or Ctrl+D to exit\n\n");

  char *line;
  while ((line = bestline("> ")) != NULL) {
    // Empty line, continue
    if (line[0] == '\0') {
      free(line);
      continue;
    }

    // Add to history
    bestlineHistoryAdd(line);

    // Evaluate with TCL interpreter
    tcl::Status status = interp.eval(tcl::string_view(line));

    // Print result or error
    if (status == tcl::S_OK) {
      if (!interp.result.empty()) {
        printf("%s\n", interp.result.c_str());
      }
    } else {
      printf("Error: %s\n", interp.result.c_str());
    }

    free(line);

    // Check if quit was called
    if (should_quit) {
      break;
    }
  }

  printf("\n");
}

// Run a TCL file
bool run_file(tcl::Interp &interp, const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
    return false;
  }

  // Read entire file into memory
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *content = (char *)malloc(size + 1);
  if (!content) {
    fprintf(stderr, "Error: Out of memory\n");
    fclose(f);
    return false;
  }

  size_t read_size = fread(content, 1, size, f);
  content[read_size] = '\0';
  fclose(f);

  // Evaluate the file
  tcl::Status status = interp.eval(tcl::string_view(content, read_size));

  free(content);

  if (status != tcl::S_OK) {
    fprintf(stderr, "Error in %s: %s\n", filename, interp.result.c_str());
    return false;
  }

  return true;
}

// Filesystem commands for POSIX
static tcl::Status cmd_fs_read(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("fs/read", argv, 2, 2)) {
    return tcl::S_ERR;
  }

  ou::File file(argv[1].c_str(), ou::FileMode::READ);
  ErrorCode err = file.open();
  if (err != ErrorCode::NONE) {
    char buf[256];
    snprintf(buf, sizeof(buf), "fs/read: failed to open file '%s': %s",
              argv[1].c_str(), error_code_to_string(err));
    i.result = buf;
    return tcl::S_ERR;
  }

  ou::string content;
  err = file.read_all(content);
  if (err != ErrorCode::NONE) {
    char buf[256];
    snprintf(buf, sizeof(buf), "fs/read: failed to read file '%s': %s",
              argv[1].c_str(), error_code_to_string(err));
    i.result = buf;
    return tcl::S_ERR;
  }

  i.result = content;
  return tcl::S_OK;
}

static tcl::Status cmd_fs_write(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("fs/write", argv, 3, 3)) {
    return tcl::S_ERR;
  }

  ou::File file(argv[1].c_str(), ou::FileMode::WRITE);
  ErrorCode err = file.open();
  if (err != ErrorCode::NONE) {
    char buf[256];
    snprintf(buf, sizeof(buf), "fs/write: failed to open file '%s': %s",
              argv[1].c_str(), error_code_to_string(err));
    i.result = buf;
    return tcl::S_ERR;
  }

  err = file.write_all(argv[2]);
  if (err != ErrorCode::NONE) {
    char buf[256];
    snprintf(buf, sizeof(buf), "fs/write: failed to write file '%s': %s",
              argv[1].c_str(), error_code_to_string(err));
    i.result = buf;
    return tcl::S_ERR;
  }

  return tcl::S_OK;
}

static tcl::Status cmd_fs_create(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("fs/create", argv, 2, 2)) {
    return tcl::S_ERR;
  }

  // Create empty file using fopen
  FILE *f = fopen(argv[1].c_str(), "w");
  if (!f) {
    char buf[256];
    snprintf(buf, sizeof(buf), "fs/create: failed to create file '%s'", argv[1].c_str());
    i.result = buf;
    return tcl::S_ERR;
  }
  fclose(f);

  return tcl::S_OK;
}

int main(int argc, char *argv[]) {
  // Create TCL interpreter
  tcl::Interp interp;
  tcl::register_core_commands(interp);

  // Allocate MessagePack buffer and register functions
  char *mpack_buffer = (char *)malloc(OT_PAGE_SIZE);
  if (mpack_buffer) {
    interp.register_mpack_functions(mpack_buffer, OT_PAGE_SIZE);
  }

  // Register quit command
  interp.register_command("quit",
                          [](tcl::Interp &i, tcl::vector<tcl::string> &argv,
                             tcl::ProcPrivdata *privdata) -> tcl::Status {
                            should_quit = true;
                            return tcl::S_OK;
                          });

  // Register filesystem commands
  interp.register_command("fs/read", cmd_fs_read, nullptr,
                          "[fs/read filename] => string - Read entire file into a string");
  interp.register_command("fs/write", cmd_fs_write, nullptr,
                          "[fs/write filename content] => nil - Write string to a file");
  interp.register_command("fs/create", cmd_fs_create, nullptr,
                          "[fs/create filename] => nil - Create a new empty file");

  // Parse arguments into actions
  std::vector<Action> actions;

  if (argc == 1) {
    // No arguments - default to REPL
    actions.push_back({ACTION_REPL, nullptr});
  } else {
    // Process arguments
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--repl") == 0) {
        actions.push_back({ACTION_REPL, nullptr});
      } else {
        actions.push_back({ACTION_FILE, argv[i]});
      }
    }
  }

  // Execute actions in order
  for (const auto &action : actions) {
    if (action.type == ACTION_REPL) {
      run_repl(interp);
    } else {
      if (!run_file(interp, action.filename)) {
        free(mpack_buffer);
        return 1;
      }
    }
  }

  free(mpack_buffer);
  return 0;
}

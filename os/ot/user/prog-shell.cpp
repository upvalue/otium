// prog-shell.cpp - TCL shell implementation
#include "ot/lib/arguments.hpp"
#include "ot/lib/file.hpp"
#include "ot/lib/messages.hpp"
#include "ot/lib/mpack/mpack-utils.hpp"
#include "ot/user/gen/filesystem-client.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/tcl.hpp"
#include "ot/user/user.hpp"

#include "ot/user/prog.h"

#include "ot/user/gen/tcl-vars.hpp"

#define SHELL_PAGES 10

// Shell-specific storage inheriting from LocalStorage
struct ShellStorage : public LocalStorage {
  char buffer[OT_PAGE_SIZE];
  size_t buffer_i;
  bool running;

  ShellStorage() {
    // Initialize memory allocator
    process_storage_init(SHELL_PAGES);
    // Initialize shell state
    buffer_i = 0;
    running = true;
  }
};

tcl::Status cmd_proc_lookup(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("proc/lookup", argv, 2, 2)) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "arity check failed");
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }
  Pid proc_pid = ou_proc_lookup(argv[1].c_str());
  if (proc_pid == PID_NONE) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "proc not found");
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }
  char buf[32];
  osnprintf(buf, sizeof(buf), "%lu", proc_pid.raw());
  i.result = buf;
  return tcl::S_OK;
}

tcl::Status cmd_ipc_send(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  // ipc/send <pid> <method> [flags] [arg1] [arg2] [arg3]
  if (!i.arity_check("ipc/send", argv, 3, 7)) {
    return tcl::S_ERR;
  }

  // Validate and parse pid
  if (!i.int_check("ipc/send", argv, 1)) {
    return tcl::S_ERR;
  }
  Pid pid = Pid((uintptr_t)atoi(argv[1].c_str()));

  // Validate and parse method
  if (!i.int_check("ipc/send", argv, 2)) {
    return tcl::S_ERR;
  }
  intptr_t method = (intptr_t)atoi(argv[2].c_str());

  // Parse optional flags (default to IPC_FLAG_NONE)
  uintptr_t flags = IPC_FLAG_NONE;
  size_t arg_start = 3;
  if (argv.size() > 3) {
    if (!i.int_check("ipc/send", argv, 3)) {
      return tcl::S_ERR;
    }
    flags = (uintptr_t)atoi(argv[3].c_str());
    arg_start = 4;
  }

  // Parse optional arguments (default to 0)
  intptr_t arg0 = 0, arg1 = 0, arg2 = 0;
  if (argv.size() > arg_start) {
    if (!i.int_check("ipc/send", argv, arg_start)) {
      return tcl::S_ERR;
    }
    arg0 = (intptr_t)atoi(argv[arg_start].c_str());
  }
  if (argv.size() > arg_start + 1) {
    if (!i.int_check("ipc/send", argv, arg_start + 1)) {
      return tcl::S_ERR;
    }
    arg1 = (intptr_t)atoi(argv[arg_start + 1].c_str());
  }
  if (argv.size() > arg_start + 2) {
    if (!i.int_check("ipc/send", argv, arg_start + 2)) {
      return tcl::S_ERR;
    }
    arg2 = (intptr_t)atoi(argv[arg_start + 2].c_str());
  }

  // Send IPC message
  IpcResponse resp = ou_ipc_send(pid, flags, method, arg0, arg1, arg2);

  // Format response as a list: <error_code> <value1> <value2> <value3>
  char buf[256];
  osnprintf(buf, sizeof(buf), "%d %ld %ld %ld", (int)resp.error_code, (long)resp.values[0], (long)resp.values[1],
            (long)resp.values[2]);
  i.result = buf;

  return tcl::S_OK;
}

tcl::Status cmd_error_string(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("error/string", argv, 2, 2)) {
    return tcl::S_ERR;
  }

  if (!i.int_check("error/string", argv, 1)) {
    return tcl::S_ERR;
  }

  int error_code = atoi(argv[1].c_str());
  const char *error_str = error_code_to_string((ErrorCode)error_code);
  i.result = error_str;

  return tcl::S_OK;
}

tcl::Status cmd_fs_read(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("fs/read", argv, 2, 2)) {
    return tcl::S_ERR;
  }

  ou::File file(argv[1].c_str(), ou::FileMode::READ);
  ErrorCode err = file.open();
  if (err != ErrorCode::NONE) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/read: failed to open file '%s': %s", argv[1].c_str(),
              error_code_to_string(err));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  ou::string content;
  err = file.read_all(content);
  if (err != ErrorCode::NONE) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/read: failed to read file '%s': %s", argv[1].c_str(),
              error_code_to_string(err));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  i.result = content;
  return tcl::S_OK;
}

tcl::Status cmd_fs_write(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("fs/write", argv, 3, 3)) {
    return tcl::S_ERR;
  }

  ou::File file(argv[1].c_str(), ou::FileMode::WRITE);
  ErrorCode err = file.open();
  if (err != ErrorCode::NONE) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/write: failed to open file '%s': %s", argv[1].c_str(),
              error_code_to_string(err));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  err = file.write_all(argv[2]);
  if (err != ErrorCode::NONE) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/write: failed to write file '%s': %s", argv[1].c_str(),
              error_code_to_string(err));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  return tcl::S_OK;
}

tcl::Status cmd_fs_create(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("fs/create", argv, 2, 2)) {
    return tcl::S_ERR;
  }

  // Lookup filesystem server
  Pid fs_pid = ou_proc_lookup("filesystem");
  if (fs_pid == PID_NONE) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/create: filesystem server not found");
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  // Call create_file via IPC
  FilesystemClient client(fs_pid);
  auto result = client.create_file(argv[1]);
  if (result.is_err()) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/create: failed to create file '%s': %s", argv[1].c_str(),
              error_code_to_string(result.error()));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  return tcl::S_OK;
}

void shell_main() {
  oprintf("SHELL BEGIN\n");

  // Get the storage page and initialize ShellStorage
  void *storage_page = ou_get_storage().as_ptr();
  ShellStorage *s = new (storage_page) ShellStorage();

  char *mp_page = (char *)ou_alloc_page();

  tcl::Interp i;
  tcl::register_core_commands(i);

  i.register_mpack_functions(mp_page, OT_PAGE_SIZE);

  // Register IPC method ID variables
  register_ipc_method_vars(i);

  // Execute shellrc startup script
#include "shellrc.hpp"

  oprintf("tcl shell ready\n");

  // Register quit command - uses local_storage global
  i.register_command(
      "quit",
      [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
        ((ShellStorage *)local_storage)->running = false;
        return tcl::S_OK;
      },
      nullptr, "[quit] - Quit the shell");

  // Register shutdown command - terminates all processes and exits kernel
  i.register_command(
      "shutdown",
      [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
        ou_shutdown();
        return tcl::S_OK; // Never reached
      },
      nullptr, "[shutdown] - Shutdown all processes and exit the kernel");

  // cause a crash by dereferencing a random addr
  i.register_command(
      "crash",
      [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
        char *p = (char *)0x10;
        (*p) = 0;
        return tcl::S_OK;
      },
      nullptr, "[crash] - Cause a crash");

  // Lookup a procedure's PID
  i.register_command("proc/lookup", cmd_proc_lookup, nullptr,
                     "[proc/lookup name:string] => pid:int - Lookup a procedure's PID");

  // IPC command
  i.register_command("ipc/send", cmd_ipc_send, nullptr,
                     "[ipc/send pid:int method:int flags?:int arg1?:int arg2?:int arg3?:int] => list - Send IPC "
                     "message and return response (error_code val1 val2 val3)");

  // Error code to string conversion
  i.register_command("error/string", cmd_error_string, nullptr,
                     "[error/string code:int] => string - Convert error code to string");

  // Filesystem commands
  i.register_command("fs/read", cmd_fs_read, nullptr,
                     "[fs/read filename:string] => string - Read entire file into a string");
  i.register_command("fs/write", cmd_fs_write, nullptr,
                     "[fs/write filename:string content:string] => nil - Write string to a file");
  i.register_command("fs/create", cmd_fs_create, nullptr,
                     "[fs/create filename:string] => nil - Create a new empty file");

  // load shellrc
  tcl::Status shellrc_status = i.eval(tcl::string_view(shellrc_content));
  if (shellrc_status != tcl::S_OK) {
    oprintf("shellrc error: %s\n", i.result.c_str());
  }

  while (s->running) {
    oprintf("> ");
    while (s->running) {
      char c = ogetchar();
      if (c >= 32 && c <= 126) {
        s->buffer[s->buffer_i++] = c;
        if (s->buffer_i == sizeof(s->buffer)) {
          oprintf("buffer full\n");
          s->buffer_i = 0;
        }
        oprintf("%c", c);
      }

      // new line
      if (c == 13) {
        s->buffer[s->buffer_i] = 0;
        oputchar('\n');
        tcl::Status status = i.eval(s->buffer);
        if (status != tcl::S_OK) {
          oprintf("tcl error: %s\n", i.result.c_str());
        } else {
          oprintf("result: %s\n", i.result.c_str());
        }
        s->buffer_i = 0;
        break;
      }

      // bksp
      if ((c == 8 || c == 127) && s->buffer_i != 0) {
        oprintf("\b \b");
        s->buffer_i--;
      }
      ou_yield();
    }
  }

  oprintf("exiting shell\n");
}

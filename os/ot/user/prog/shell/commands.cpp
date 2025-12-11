// commands.cpp - Shared shell command implementations
#include "ot/user/prog/shell/commands.hpp"

#include <stdio.h>

#include "ot/lib/file.hpp"
#include "ot/lib/messages.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"
#include "ot/user/gen/filesystem-client.hpp"
#include "ot/user/prog/shell/shell.hpp"
#include "ot/user/user.hpp"

namespace shell {

tcl::Status cmd_proc_lookup(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("proc/lookup", argv, 2, 2)) {
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "arity check failed");
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }
  Pid proc_pid = ou_proc_lookup(argv[1].c_str());
  if (proc_pid == PID_NONE) {
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "proc not found");
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%lu", proc_pid.raw());
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
  snprintf(buf, sizeof(buf), "%d %ld %ld %ld", (int)resp.error_code, (long)resp.values[0], (long)resp.values[1],
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

tcl::Status cmd_length(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("length", argv, 2, 2)) {
    return tcl::S_ERR;
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%zu", argv[1].length());
  i.result = buf;
  return tcl::S_OK;
}

tcl::Status cmd_fs_read(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("fs/read", argv, 2, 2)) {
    return tcl::S_ERR;
  }

  ou::File file(argv[1].c_str(), ou::FileMode::READ);
  ErrorCode err = file.open();
  if (err != ErrorCode::NONE) {
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/read: failed to open file '%s': %s", argv[1].c_str(),
             error_code_to_string(err));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  ou::string content;
  err = file.read_all(content);
  if (err != ErrorCode::NONE) {
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/read: failed to read file '%s': %s", argv[1].c_str(),
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
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/write: failed to open file '%s': %s", argv[1].c_str(),
             error_code_to_string(err));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  err = file.write_all(argv[2]);
  if (err != ErrorCode::NONE) {
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/write: failed to write file '%s': %s", argv[1].c_str(),
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
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/create: filesystem server not found");
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  // Call create_file via IPC
  FilesystemClient client(fs_pid);
  auto result = client.create_file(argv[1]);
  if (result.is_err()) {
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "fs/create: failed to create file '%s': %s", argv[1].c_str(),
             error_code_to_string(result.error()));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  return tcl::S_OK;
}

tcl::Status cmd_dofile(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("dofile", argv, 2, 2)) {
    return tcl::S_ERR;
  }

  ou::File file(argv[1].c_str(), ou::FileMode::READ);
  ErrorCode err = file.open();
  if (err != ErrorCode::NONE) {
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "dofile: failed to open file '%s': %s", argv[1].c_str(),
             error_code_to_string(err));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  ou::string content;
  err = file.read_all(content);
  if (err != ErrorCode::NONE) {
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "dofile: failed to read file '%s': %s", argv[1].c_str(),
             error_code_to_string(err));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  return i.eval(content);
}

tcl::Status cmd_proc_is_alive(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("proc/is-alive", argv, 2, 2)) {
    return tcl::S_ERR;
  }
  if (!i.int_check("proc/is-alive", argv, 1)) {
    return tcl::S_ERR;
  }
  Pid pid = Pid((uintptr_t)atoi(argv[1].c_str()));
  bool alive = ou_proc_is_alive(pid);
  i.result = alive ? "1" : "0";
  return tcl::S_OK;
}

tcl::Status cmd_run(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  constexpr size_t MAX_SPAWN_ARGS = 32;
  if (!i.arity_check("run", argv, 2, MAX_SPAWN_ARGS + 2)) { // At least program name, unlimited args
    return tcl::S_ERR;
  }

  const char *program_name = argv[1].c_str();

  // Build argv array for the spawned process
  // argv[0] should be the program name
  char *spawn_argv[MAX_SPAWN_ARGS];
  int spawn_argc = 0;

  for (size_t j = 1; j < argv.size() && spawn_argc < (int)MAX_SPAWN_ARGS; j++) {
    spawn_argv[spawn_argc++] = (char *)argv[j].c_str();
  }

  Pid new_pid = ou_proc_spawn(program_name, spawn_argc, spawn_argv);

  if (new_pid == PID_NONE) {
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "run: failed to spawn '%s' (unknown program or process limit)",
             program_name);
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "%lu", new_pid.raw());
  i.result = buf;
  return tcl::S_OK;
}

tcl::Status cmd_quit(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  ((ShellStorage *)local_storage)->running = false;
  return tcl::S_OK;
}

tcl::Status cmd_shutdown(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  ou_shutdown();
  return tcl::S_OK; // Never reached
}

tcl::Status cmd_dir_ls(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("dir/ls", argv, 1, 2)) {
    return tcl::S_ERR;
  }

  Pid fs_pid = ou_proc_lookup("filesystem");
  if (fs_pid == PID_NONE) {
    i.result = "dir/ls: filesystem server not found";
    return tcl::S_ERR;
  }

  FilesystemClient client(fs_pid);
  ou::string path = (argv.size() > 1) ? argv[1].c_str() : "/";

  auto result = client.list_dir(path);
  if (result.is_err()) {
    snprintf(ot_scratch_buffer, OT_PAGE_SIZE, "dir/ls: %s", error_code_to_string(result.error()));
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }

  // Read msgpack array from comm page
  PageAddr comm = ou_get_comm_page();
  MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);

  uint32_t count;
  reader.enter_array(count);

  // Build Tcl list result
  tcl::vector<tcl::string> entries;
  for (uint32_t j = 0; j < count; j++) {
    StringView sv;
    reader.read_string(sv);
    entries.push_back(tcl::string(sv.ptr, sv.len));
  }

  tcl::list_format(entries, i.result);
  return tcl::S_OK;
}

void register_shell_commands(tcl::Interp &i) {
  // Lookup a procedure's PID
  i.register_command("proc/lookup", cmd_proc_lookup, nullptr,
                     "[proc/lookup name:string] => pid:int - Lookup a procedure's PID");

  // Check if a process is alive
  i.register_command("proc/is-alive", cmd_proc_is_alive, nullptr,
                     "[proc/is-alive pid:int] => bool - Check if a process is alive (1=alive, 0=dead)");

  // IPC command
  i.register_command("ipc/send", cmd_ipc_send, nullptr,
                     "[ipc/send pid:int method:int flags?:int arg1?:int arg2?:int arg3?:int] => list - Send IPC "
                     "message and return response (error_code val1 val2 val3)");

  // Error code to string conversion
  i.register_command("error/string", cmd_error_string, nullptr,
                     "[error/string code:int] => string - Convert error code to string");

  // String commands
  i.register_command("length", cmd_length, nullptr, "[length str:string] => int - Return the length of a string");

  // Filesystem commands
  i.register_command("fs/read", cmd_fs_read, nullptr,
                     "[fs/read filename:string] => string - Read entire file into a string");
  i.register_command("fs/write", cmd_fs_write, nullptr,
                     "[fs/write filename:string content:string] => nil - Write string to a file");
  i.register_command("fs/create", cmd_fs_create, nullptr,
                     "[fs/create filename:string] => nil - Create a new empty file");
  i.register_command("dofile", cmd_dofile, nullptr, "[dofile filename:string] => result - Execute a Tcl script file");

  // Directory commands
  i.register_command("dir/ls", cmd_dir_ls, nullptr,
                     "[dir/ls path?] => list - List directory contents (dirs have trailing /)");

  // Process spawning
  i.register_command("run", cmd_run, nullptr,
                     "[run program:string args...] => pid:int - Spawn a new process and return its PID");

  // Shell control commands
  i.register_command("quit", cmd_quit, nullptr, "[quit] - Quit the shell");
  i.register_command("shutdown", cmd_shutdown, nullptr, "[shutdown] - Shutdown all processes and exit the kernel");
}

} // namespace shell

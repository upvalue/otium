// commands.hpp - Shared shell command implementations
#pragma once

#include "ot/user/tcl.hpp"

namespace shell {

// Shell command implementations
tcl::Status cmd_proc_lookup(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata);
tcl::Status cmd_ipc_send(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata);
tcl::Status cmd_error_string(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata);
tcl::Status cmd_fs_read(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata);
tcl::Status cmd_fs_write(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata);
tcl::Status cmd_fs_create(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata);

// Register all shell commands to an interpreter
void register_shell_commands(tcl::Interp &i);

} // namespace shell

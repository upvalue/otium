// tcl-test.cpp - unit tests for TCL interpreter
#include "ot/common.h"
#include "ot/user/tcl.hpp"
#include "vendor/doctest.h"

#ifdef OT_POSIX
#include <stdlib.h>
// Provide OU memory functions for test environment
void *ou_malloc(size_t size) { return malloc(size); }
void ou_free(void *ptr) { free(ptr); }
void *ou_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
#endif

TEST_CASE("tcl - basic evaluation") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("empty string") {
    CHECK(i.eval("") == tcl::S_OK);
  }

  SUBCASE("simple command") {
    CHECK(i.eval("set x 42") == tcl::S_OK);
  }

  SUBCASE("multiple commands") {
    CHECK(i.eval("set x 1; set y 2") == tcl::S_OK);
  }
}

TEST_CASE("tcl - variables") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("set and get variable") {
    CHECK(i.eval("set x 42") == tcl::S_OK);
    CHECK(i.eval("+ $x 0") == tcl::S_OK);
    CHECK(i.result.compare("42") == 0);
  }

  SUBCASE("undefined variable error") {
    CHECK(i.eval("set y $undefined") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("variable substitution in command") {
    CHECK(i.eval("set x 5") == tcl::S_OK);
    CHECK(i.eval("set y 3") == tcl::S_OK);
    CHECK(i.eval("+ $x $y") == tcl::S_OK);
    CHECK(i.result.compare("8") == 0);
  }
}

TEST_CASE("tcl - arithmetic operators") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("addition") {
    CHECK(i.eval("+ 5 3") == tcl::S_OK);
    CHECK(i.result.compare("8") == 0);
  }

  SUBCASE("subtraction") {
    CHECK(i.eval("- 10 4") == tcl::S_OK);
    CHECK(i.result.compare("6") == 0);
  }

  SUBCASE("multiplication") {
    CHECK(i.eval("* 7 6") == tcl::S_OK);
    CHECK(i.result.compare("42") == 0);
  }

  SUBCASE("division") {
    CHECK(i.eval("/ 20 5") == tcl::S_OK);
    CHECK(i.result.compare("4") == 0);
  }

  SUBCASE("zero operations") {
    CHECK(i.eval("+ 0 5") == tcl::S_OK);
    CHECK(i.result.compare("5") == 0);
    CHECK(i.eval("* 10 0") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("arity error") {
    CHECK(i.eval("+ 5") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("non-integer error") {
    CHECK(i.eval("+ abc 5") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }
}

TEST_CASE("tcl - comparison operators") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("equal - true") {
    CHECK(i.eval("== 5 5") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("equal - false") {
    CHECK(i.eval("== 5 3") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("not equal - true") {
    CHECK(i.eval("!= 5 3") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("not equal - false") {
    CHECK(i.eval("!= 5 5") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("greater than - true") {
    CHECK(i.eval("> 10 5") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("greater than - false") {
    CHECK(i.eval("> 3 5") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("less than - true") {
    CHECK(i.eval("< 3 5") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("less than - false") {
    CHECK(i.eval("< 10 5") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("greater than or equal - true") {
    CHECK(i.eval(">= 5 5") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
    CHECK(i.eval(">= 10 5") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("greater than or equal - false") {
    CHECK(i.eval(">= 3 5") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("less than or equal - true") {
    CHECK(i.eval("<= 5 5") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
    CHECK(i.eval("<= 3 5") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("less than or equal - false") {
    CHECK(i.eval("<= 10 5") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }
}

TEST_CASE("tcl - if statement") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("if true - then branch") {
    CHECK(i.eval("if {== 5 5} {set x 1}") == tcl::S_OK);
    CHECK(i.eval("+ $x 0") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("if false - no else") {
    CHECK(i.eval("if {== 5 3} {set x 1}") == tcl::S_OK);
    CHECK(i.eval("set y $x") == tcl::S_ERR); // x should not be set
  }

  SUBCASE("if false - else branch") {
    CHECK(i.eval("if {== 5 3} {set x 1} else {set x 2}") == tcl::S_OK);
    CHECK(i.eval("+ $x 0") == tcl::S_OK);
    CHECK(i.result.compare("2") == 0);
  }

  SUBCASE("if true - else branch not executed") {
    CHECK(i.eval("if {== 5 5} {set x 1} else {set x 2}") == tcl::S_OK);
    CHECK(i.eval("+ $x 0") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }
}

TEST_CASE("tcl - while loop") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("simple while loop") {
    CHECK(i.eval("set i 0") == tcl::S_OK);
    CHECK(i.eval("while {< $i 5} {set i [+ $i 1]}") == tcl::S_OK);
    CHECK(i.eval("+ $i 0") == tcl::S_OK);
    CHECK(i.result.compare("5") == 0);
  }

  SUBCASE("while with break") {
    CHECK(i.eval("set i 0") == tcl::S_OK);
    CHECK(i.eval("while {< $i 10} {set i [+ $i 1]; if {== $i 3} {break}}") ==
          tcl::S_OK);
    CHECK(i.eval("+ $i 0") == tcl::S_OK);
    CHECK(i.result.compare("3") == 0);
  }

  SUBCASE("while with continue") {
    CHECK(i.eval("set i 0") == tcl::S_OK);
    CHECK(i.eval("set sum 0") == tcl::S_OK);
    CHECK(i.eval("while {< $i 5} {set i [+ $i 1]; if {== $i 3} {continue}; "
                 "set sum [+ $sum $i]}") == tcl::S_OK);
    CHECK(i.eval("+ $sum 0") == tcl::S_OK);
    // sum should be 1 + 2 + 4 + 5 = 12 (skips 3)
    CHECK(i.result.compare("12") == 0);
  }

  SUBCASE("while never executes") {
    CHECK(i.eval("set x 0") == tcl::S_OK);
    CHECK(i.eval("while {== 1 0} {set x 1}") == tcl::S_OK);
    CHECK(i.eval("+ $x 0") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }
}

TEST_CASE("tcl - procedures") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("simple procedure") {
    CHECK(i.eval("proc double {x} {+ $x $x}") == tcl::S_OK);
    CHECK(i.eval("double 5") == tcl::S_OK);
    CHECK(i.result.compare("10") == 0);
  }

  SUBCASE("procedure with multiple arguments") {
    CHECK(i.eval("proc add {a b} {+ $a $b}") == tcl::S_OK);
    CHECK(i.eval("add 3 7") == tcl::S_OK);
    CHECK(i.result.compare("10") == 0);
  }

  SUBCASE("procedure with return") {
    CHECK(i.eval("proc test {x} {if {> $x 5} {return 1}; return 0}") ==
          tcl::S_OK);
    CHECK(i.eval("test 10") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
    CHECK(i.eval("test 3") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("procedure arity error") {
    CHECK(i.eval("proc foo {x y} {+ $x $y}") == tcl::S_OK);
    CHECK(i.eval("foo 5") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("procedure with local variables") {
    CHECK(i.eval("proc test {} {set local 42; return $local}") == tcl::S_OK);
    CHECK(i.eval("test") == tcl::S_OK);
    CHECK(i.result.compare("42") == 0);
    // local variable should not exist in global scope
    CHECK(i.eval("set x $local") == tcl::S_ERR);
  }
}

TEST_CASE("tcl - command substitution") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("simple substitution") {
    CHECK(i.eval("+ [+ 1 2] 3") == tcl::S_OK);
    CHECK(i.result.compare("6") == 0);
  }

  SUBCASE("nested substitution") {
    CHECK(i.eval("+ [+ [+ 1 2] 3] 4") == tcl::S_OK);
    CHECK(i.result.compare("10") == 0);
  }

  SUBCASE("substitution with variables") {
    CHECK(i.eval("set x 5") == tcl::S_OK);
    CHECK(i.eval("+ [+ $x 3] 2") == tcl::S_OK);
    CHECK(i.result.compare("10") == 0);
  }
}

TEST_CASE("tcl - error cases") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("unknown command") {
    CHECK(i.eval("unknown_command") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("arity check - too few args") {
    CHECK(i.eval("set x") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("arity check - too many args") {
    CHECK(i.eval("set x y z") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("duplicate command registration") {
    tcl::Interp j;
    CHECK(j.register_command("test", [](tcl::Interp &i,
                                         tcl::vector<tcl::string> &argv,
                                         tcl::ProcPrivdata *privdata) {
            return tcl::S_OK;
          }) == tcl::S_OK);
    CHECK(j.register_command("test", [](tcl::Interp &i,
                                         tcl::vector<tcl::string> &argv,
                                         tcl::ProcPrivdata *privdata) {
            return tcl::S_OK;
          }) == tcl::S_ERR);
    CHECK(j.result.length() > 0);
  }
}

TEST_CASE("tcl - return statement") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("return without value") {
    CHECK(i.eval("proc test {} {return}") == tcl::S_OK);
    CHECK(i.eval("test") == tcl::S_OK);
  }

  SUBCASE("return with value") {
    CHECK(i.eval("proc test {} {return 42}") == tcl::S_OK);
    CHECK(i.eval("test") == tcl::S_OK);
    CHECK(i.result.compare("42") == 0);
  }

  SUBCASE("early return") {
    CHECK(i.eval("proc test {x} {if {> $x 0} {return 1}; return 0}") ==
          tcl::S_OK);
    CHECK(i.eval("test 5") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }
}

TEST_CASE("tcl - complex expressions") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("arithmetic expression") {
    // (5 + 3) * 2 = 16
    CHECK(i.eval("* [+ 5 3] 2") == tcl::S_OK);
    CHECK(i.result.compare("16") == 0);
  }

  SUBCASE("comparison in condition") {
    CHECK(i.eval("set x 10") == tcl::S_OK);
    CHECK(i.eval("if {> $x 5} {set y 1} else {set y 0}") == tcl::S_OK);
    CHECK(i.eval("+ $y 0") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("procedure returning calculation") {
    CHECK(i.eval("proc calc {a b c} {+ [* $a $b] $c}") == tcl::S_OK);
    // 3 * 4 + 5 = 17
    CHECK(i.eval("calc 3 4 5") == tcl::S_OK);
    CHECK(i.result.compare("17") == 0);
  }
}

TEST_CASE("tcl - messagepack commands") {
  tcl::Interp i;
  tcl::register_core_commands(i);
  char mpack_buffer[OT_PAGE_SIZE];
  i.register_mpack_functions(mpack_buffer, OT_PAGE_SIZE);

  SUBCASE("mp/nil") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/nil") == tcl::S_OK);
    CHECK(i.eval("mp/size") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "1") == 0);
    CHECK(i.eval("mp/hex") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "c0 ") == 0);
  }

  SUBCASE("mp/bool") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/bool 1") == tcl::S_OK);
    CHECK(i.eval("mp/hex") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "c3 ") == 0);
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/bool 0") == tcl::S_OK);
    CHECK(i.eval("mp/hex") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "c2 ") == 0);
  }

  SUBCASE("mp/bool - invalid argument") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/bool 2") == tcl::S_ERR);
    CHECK(i.eval("mp/bool true") == tcl::S_ERR);
  }

  SUBCASE("mp/int") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/int 42") == tcl::S_OK);
    CHECK(i.eval("mp/size") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "1") == 0);
    CHECK(i.eval("mp/hex") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "2a ") == 0);
  }

  SUBCASE("mp/int - negative") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/int -5") == tcl::S_OK);
    CHECK(i.eval("mp/hex") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "fb ") == 0);
  }

  SUBCASE("mp/uint") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/uint 255") == tcl::S_OK);
    CHECK(i.eval("mp/size") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "2") == 0);
  }

  SUBCASE("mp/uint - invalid") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/uint -1") == tcl::S_ERR);
    CHECK(i.eval("mp/uint abc") == tcl::S_ERR);
  }

  SUBCASE("mp/string") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/string {hello}") == tcl::S_OK);
    CHECK(i.eval("mp/size") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "6") == 0);
  }

  SUBCASE("mp/string - empty") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/string {}") == tcl::S_OK);
    CHECK(i.eval("mp/size") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "1") == 0);
  }

  SUBCASE("mp/array") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/array 3") == tcl::S_OK);
    CHECK(i.eval("mp/int 1") == tcl::S_OK);
    CHECK(i.eval("mp/int 2") == tcl::S_OK);
    CHECK(i.eval("mp/int 3") == tcl::S_OK);
    CHECK(i.eval("mp/size") == tcl::S_OK);
    // Array header (1 byte) + 3 ints (1 byte each) = 4 bytes
    CHECK(strcmp(i.result.c_str(), "4") == 0);
  }

  SUBCASE("mp/array - negative count") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/array -1") == tcl::S_ERR);
  }

  SUBCASE("mp/map") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/map 2") == tcl::S_OK);
    CHECK(i.eval("mp/string {key1}") == tcl::S_OK);
    CHECK(i.eval("mp/int 1") == tcl::S_OK);
    CHECK(i.eval("mp/string {key2}") == tcl::S_OK);
    CHECK(i.eval("mp/int 2") == tcl::S_OK);
    CHECK(i.eval("mp/size") == tcl::S_OK);
    // Map header + 2 keys (5 bytes each) + 2 values (1 byte each) = 13 bytes
    CHECK(strcmp(i.result.c_str(), "13") == 0);
  }

  SUBCASE("mp/map - negative count") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/map -1") == tcl::S_ERR);
  }

  SUBCASE("mp/reset") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/int 42") == tcl::S_OK);
    CHECK(i.eval("mp/size") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "1") == 0);
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/size") == tcl::S_OK);
    CHECK(strcmp(i.result.c_str(), "0") == 0);
  }

  SUBCASE("complex structure") {
    CHECK(i.eval("mp/reset") == tcl::S_OK);
    CHECK(i.eval("mp/array 3") == tcl::S_OK);
    CHECK(i.eval("mp/string {ipc}") == tcl::S_OK);
    CHECK(i.eval("mp/string {echo}") == tcl::S_OK);
    CHECK(i.eval("mp/string {Hello, world!}") == tcl::S_OK);
    CHECK(i.eval("mp/size") == tcl::S_OK);
    // Array (1) + "ipc" (4) + "echo" (5) + "Hello, world!" (14) = 24
    CHECK(strcmp(i.result.c_str(), "24") == 0);
  }
}

TEST_CASE("tcl - messagepack without buffer") {
  tcl::Interp i;
  tcl::register_core_commands(i);
  // Don't register mpack functions

  SUBCASE("commands fail without buffer") {
    CHECK(i.eval("mp/reset") == tcl::S_ERR);
    CHECK(i.eval("mp/nil") == tcl::S_ERR);
    CHECK(i.eval("mp/int 42") == tcl::S_ERR);
    CHECK(i.eval("mp/string {test}") == tcl::S_ERR);
  }
}

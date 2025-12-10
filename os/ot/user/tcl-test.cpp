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

  SUBCASE("empty string") { CHECK(i.eval("") == tcl::S_OK); }

  SUBCASE("simple command") { CHECK(i.eval("set x 42") == tcl::S_OK); }

  SUBCASE("multiple commands") { CHECK(i.eval("set x 1; set y 2") == tcl::S_OK); }
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
    CHECK(i.eval("while {< $i 10} {set i [+ $i 1]; if {== $i 3} {break}}") == tcl::S_OK);
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
    CHECK(i.eval("proc test {x} {if {> $x 5} {return 1}; return 0}") == tcl::S_OK);
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
    CHECK(j.register_command("test", [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
      return tcl::S_OK;
    }) == tcl::S_OK);
    CHECK(j.register_command("test", [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
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
    CHECK(i.eval("proc test {x} {if {> $x 0} {return 1}; return 0}") == tcl::S_OK);
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

TEST_CASE("tcl - list operations") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("list - create empty list") {
    CHECK(i.eval("list") == tcl::S_OK);
    CHECK(i.result.length() == 0);
  }

  SUBCASE("list - create list with single element") {
    CHECK(i.eval("list hello") == tcl::S_OK);
    CHECK(i.result.compare("hello") == 0);
  }

  SUBCASE("list - create list with multiple elements") {
    CHECK(i.eval("list a b c d") == tcl::S_OK);
    CHECK(i.result.compare("a b c d") == 0);
  }

  SUBCASE("list - elements with spaces get braced") {
    CHECK(i.eval("list hello {world test} foo") == tcl::S_OK);
    CHECK(i.result.compare("hello {world test} foo") == 0);
  }

  SUBCASE("llength - empty list") {
    CHECK(i.eval("llength {}") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("llength - single element") {
    CHECK(i.eval("llength {hello}") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("llength - multiple elements") {
    CHECK(i.eval("llength {a b c d}") == tcl::S_OK);
    CHECK(i.result.compare("4") == 0);
  }

  SUBCASE("llength - with braced elements") {
    CHECK(i.eval("llength {hello {world test} foo}") == tcl::S_OK);
    CHECK(i.result.compare("3") == 0);
  }

  SUBCASE("lindex - get first element") {
    CHECK(i.eval("lindex {a b c} 0") == tcl::S_OK);
    CHECK(i.result.compare("a") == 0);
  }

  SUBCASE("lindex - get middle element") {
    CHECK(i.eval("lindex {a b c d} 2") == tcl::S_OK);
    CHECK(i.result.compare("c") == 0);
  }

  SUBCASE("lindex - get last element") {
    CHECK(i.eval("lindex {a b c} 2") == tcl::S_OK);
    CHECK(i.result.compare("c") == 0);
  }

  SUBCASE("lindex - out of bounds returns empty") {
    CHECK(i.eval("lindex {a b c} 5") == tcl::S_OK);
    CHECK(i.result.length() == 0);
  }

  SUBCASE("lindex - negative index returns empty") {
    CHECK(i.eval("lindex {a b c} -1") == tcl::S_OK);
    CHECK(i.result.length() == 0);
  }

  SUBCASE("lindex - braced element") {
    CHECK(i.eval("lindex {hello {world test} foo} 1") == tcl::S_OK);
    CHECK(i.result.compare("world test") == 0);
  }

  SUBCASE("lappend - to new variable") {
    CHECK(i.eval("lappend mylist a") == tcl::S_OK);
    CHECK(i.result.compare("a") == 0);
  }

  SUBCASE("lappend - to existing list") {
    CHECK(i.eval("set mylist {a b}") == tcl::S_OK);
    CHECK(i.eval("lappend mylist c d") == tcl::S_OK);
    CHECK(i.result.compare("a b c d") == 0);
    CHECK(i.eval("+ [llength $mylist] 0") == tcl::S_OK);
    CHECK(i.result.compare("4") == 0);
  }

  SUBCASE("lappend - updates variable") {
    CHECK(i.eval("set mylist {x y}") == tcl::S_OK);
    CHECK(i.eval("lappend mylist z") == tcl::S_OK);
    CHECK(i.eval("+ [llength $mylist] 0") == tcl::S_OK);
    CHECK(i.result.compare("3") == 0);
  }

  SUBCASE("lrange - entire list") {
    CHECK(i.eval("lrange {a b c d} 0 3") == tcl::S_OK);
    CHECK(i.result.compare("a b c d") == 0);
  }

  SUBCASE("lrange - middle portion") {
    CHECK(i.eval("lrange {a b c d e} 1 3") == tcl::S_OK);
    CHECK(i.result.compare("b c d") == 0);
  }

  SUBCASE("lrange - single element") {
    CHECK(i.eval("lrange {a b c d} 2 2") == tcl::S_OK);
    CHECK(i.result.compare("c") == 0);
  }

  SUBCASE("lrange - end beyond list") {
    CHECK(i.eval("lrange {a b c} 1 10") == tcl::S_OK);
    CHECK(i.result.compare("b c") == 0);
  }

  SUBCASE("lrange - negative start becomes 0") {
    CHECK(i.eval("lrange {a b c d} -5 2") == tcl::S_OK);
    CHECK(i.result.compare("a b c") == 0);
  }

  SUBCASE("split - default delimiter (space)") {
    CHECK(i.eval("split {hello world test}") == tcl::S_OK);
    CHECK(i.result.compare("hello world test") == 0);
  }

  SUBCASE("split - custom delimiter") {
    CHECK(i.eval("split {hello-world-test} -") == tcl::S_OK);
    CHECK(i.result.compare("hello world test") == 0);
  }

  SUBCASE("split - comma delimiter") {
    CHECK(i.eval("split {a,b,c,d} ,") == tcl::S_OK);
    CHECK(i.result.compare("a b c d") == 0);
  }

  SUBCASE("split - colon delimiter") {
    CHECK(i.eval("split {foo:bar:baz} :") == tcl::S_OK);
    CHECK(i.result.compare("foo bar baz") == 0);
  }

  SUBCASE("split - empty parts") {
    CHECK(i.eval("split {a::b} :") == tcl::S_OK);
    CHECK(i.result.compare("a {} b") == 0);
  }

  SUBCASE("split - delimiter error") { CHECK(i.eval("split {test} abc") == tcl::S_ERR); }

  SUBCASE("join - default separator (space)") {
    CHECK(i.eval("join {a b c}") == tcl::S_OK);
    CHECK(i.result.compare("a b c") == 0);
  }

  SUBCASE("join - custom separator") {
    CHECK(i.eval("join {hello world test} -") == tcl::S_OK);
    CHECK(i.result.compare("hello-world-test") == 0);
  }

  SUBCASE("join - comma separator") {
    CHECK(i.eval("join {a b c d} ,") == tcl::S_OK);
    CHECK(i.result.compare("a,b,c,d") == 0);
  }

  SUBCASE("join - empty separator") {
    CHECK(i.eval("join {h e l l o} {}") == tcl::S_OK);
    CHECK(i.result.compare("hello") == 0);
  }

  SUBCASE("join - multi-char separator") {
    CHECK(i.eval("join {foo bar baz} { :: }") == tcl::S_OK);
    CHECK(i.result.compare("foo :: bar :: baz") == 0);
  }

  SUBCASE("split and join roundtrip") {
    CHECK(i.eval("set orig {hello-world-test}") == tcl::S_OK);
    CHECK(i.eval("set parts [split $orig -]") == tcl::S_OK);
    CHECK(i.eval("join $parts -") == tcl::S_OK);
    CHECK(i.result.compare("hello-world-test") == 0);
  }

  SUBCASE("complex list operations") {
    CHECK(i.eval("set mylist [list a b c]") == tcl::S_OK);
    CHECK(i.eval("lappend mylist d e") == tcl::S_OK);
    CHECK(i.eval("set sublist [lrange $mylist 1 3]") == tcl::S_OK);
    CHECK(i.eval("llength $sublist") == tcl::S_OK);
    CHECK(i.result.compare("3") == 0);
    CHECK(i.eval("lindex $sublist 1") == tcl::S_OK);
    CHECK(i.result.compare("c") == 0);
  }
}

TEST_CASE("tcl - number conversion - hex") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("hex - simple lowercase") {
    CHECK(i.eval("hex ff") == tcl::S_OK);
    CHECK(i.result.compare("255") == 0);
  }

  SUBCASE("hex - simple uppercase") {
    CHECK(i.eval("hex FF") == tcl::S_OK);
    CHECK(i.result.compare("255") == 0);
  }

  SUBCASE("hex - with 0x prefix lowercase") {
    CHECK(i.eval("hex 0xff") == tcl::S_OK);
    CHECK(i.result.compare("255") == 0);
  }

  SUBCASE("hex - with 0x prefix uppercase") {
    CHECK(i.eval("hex 0xFF") == tcl::S_OK);
    CHECK(i.result.compare("255") == 0);
  }

  SUBCASE("hex - with 0X prefix") {
    CHECK(i.eval("hex 0XFF") == tcl::S_OK);
    CHECK(i.result.compare("255") == 0);
  }

  SUBCASE("hex - zero") {
    CHECK(i.eval("hex 0") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("hex - single digit") {
    CHECK(i.eval("hex a") == tcl::S_OK);
    CHECK(i.result.compare("10") == 0);
  }

  SUBCASE("hex - large value") {
    CHECK(i.eval("hex 1a2b") == tcl::S_OK);
    CHECK(i.result.compare("6699") == 0);
  }

  SUBCASE("hex - mixed case") {
    CHECK(i.eval("hex AbCd") == tcl::S_OK);
    CHECK(i.result.compare("43981") == 0);
  }

  SUBCASE("hex - all digits") {
    CHECK(i.eval("hex 123") == tcl::S_OK);
    CHECK(i.result.compare("291") == 0);
  }

  SUBCASE("hex - invalid character") {
    CHECK(i.eval("hex 1g2") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("hex - empty string") {
    CHECK(i.eval("hex {}") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("hex - use in arithmetic") {
    CHECK(i.eval("+ [hex ff] 1") == tcl::S_OK);
    CHECK(i.result.compare("256") == 0);
  }
}

TEST_CASE("tcl - number conversion - oct") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("oct - simple") {
    CHECK(i.eval("oct 77") == tcl::S_OK);
    CHECK(i.result.compare("63") == 0);
  }

  SUBCASE("oct - with 0o prefix lowercase") {
    CHECK(i.eval("oct 0o77") == tcl::S_OK);
    CHECK(i.result.compare("63") == 0);
  }

  SUBCASE("oct - with 0o prefix uppercase") {
    CHECK(i.eval("oct 0O77") == tcl::S_OK);
    CHECK(i.result.compare("63") == 0);
  }

  SUBCASE("oct - zero") {
    CHECK(i.eval("oct 0") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("oct - single digit") {
    CHECK(i.eval("oct 7") == tcl::S_OK);
    CHECK(i.result.compare("7") == 0);
  }

  SUBCASE("oct - larger value") {
    CHECK(i.eval("oct 755") == tcl::S_OK);
    CHECK(i.result.compare("493") == 0);
  }

  SUBCASE("oct - all zeros") {
    CHECK(i.eval("oct 000") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("oct - invalid character 8") {
    CHECK(i.eval("oct 78") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("oct - invalid character 9") {
    CHECK(i.eval("oct 79") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("oct - empty string") {
    CHECK(i.eval("oct {}") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("oct - use in arithmetic") {
    CHECK(i.eval("+ [oct 10] 2") == tcl::S_OK);
    CHECK(i.result.compare("10") == 0);
  }
}

TEST_CASE("tcl - number conversion - bin") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("bin - simple") {
    CHECK(i.eval("bin 1111") == tcl::S_OK);
    CHECK(i.result.compare("15") == 0);
  }

  SUBCASE("bin - with 0b prefix lowercase") {
    CHECK(i.eval("bin 0b1111") == tcl::S_OK);
    CHECK(i.result.compare("15") == 0);
  }

  SUBCASE("bin - with 0b prefix uppercase") {
    CHECK(i.eval("bin 0B1111") == tcl::S_OK);
    CHECK(i.result.compare("15") == 0);
  }

  SUBCASE("bin - zero") {
    CHECK(i.eval("bin 0") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("bin - single digit 0") {
    CHECK(i.eval("bin 0") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("bin - single digit 1") {
    CHECK(i.eval("bin 1") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("bin - byte value") {
    CHECK(i.eval("bin 11111111") == tcl::S_OK);
    CHECK(i.result.compare("255") == 0);
  }

  SUBCASE("bin - mixed pattern") {
    CHECK(i.eval("bin 10101010") == tcl::S_OK);
    CHECK(i.result.compare("170") == 0);
  }

  SUBCASE("bin - all zeros") {
    CHECK(i.eval("bin 0000") == tcl::S_OK);
    CHECK(i.result.compare("0") == 0);
  }

  SUBCASE("bin - power of 2") {
    CHECK(i.eval("bin 10000") == tcl::S_OK);
    CHECK(i.result.compare("16") == 0);
  }

  SUBCASE("bin - invalid character 2") {
    CHECK(i.eval("bin 102") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("bin - invalid character a") {
    CHECK(i.eval("bin 1a1") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("bin - empty string") {
    CHECK(i.eval("bin {}") == tcl::S_ERR);
    CHECK(i.result.length() > 0);
  }

  SUBCASE("bin - use in arithmetic") {
    CHECK(i.eval("+ [bin 1010] [bin 0101]") == tcl::S_OK);
    CHECK(i.result.compare("15") == 0);
  }
}

TEST_CASE("tcl - number conversion - mixed bases") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("mix hex and dec in arithmetic") {
    CHECK(i.eval("+ [hex 10] 16") == tcl::S_OK);
    CHECK(i.result.compare("32") == 0);
  }

  SUBCASE("mix oct and dec in arithmetic") {
    CHECK(i.eval("+ [oct 10] 8") == tcl::S_OK);
    CHECK(i.result.compare("16") == 0);
  }

  SUBCASE("mix bin and dec in arithmetic") {
    CHECK(i.eval("+ [bin 10] 2") == tcl::S_OK);
    CHECK(i.result.compare("4") == 0);
  }

  SUBCASE("all three bases together") {
    CHECK(i.eval("+ [hex 10] [+ [oct 10] [bin 10]]") == tcl::S_OK);
    CHECK(i.result.compare("26") == 0);
  }

  SUBCASE("compare hex values") {
    CHECK(i.eval("== [hex ff] 255") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("compare oct values") {
    CHECK(i.eval("== [oct 100] 64") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }

  SUBCASE("compare bin values") {
    CHECK(i.eval("== [bin 1000] 8") == tcl::S_OK);
    CHECK(i.result.compare("1") == 0);
  }
}

TEST_CASE("tcl - escape sequences in quoted strings") {
  tcl::Interp i;
  tcl::register_core_commands(i);

  SUBCASE("escaped quote") {
    CHECK(i.eval("set x \"hello \\\"world\\\"\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("hello \"world\"") == 0);
  }

  SUBCASE("escaped backslash") {
    CHECK(i.eval("set x \"path\\\\to\\\\file\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("path\\to\\file") == 0);
  }

  SUBCASE("newline escape") {
    CHECK(i.eval("set x \"line1\\nline2\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("line1\nline2") == 0);
  }

  SUBCASE("tab escape") {
    CHECK(i.eval("set x \"col1\\tcol2\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("col1\tcol2") == 0);
  }

  SUBCASE("carriage return escape") {
    CHECK(i.eval("set x \"text\\rmore\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("text\rmore") == 0);
  }

  SUBCASE("mixed escapes") {
    CHECK(i.eval("set x \"say \\\"hi\\\\there\\\"\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("say \"hi\\there\"") == 0);
  }

  SUBCASE("escape at start and end") {
    CHECK(i.eval("set x \"\\\"quoted\\\"\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("\"quoted\"") == 0);
  }

  SUBCASE("unknown escape passes through") {
    CHECK(i.eval("set x \"test\\xvalue\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("test\\xvalue") == 0);
  }

  SUBCASE("no escapes - unchanged behavior") {
    CHECK(i.eval("set x \"hello world\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("hello world") == 0);
  }

  SUBCASE("braced strings unchanged") {
    CHECK(i.eval("set x {no \\\"escape\\\" here}") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("no \\\"escape\\\" here") == 0);
  }

  SUBCASE("empty string with escapes") {
    CHECK(i.eval("set x \"\\\"\\\"\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("\"\"") == 0);
  }

  SUBCASE("multiple newlines") {
    CHECK(i.eval("set x \"a\\nb\\nc\"") == tcl::S_OK);
    tcl::Var *v = i.get_var(tcl::string_view("x"));
    CHECK(v != nullptr);
    CHECK(v->val->compare("a\nb\nc") == 0);
  }
}

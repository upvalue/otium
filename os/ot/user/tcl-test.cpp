// tcl-test.cpp - unit tests for TCL interpreter
#include "ot/common.h"
#include "ot/user/tcl.h"
#include "vendor/doctest.h"

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

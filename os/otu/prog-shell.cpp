// prog-shell.cpp - shell program
#include "otu/user.hpp"

extern "C" void main(void) {
  // it borken
  ou_putchar('C');
  ou_putchar('D');
  ou_exit();
}
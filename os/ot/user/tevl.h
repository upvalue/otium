// tevl.h - TEVL text editor interface
#ifndef OT_USER_TEVL_H
#define OT_USER_TEVL_H

struct Backend {
  // check for keyboard input. does not block
  virtual Result<char, EditorErr> readKey();
};

void tevl_main();

#endif

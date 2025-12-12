# gfxdemomini.tcl
# minimal graphical demo

set uishell_output_to_console 1

gfx/loop 60 {
  gfx/rect [hex 0000ffff] 0 0 1024 670
  gfx/loop-iter
}

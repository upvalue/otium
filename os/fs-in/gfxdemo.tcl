# gfxdemo.tcl - an example graphical demo
gfx/init

set width 1024
set height 670

puts "gfxdemo starting"

proc main {} {
  # draw a blue background
  gfx/rect 0000ff 0 0 $width $height
  gfx/draw-text 5 5 12 "Wut" 
}

# try for 60 fps 
gfx/loop main 60

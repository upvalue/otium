# mandelbrot.tcl - Render Mandelbrot set using fixed-point arithmetic
# Scale factor: 256 (8-bit fixed point)

set uishell_output_to_console 1

set MAX_ITER 32
set WIDTH 128
set HEIGHT 100

# Mandelbrot bounds in fixed-point (scaled by 256):
# Real: -2.0 to 2.0  => -512 to 512 (doubled X_MAX)
# Imag: -1.0 to 1.0  => -256 to 256
set X_MIN -512
set X_MAX 512
set Y_MIN -256
set Y_MAX 256

# Fixed-point multiply: (a * b) >> 8  (divide by 256)
proc fpmul {a b} {
  return [>> [* $a $b] 8]
}

# Color palette - map iteration to BGRA color
proc get_color {iter max_iter} {
  if {== $iter $max_iter} {
    return [hex ff000000]
  }
  # Simple color gradient based on iteration
  set t [% $iter 16]
  set r [* $t 16]
  set g [* [% $iter 8] 32]
  set b [- 255 [* $t 8]]
  # Pack as 0xAABBGGRR
  return [+ [hex ff000000] [+ [<< $b 16] [+ [<< $g 8] $r]]]
}

proc draw_mandelbrot {} {
  # Calculate centering offset (assuming 640x480 framebuffer)
  set FB_WIDTH 640
  set FB_HEIGHT 480
  set OFFSET_X [/ [- $FB_WIDTH $WIDTH] 2]
  set OFFSET_Y [/ [- $FB_HEIGHT $HEIGHT] 2]

  ot_yield
  set x_range [- $X_MAX $X_MIN]
  set y_range [- $Y_MAX $Y_MIN]

  # Show initial status message
  gfx/text "Rendering Mandelbrot..." 10 10 [hex ffffffff]
  gfx/flush

  set py 0
  while {< $py $HEIGHT} {
    ot_yield
    # Map py to imaginary coordinate (fixed-point)
    set c_imag [+ $Y_MIN [/ [* $py $y_range] $HEIGHT]]

    set px 0
    while {< $px $WIDTH} {
      # Map px to real coordinate (fixed-point)
      set c_real [+ $X_MIN [/ [* $px $x_range] $WIDTH]]

      # z = 0 + 0i
      set zr 0
      set zi 0
      set iter 0

      # Iterate: z = z^2 + c until escape or max iterations
      while {< $iter $MAX_ITER} {
        set zr_sq [fpmul $zr $zr]
        set zi_sq [fpmul $zi $zi]

        # Escape: |z|^2 > 4 => (zr^2 + zi^2) > 4*256 = 1024
        if {> [+ $zr_sq $zi_sq] 1024} {
          break
        }

        # z_new = zr^2 - zi^2 + cr, 2*zr*zi + ci
        set zi_new [+ [fpmul [<< $zr 1] $zi] $c_imag]
        set zr [+ [- $zr_sq $zi_sq] $c_real]
        set zi $zi_new

        set iter [+ $iter 1]
      }

      # Draw pixel centered on screen
      gfx/pixel [get_color $iter $MAX_ITER] [+ $px $OFFSET_X] [+ $py $OFFSET_Y]
      set px [+ $px 1]
    }

    # Flush after each line so user sees progressive rendering
    gfx/flush
    set py [+ $py 1]
  }

  # Show completion message
  gfx/text "Rendering complete!" 10 10 [hex ffffffff]
  gfx/flush
}

# Render once, then keep looping to maintain the display
set rendered 0
gfx/loop 60 {
  if {== $rendered 0} {
    # Clear to black background
    gfx/rect [hex ff000000] 0 0 640 480
    draw_mandelbrot
    set rendered 1
  }
  gfx/loop-iter
}

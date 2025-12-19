# mandelbrot.tcl - Render Mandelbrot set using fixed-point arithmetic
# Scale factor: 256 (8-bit fixed point)

set uishell_output_to_console 1

puts "testing output"

set MAX_ITER 32
set WIDTH 128
set HEIGHT 100

# Mandelbrot bounds in fixed-point (scaled by 256):
# Real: -2.0 to 1.0  => -512 to 256
# Imag: -1.0 to 1.0  => -256 to 256
set X_MIN -512
set X_MAX 256
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
  set x_range [- $X_MAX $X_MIN]
  set y_range [- $Y_MAX $Y_MIN]

  set py 0
  while {< $py $HEIGHT} {
    puts "entering py loop"
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

      gfx/pixel [get_color $iter $MAX_ITER] $px $py
      set px [+ $px 1]
    }
    set py [+ $py 1]
  }
}

puts "hi"

# Render once
gfx/loop 60 {
  puts "entering draw_mandelbrot"
  draw_mandelbrot
  puts "ending draw_mandelbrot"
  gfx/loop-iter
  break
}

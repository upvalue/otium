# Mandelbrot benchmark - compute without graphics
# This exercises: arithmetic, loops, procedures, variables

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

proc compute_mandelbrot {} {
  set x_range [- $X_MAX $X_MIN]
  set y_range [- $Y_MAX $Y_MIN]
  set total_iters 0

  set py 0
  while {< $py $HEIGHT} {
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

      # Accumulate iterations (just to ensure computation isn't optimized away)
      set total_iters [+ $total_iters $iter]
      set px [+ $px 1]
    }
    set py [+ $py 1]
  }

  return $total_iters
}

set result [compute_mandelbrot]
puts $result

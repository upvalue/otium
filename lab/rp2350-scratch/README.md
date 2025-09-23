# rp2350-scratch

RP2350 bare metal demo -- this is largely derived from the
[rp-hal](https://github.com/rp-rs/rp-hal) repository.

For running, Rust setup:

```
rustup self update
rustup update stable
rustup target add thumbv6m-none-eabi
rustup target add thumbv8m.main-none-eabihf
```

You'll need picotool

> brew install picotool

works on OSX

Then to build and flash

> picotool load -u -v -x -t elf target/thumbv8m.main-none-eabihf/debug/rp2350-scratch

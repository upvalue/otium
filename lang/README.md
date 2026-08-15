# otium language

experimental little programming language

## Formatting

Run `tools/format-cpp` to format the C++ sources. Run
`tools/format-cpp --check` to check them without making changes.

The script looks for `clang-format` on `PATH` and through Xcode's `xcrun`.
Set `CLANG_FORMAT` to use a specific binary.

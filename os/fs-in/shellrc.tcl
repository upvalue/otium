# Shell startup script
# This file is compiled into the shell and executed at startup

puts "shellrc loaded"

# on startup
if {! $features_ui} {
  run uishell
}

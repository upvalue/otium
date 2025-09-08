use std::path::Path;

fn find_llvm_tool(tool: &str) -> Option<String> {
  // Check environment variable first
  if let Ok(path) = std::env::var(&format!("LLVM_{}", tool.to_uppercase())) {
    return Some(path);
  }

  // Common LLVM installation paths
  let common_paths = [
    "/opt/homebrew/opt/llvm/bin", // Homebrew on macOS
    "/usr/bin",                   // System package manager
    "/usr/local/bin",             // Manual installation
  ];

  for base_path in &common_paths {
    let tool_path = format!("{}/{}", base_path, tool);
    if Path::new(&tool_path).exists() {
      return Some(tool_path);
    }
  }

  // Fallback to system PATH
  Some(tool.to_string())
}

fn compile_c_support(arch: &str, target: &str) {
  let clang = find_llvm_tool("clang").unwrap_or_else(|| "clang".to_string());
  let ar = find_llvm_tool("llvm-ar").unwrap_or_else(|| "ar".to_string());

  println!(
    "cargo:warning=Using LLVM tools for {}: clang={}, ar={}",
    arch, clang, ar
  );

  let mut build = cc::Build::new();
  build
    .file(&format!("src/{}/support.c", arch))
    .target(target)
    .compiler(&clang)
    .archiver(&ar)
    .flag("-nostdlib")
    .flag("-ffreestanding")
    .flag("-fno-builtin")
    .opt_level(2);

  // Architecture-specific flags
  if arch == "riscv32" {
    build
      .flag("--target=riscv32")
      .flag("-march=rv32imac")
      .flag("-mabi=ilp32");
  } else if arch == "wasm" {
    build.flag("--target=wasm32");
  }

  build.compile(&format!("support_{}", arch));

  println!("cargo:rerun-if-changed=src/{}/support.c", arch);
}

fn main() {
  let target = std::env::var("TARGET").unwrap_or_default();

  if target.contains("riscv32") {
    compile_c_support("riscv32", &target);
  }

  if target.contains("wasm32") {
    compile_c_support("wasm", &target);
  }
}

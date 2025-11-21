import { Eta } from "eta";
import { Arg, ErrorCodeDef, IntAlias, Method, ParsedIDL, Return, Service } from "./parser.ts";

const eta = new Eta();

// Global int aliases map (populated during generation)
let intAliasMap: Map<string, IntAlias> = new Map();

interface TemplateContext {
  service?: Service;
  services?: Service[];
  errorCodes?: ErrorCodeDef[];
  intAliases?: IntAlias[];
  // Helper functions
  toPascalCase: (str: string) => string;
  toUpperSnake: (str: string) => string;
  toKebabCase: (str: string) => string;
  toHex: (num: number) => string;
  getType: (arg: Arg | Return) => string;
  getServerType: (arg: Arg | Return) => string;
  getReturnType: (method: Method) => string;
  formatArgs: (args: Arg[]) => string;
  formatServerArgs: (args: Arg[]) => string;
  formatIpcArgs: (args: Arg[]) => string;
  extractMsgArgs: (method: Method) => string;
  isComplexType: (arg: Arg | Return) => boolean;
  hasComplexArgs: (method: Method) => boolean;
  usesIntAliases: (service: Service) => boolean;
}

function toPascalCase(str: string): string {
  return str
    .split("_")
    .map((word) => word.charAt(0).toUpperCase() + word.slice(1).toLowerCase())
    .join("");
}

function toUpperSnake(str: string): string {
  return str
    .replace(/([a-z])([A-Z])/g, "$1_$2")
    .replace(/([A-Z])([A-Z][a-z])/g, "$1_$2")
    .toUpperCase();
}

function toKebabCase(str: string): string {
  return str
    .replace(/__/g, "-")
    .replace(/_/g, "-")
    .toLowerCase();
}

function toHex(num: number): string {
  return `0x${num.toString(16)}`;
}

function getType(arg: Arg | Return): string {
  const type = arg.type || "int";
  if (type === "string") return "const ou::string&";
  if (type === "buffer") return "const ou::vector<uint8_t>&";
  if (type === "uint") return "uintptr_t";

  // Check if this is an int alias
  if (intAliasMap.has(type)) {
    return type;
  }

  // Default: int or check signed flag
  return arg.signed ?? true ? "intptr_t" : "uintptr_t";
}

// Server-side type for parameters - uses StringView for buffers (zero-copy)
function getServerType(arg: Arg | Return): string {
  const type = arg.type || "int";
  if (type === "string") return "const ou::string&";
  if (type === "buffer") return "const StringView&";  // Zero-copy from comm page
  if (type === "uint") return "uintptr_t";

  // Check if this is an int alias
  if (intAliasMap.has(type)) {
    return type;
  }

  // Default: int or check signed flag
  return arg.signed ?? true ? "intptr_t" : "uintptr_t";
}

function isComplexType(arg: Arg | Return): boolean {
  const type = arg.type || "int";
  return type === "string" || type === "buffer";
}

function hasComplexArgs(method: Method): boolean {
  return method.args.some(isComplexType);
}

function getReturnType(method: Method): string {
  if (method.returns.length === 0) {
    return "bool"; // For void methods, return bool (always true on success)
  } else if (method.returns.length === 1) {
    return getType(method.returns[0]);
  } else {
    return `${toPascalCase(method.name)}Result`;
  }
}

function formatArgs(args: Arg[]): string {
  if (args.length === 0) return "";
  return args.map((arg) => `${getType(arg)} ${arg.name}`).join(", ");
}

// Server-side argument formatting - uses StringView for buffers
function formatServerArgs(args: Arg[]): string {
  if (args.length === 0) return "";
  return args.map((arg) => `${getServerType(arg)} ${arg.name}`).join(", ");
}

function formatIpcArgs(args: Arg[]): string {
  // Only include non-complex (int/uint) args for IPC inline args
  const simpleArgs = args.filter(arg => !isComplexType(arg));
  const argNames = simpleArgs.map((arg) => {
    const type = arg.type || "int";
    // If it's an int alias, extract raw value
    if (intAliasMap.has(type)) {
      return `${arg.name}.raw()`;
    }
    return arg.name;
  });
  // Pad to 3 arguments
  while (argNames.length < 3) {
    argNames.push("0");
  }
  return argNames.slice(0, 3).join(", ");
}

function extractMsgArgs(method: Method): string {
  if (method.args.length === 0) return "";
  return method.args
    .map((_, index) => `msg.args[${index}]`)
    .join(", ");
}

function usesIntAliases(service: Service): boolean {
  for (const method of service.methods) {
    // Check args
    for (const arg of method.args) {
      const type = arg.type || "int";
      if (intAliasMap.has(type)) {
        return true;
      }
    }
    // Check returns
    for (const ret of method.returns) {
      const type = ret.type || "int";
      if (intAliasMap.has(type)) {
        return true;
      }
    }
  }
  return false;
}

function createContext(data: Partial<TemplateContext>): TemplateContext {
  return {
    ...data,
    toPascalCase,
    toUpperSnake,
    toKebabCase,
    toHex,
    getType,
    getServerType,
    getReturnType,
    formatArgs,
    formatServerArgs,
    formatIpcArgs,
    extractMsgArgs,
    isComplexType,
    hasComplexArgs,
    usesIntAliases,
  };
}

async function renderTemplate(
  templatePath: string,
  context: TemplateContext,
): Promise<string> {
  const template = await Deno.readTextFile(templatePath);
  return eta.renderString(template, context) as string;
}

async function ensureDir(path: string) {
  try {
    await Deno.mkdir(path, { recursive: true });
  } catch (error) {
    if (!(error instanceof Deno.errors.AlreadyExists)) {
      throw error;
    }
  }
}

async function writeIfChanged(filePath: string, content: string) {
  let existing = "";
  try {
    existing = await Deno.readTextFile(filePath);
  } catch {
    // File doesn't exist, that's fine
  }

  if (existing !== content) {
    await Deno.writeTextFile(filePath, content);
    console.log(`  Generated: ${filePath}`);
  } else {
    console.log(`  Unchanged: ${filePath}`);
  }
}

async function generateService(
  service: Service,
  outputDir: string,
  templateDir: string,
) {
  const serviceLower = service.name.toLowerCase();
  const context = createContext({ service });

  // Generate types header
  const typesHeader = await renderTemplate(
    `${templateDir}/types-header.eta`,
    context,
  );
  await writeIfChanged(`${outputDir}/${serviceLower}-types.hpp`, typesHeader);

  // Generate client files
  const clientHeader = await renderTemplate(
    `${templateDir}/client-header.eta`,
    context,
  );
  await writeIfChanged(`${outputDir}/${serviceLower}-client.hpp`, clientHeader);

  const clientImpl = await renderTemplate(
    `${templateDir}/client-impl.eta`,
    context,
  );
  await writeIfChanged(`${outputDir}/${serviceLower}-client.cpp`, clientImpl);

  // Generate server files
  const serverHeader = await renderTemplate(
    `${templateDir}/server-header.eta`,
    context,
  );
  await writeIfChanged(`${outputDir}/${serviceLower}-server.hpp`, serverHeader);

  const serverImpl = await renderTemplate(
    `${templateDir}/server-impl.eta`,
    context,
  );
  await writeIfChanged(`${outputDir}/${serviceLower}-server.cpp`, serverImpl);
}

async function generateGlobalFiles(
  idl: ParsedIDL,
  outputDir: string,
  templateDir: string,
) {
  const context = createContext({
    services: idl.services,
    errorCodes: idl.errorCodes,
  });

  // Generate method IDs
  const methodIds = await renderTemplate(
    `${templateDir}/method-ids.eta`,
    context,
  );
  await writeIfChanged(`${outputDir}/method-ids.hpp`, methodIds);

  // Generate error codes enum values
  const errorCodesEnum = await renderTemplate(
    `${templateDir}/error-codes.eta`,
    context,
  );
  await writeIfChanged(`${outputDir}/error-codes-gen.hpp`, errorCodesEnum);

  // Generate error codes switch cases
  const errorCodesSwitch = await renderTemplate(
    `${templateDir}/error-codes-switch.eta`,
    context,
  );
  await writeIfChanged(
    `${outputDir}/error-codes-gen-switch.hpp`,
    errorCodesSwitch,
  );

  // Generate TCL variable registration
  const tclVars = await renderTemplate(
    `${templateDir}/tcl-vars.eta`,
    context,
  );
  await writeIfChanged(`${outputDir}/tcl-vars.hpp`, tclVars);
}

async function createStubImplementation(service: Service, implDir: string) {
  const implPath = `${implDir}/impl.cpp`;

  // Check if file already exists
  try {
    await Deno.stat(implPath);
    console.log(`  Skipped (exists): ${implPath}`);
    return;
  } catch {
    // File doesn't exist, create it
  }

  const serviceLower = service.name.toLowerCase();
  const lines = [
    `#include "ot/user/gen/${serviceLower}-server.hpp"`,
    "",
  ];

  for (const method of service.methods) {
    const returnType = getReturnType(method);
    const args = formatArgs(method.args);

    lines.push(
      `Result<${returnType}, ErrorCode> ${service.name}Server::handle_${method.name}(${args}) {`,
    );
    lines.push("  // TODO: Implement");

    if (method.returns.length === 0) {
      lines.push(`  return Result<${returnType}, ErrorCode>::ok(true);`);
    } else if (method.returns.length === 1) {
      lines.push(`  return Result<${returnType}, ErrorCode>::ok(0);`);
    } else {
      lines.push(`  ${returnType} result = {};`);
      lines.push(`  return Result<${returnType}, ErrorCode>::ok(result);`);
    }

    lines.push("}");
    lines.push("");
  }

  // Add process entry point
  lines.push(`void proc_${serviceLower}(void) {`);
  lines.push(`  ${service.name}Server server;`);
  lines.push(`  server.run();`);
  lines.push("}");
  lines.push("");

  await Deno.writeTextFile(implPath, lines.join("\n"));
  console.log(`  Created: ${implPath}`);
}

export async function generate(
  idl: ParsedIDL,
  outputDir: string,
  templateDir: string,
  projectRoot: string,
) {
  console.log("\nGenerating IPC code...");

  // Initialize int alias map
  intAliasMap = new Map();
  for (const alias of idl.intAliases) {
    intAliasMap.set(alias.name, alias);
  }

  // Ensure output directory exists
  await ensureDir(outputDir);

  // Generate per-service files
  for (const service of idl.services) {
    console.log(`\nService: ${service.name}`);
    await generateService(service, outputDir, templateDir);

    // Create implementation directory and stub file
    const implDir = `${projectRoot}/ot/user/${service.name.toLowerCase()}`;
    await ensureDir(implDir);
    await createStubImplementation(service, implDir);
  }

  // Generate global files
  console.log("\nGlobal files:");
  await generateGlobalFiles(idl, outputDir, templateDir);

  console.log("\nCode generation complete!");
}

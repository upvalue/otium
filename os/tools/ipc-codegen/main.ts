import { parseIDL } from "./parser.ts";
import { generate } from "./generator.ts";
import { parse } from "https://deno.land/std@0.208.0/flags/mod.ts";

async function main() {
  const flags = parse(Deno.args, {
    string: ["input", "output"],
    default: {
      input: "services.yaml",
      output: "ot/user/gen",
    },
  });

  const inputFile = flags.input;
  const outputDir = flags.output;

  console.log(`Reading IDL from: ${inputFile}`);

  try {
    const idl = await parseIDL(inputFile);

    console.log(`\nParsed ${idl.services.length} service(s):`);
    for (const service of idl.services) {
      console.log(`  - ${service.name} (${service.methods.length} methods)`);
    }
    console.log(`\nGenerated ${idl.errorCodes.length} error code(s)`);

    // Determine template directory (relative to this script)
    const scriptDir = new URL(".", import.meta.url).pathname;
    const templateDir = `${scriptDir}templates`;

    // Determine project root (2 levels up from tools/ipc-codegen)
    const projectRoot = `${scriptDir}../..`;

    await generate(idl, outputDir, templateDir, projectRoot);
  } catch (error) {
    console.error("Error:", error.message);
    Deno.exit(1);
  }
}

if (import.meta.main) {
  main();
}

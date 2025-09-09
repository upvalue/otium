export function runWithFile(
  callback: (content: string, filename: string) => void
): void {
  const args = process.argv.slice(2);
  if (args.length !== 1) {
    console.error("Usage: bun <script> <file>");
    process.exit(1);
  }

  const filename = args[0];

  try {
    const fs = require("fs");
    const content = fs.readFileSync(filename, "utf8") as string;
    try {
      callback(content, filename!);
    } catch (error) {
      console.error(`Error while operating on file: ${error}`);
    }
  } catch (error) {
    console.error(`Error reading file: ${error}`);
    process.exit(1);
  }
}

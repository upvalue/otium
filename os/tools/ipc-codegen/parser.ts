import { parse } from "yaml";

export interface Config {
  method_id_base: number;
  error_code_base: number;
}

export interface Arg {
  name: string;
  signed?: boolean; // Defaults to true if omitted
}

export interface Return {
  name: string;
  signed?: boolean; // Defaults to true if omitted
}

export interface Method {
  name: string;
  args: Arg[];
  returns: Return[];
  errors: string[];
  methodId?: number; // Assigned during parsing
}

export interface Service {
  name: string;
  methods: Method[];
}

export interface ErrorCodeDef {
  name: string;
  value: number;
  service: string;
}

export interface ParsedIDL {
  config: Config;
  services: Service[];
  errorCodes: ErrorCodeDef[];
}

export async function parseIDL(filePath: string): Promise<ParsedIDL> {
  const content = await Deno.readTextFile(filePath);
  const yaml: any = parse(content);

  const config: Config = {
    method_id_base: yaml.config?.method_id_base ?? 0x1000,
    error_code_base: yaml.config?.error_code_base ?? 100,
  };

  const services: Service[] = [];
  const errorCodes: ErrorCodeDef[] = [];

  let nextMethodId = config.method_id_base;
  let nextErrorCode = config.error_code_base;

  // Track unique error codes across all services
  const seenErrors = new Set<string>();

  // Parse services
  for (const svc of yaml.services || []) {
    const methods: Method[] = [];

    for (const method of svc.methods || []) {
      const args: Arg[] = (method.args || []).map((arg: any) => ({
        name: typeof arg === "string" ? arg : arg.name,
        signed: typeof arg === "object" ? (arg.signed ?? true) : true,
      }));

      const returns: Return[] = (method.returns || []).map((ret: any) => ({
        name: typeof ret === "string" ? ret : ret.name,
        signed: typeof ret === "object" ? (ret.signed ?? true) : true,
      }));

      methods.push({
        name: method.name,
        args,
        returns,
        errors: method.errors || [],
        methodId: nextMethodId++,
      });

      // Collect unique error codes for this method
      for (const errorName of method.errors || []) {
        const fullName = `${svc.name.toUpperCase()}__${errorName}`;
        if (!seenErrors.has(fullName)) {
          seenErrors.add(fullName);
          errorCodes.push({
            name: fullName,
            value: nextErrorCode++,
            service: svc.name,
          });
        }
      }
    }

    services.push({
      name: svc.name,
      methods,
    });
  }

  return {
    config,
    services,
    errorCodes,
  };
}

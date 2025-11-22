import { parse } from "yaml";

export interface Config {
  method_id_base: number;
  error_code_base: number;
}

export interface IntAlias {
  name: string;
  signed: boolean;
}

// Message type definitions
export interface MessageField {
  name: string;
  type: string;  // Can be primitive, array, or another message type
  element?: string; // For array types, specifies the element type
}

export interface MessageType {
  name: string;
  kind: "struct" | "array";
  fields?: MessageField[]; // For struct types
  element?: string; // For array types
}

export interface Arg {
  name: string;
  type?: string;    // Type: "int" (default), "uint", "string", "buffer", or message type name
  signed?: boolean; // Defaults to true if omitted (for backward compat with int/uint)
}

export interface Return {
  name: string;
  type?: string;    // Type: "int" (default), "uint", "string", "buffer"
  signed?: boolean; // Defaults to true if omitted (for backward compat with int/uint)
}

export interface Method {
  name: string;
  args: Arg[];
  returns: Return[];
  errors: string[];
  methodId?: number; // Assigned during parsing
  returns_comm_data?: boolean; // True if method returns data via comm page
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
  intAliases: IntAlias[];
  messageTypes: MessageType[];
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

  // Parse int aliases
  const intAliases: IntAlias[] = [];
  if (yaml.int_aliases) {
    for (const [name, def] of Object.entries(yaml.int_aliases)) {
      const aliasDef = def as any;
      intAliases.push({
        name,
        signed: aliasDef.signed ?? false,
      });
    }
  }

  // Parse message types
  const messageTypes: MessageType[] = [];
  if (yaml.types) {
    for (const [name, def] of Object.entries(yaml.types)) {
      const typeDef = def as any;

      if (typeDef.type === "array") {
        // Array type: { type: "array", element: "uint" }
        messageTypes.push({
          name,
          kind: "array",
          element: typeDef.element,
        });
      } else if (typeDef.type === "struct") {
        // Struct type: { type: "struct", fields: [...] }
        const fields: MessageField[] = (typeDef.fields || []).map((field: any) => ({
          name: field.name,
          type: field.type,
          element: field.element, // For array fields
        }));
        messageTypes.push({
          name,
          kind: "struct",
          fields,
        });
      }
    }
  }

  const services: Service[] = [];
  const errorCodes: ErrorCodeDef[] = [];

  let nextMethodId = config.method_id_base;
  let nextErrorCode = config.error_code_base;

  // Track unique error codes across all services
  const seenErrors = new Set<string>();

  // Validate method_id_base doesn't use lower 8 bits (reserved for flags)
  if (config.method_id_base % 0x100 !== 0) {
    throw new Error(`method_id_base (0x${config.method_id_base.toString(16)}) must be a multiple of 0x100 to avoid flag bits`);
  }

  // Parse services
  for (const svc of yaml.services || []) {
    const methods: Method[] = [];

    for (const method of svc.methods || []) {
      const args: Arg[] = (method.args || []).map((arg: any) => {
        if (typeof arg === "string") {
          return { name: arg, type: "int", signed: true };
        }
        const type = arg.type || "int";
        const signed = arg.signed ?? (type === "int");
        return { name: arg.name, type, signed };
      });

      const returns: Return[] = (method.returns || []).map((ret: any) => {
        if (typeof ret === "string") {
          return { name: ret, type: "int", signed: true };
        }
        const type = ret.type || "int";
        const signed = ret.signed ?? (type === "int");
        return { name: ret.name, type, signed };
      });

      // Auto-detect if method returns complex data that needs comm page
      const needsCommData = method.returns_comm_data ??
        returns.some(ret => {
          const type = ret.type || "int";
          return type === "string" || type === "buffer" || messageTypes.some(mt => mt.name === type);
        });

      methods.push({
        name: method.name,
        args,
        returns,
        errors: method.errors || [],
        methodId: nextMethodId,
        returns_comm_data: needsCommData,
      });
      nextMethodId += 0x100; // Increment by 256 to skip flag bits

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
    intAliases,
    messageTypes,
    services,
    errorCodes,
  };
}

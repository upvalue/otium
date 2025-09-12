// bunsupport.ts - side effecting file to make values print nice in bun
// without compromising code being able to run in browser
import { getSymbol, OtSymbol } from "./values";

// @ts-expect-error
OtSymbol.prototype[Bun.inspect.custom] = OtSymbol.prototype.toString;

---
id: lan-5je1
status: open
deps: []
links: [lan-q4lf, lan-515h, lan-67q1]
created: 2026-08-16T02:22:20Z
type: epic
priority: 1
assignee: Phil
tags: [errors, diagnostics, developer-experience]
---
# Rust-inspired error diagnostics: TCO-aware backtraces, stable codes, and development desire paths

Otium errors should be useful to a person and to tooling without giving up the language's low-memory goals. Take the useful parts of Rust diagnostics: a stable identity, a concise primary message, actionable context, and a documentation path.

Today native and evaluator failures generally become an ordinary condition table with :type and :message, and the REPL prints that table. The evaluator also implements proper tail calls by reusing a trampoline frame, so a conventional stack trace would silently omit the calls that were optimized away.

Build one diagnostic pipeline with three parts:

1. Bounded backtraces that report physical frames and the number of logical frames removed by tail-call optimization.
2. Stable error codes for known runtime, reader, compiler, and development-hint errors, with permanent documentation anchors.
3. A development-only desire-path layer that recognizes common unsupported forms or names and adds a targeted explanation and Otium-native suggestion. This is meant to help LLM-generated code and people arriving from Scheme. It must not become a compatibility layer or affect production semantics.

Conditions stay ordinary catchable values. Structured diagnostic fields are attached at the point where an error condition is created or first raised; terminal formatting remains a host concern.

This work should coordinate with lan-q4lf. The new VM's CallFrame stack is the natural long-term source of trace frames, and TAILCALL should increment an elided-tail-call counter when it reuses one. The diagnostic data model, error registry, renderer, and desire-path loader can be developed independently while the VM work is underway.

## Design

## Diagnostic data model

Extend known error conditions with stable structured fields. At minimum:

- :code: a stable symbol or string such as OT1001
- :message: the short primary message
- :trace: a bounded array of logical frame records
- :help: optional actionable text, including desire-path guidance

A trace frame should carry the callable name and source name/location when known. It should also carry an elided-tail-calls count. Keep the representation GC-safe and bounded by a configurable small frame limit; when physical frames are truncated, report that count separately. Capturing a diagnostic must not recurse into another allocation or depth failure.

Capture the trace at the original signal site, before handlers run or the stack unwinds. Catching, inspecting, and re-raising a condition must not silently replace the original trace. If error receives a user-provided value, preserve it and only attach metadata when the value follows the condition-table convention and the fields are absent.

## Tail-call accounting

Maintain one counter on each reusable execution frame. Every proper tail call that replaces the current logical callee increments the counter. A trace renders the currently executing function, then a marker such as 37 tail calls elided, then the physical caller. Use saturating counters so diagnostics cannot wrap during an unbounded loop.

Implement this against the bytecode VM CallFrame design from lan-q4lf. If any work lands before that VM, keep the interface evaluator-independent and avoid building a second permanent frame stack for eval_tr.

## Error code registry and documentation

Define a checked-in registry grouped by subsystem, with reserved numeric ranges for reader, expansion/compiler, runtime/application, namespace/module, and host/CLI errors. Codes are never renumbered or reused. Message wording may improve without changing the code when the underlying failure category is unchanged.

Route known internal failures through typed constructors instead of free-form raise_error strings. Arbitrary user conditions and error strings do not need invented codes.

Generate or validate one documentation page/anchor per code. The terminal renderer prints error[OTxxxx] and a stable local or web documentation URL. Add a test that rejects duplicate codes, undocumented codes, reused retired codes, and known error sites that bypass the registry.

## Rendering

Add one renderer shared by script execution and the REPL. Keep condition printing/repr unchanged for programs that inspect values. The renderer should produce, in order: code and primary message, source location when known, optional help, bounded backtrace, tail-call loss markers, and the documentation link.

Output must be deterministic without color for tests and redirected stderr. TTY styling can be layered on without changing the text contract. Reader errors should use the same renderer rather than their current special path.

Source locations are currently discarded after reading, and the planned bytecode Code object has no debug map yet. Add the smallest source metadata needed for diagnostics: source name plus form/instruction location, with a compact instruction-to-location table for compiled code. Agree on that layout with lan-q4lf before Code objects are finalized.

## Development-only desire paths

Add an explicit development diagnostics mode and load a checked-in desire-path rules file only in that mode. A rule matches a structured error code plus narrow context such as an unresolved symbol or rejected form shape, then supplies help text and an optional documentation target.

Rules may clarify an error that would already be raised; they must never rewrite or execute the form, introduce aliases, change condition type/code, or suppress the error. Adding a new rule should require editing the rules file and its tests, not changing the evaluator or compiler.

Start with a small evidence-based catalog of common Scheme or LLM mistakes. Keep the mechanism useful even when the catalog is empty. Production builds and normal CLI execution must not load, embed, allocate, or consult this layer.

## Delivery order

1. Specify the condition schema, trace truncation rules, code format/ranges, and rendered output. Add the registry validator and a few representative codes.
2. Centralize known error construction and add the shared renderer and documentation pages.
3. Add source metadata and bounded frame capture in coordination with lan-q4lf, including TAILCALL loss counters.
4. Add the explicit development mode, desire-path rule loader, first rules, and on/off integration tests.
5. Migrate remaining known error sites, lock down snapshot tests, and document how to add or retire a code and how to add a desire-path rule.

## Non-goals

- Implementing Scheme compatibility or automatically correcting generated code
- Assigning stable codes to arbitrary user-created conditions
- Preserving every tail-called frame or argument value in memory
- Making terminal formatting part of condition equality or language semantics

## Acceptance Criteria

- Known reader, compiler/expander, runtime, namespace/module, and CLI failures carry a documented stable OTxxxx code; arbitrary user errors remain valid without one.
- A checked-in validator fails on duplicate, undocumented, reused retired, or malformed codes and on designated known-error constructors that omit a code.
- Script mode and the REPL use the same deterministic diagnostic renderer and include code, message, available source location, optional help, backtrace, and documentation link.
- A nested non-tail call test reports frames in call order. A deep tail-recursive and mutually tail-recursive test stays constant-space and reports the exact saturated-safe count of frames elided by TCO.
- Trace capture is bounded. Tests cover physical-frame truncation and show that generating a depth or value-stack overflow diagnostic does not recursively fail.
- Handlers still run at the signal site. Catching or re-raising does not discard the original trace, and conditions/restarts/unwind-protect behavior remains unchanged.
- Source names and useful locations survive reading, expansion, compilation, and execution with a compact representation agreed with lan-q4lf.
- Development mode can load desire-path rules from one data file. A fixture for an unsupported Scheme-style name/form adds targeted help while preserving the original code and failure.
- With development mode off, the same input produces the normal diagnostic and no desire-path file is loaded or embedded. Desire-path rules never rewrite or execute input.
- The full test suite, GC-stress build, and sanitizer build pass within the existing configured heap and stack limits.


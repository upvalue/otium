// ns.h — namespaces and vars (spec section 7).
//
// Representation (all GC-managed, rooted via vm->stack slots that hold the
// registry):
//   registry : table  nsName-symbol -> ns record
//   ns record: table  {:name sym  :vars table  :aliases table  :refers table
//                      :order array-of-name-syms}
//   var cell : array  [value, name-sym, ns-sym, docstring, private-flag]
// Redefinition mutates the existing var array in place, so every reference
// through the cell sees the new value instantly.
#pragma once
#include "value.h"

typedef struct State State;
enum VarSlot : u32 { VAR_VALUE = 0, VAR_NAME, VAR_NS, VAR_DOC, VAR_PRIVATE, VAR_SLOTS };

// Registry access
Value ns_lookup(State* vm, u32 nsName);         // ns record or nil
Value ns_get_or_create(State* vm, u32 nsName);  // creates + auto-refers otium.core

// Fields of an ns record (kw is one of vm->syms.kwVars / kwAliases / ...)
Value ns_field(State* vm, Value nsRecord, u32 kwId);

// Full 3.1 resolution (non-lexical part): own vars -> refers; qualified via
// alias table then literal name; privacy enforced. Returns the VALUE, or
// Unwind if unresolvable.
Value ns_resolve(State* vm, Value symbol);

// Same walk but returns the var CELL, or nil (never raises). Used by set!
// and the expander oracle.
Value ns_resolve_var(State* vm, Value symbol);

// Define (or redefine, reusing the cell) `name` in the current namespace.
// Returns v, or Unwind.
Value ns_define(State* vm, u32 name, Value v, bool isPrivate, Value docstring);

void ns_switch(State* vm, u32 nsName);  // creates if needed

// Helpers over var cells
Value var_value(Value var);
void var_set(Value var, Value v);
bool var_private(Value var);

// True if the symbol has an interior '/' (namespace-qualified form).
bool sym_qualified(State* vm, u32 symId);

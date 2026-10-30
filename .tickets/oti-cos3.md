---
id: oti-cos3
status: open
deps: []
links: []
created: 2026-10-30T04:18:15Z
type: task
priority: 2
assignee: Phil
tags: [language, expander]
---
# Make when/unless ordinary core macros

when/unless are handled in four places: core.ot defmacros, hard-coded in the expander (expander.ot:226), convert.ot special-forms, and compiler.c. The back ends match them by name regardless of binding, and they are not in expander-core-forms, so a local or global named when/unless is silently ignored (confirmed on native and --c.vm). The hard-coding exists because of boot order: expander.ot (1 use) is compiled with no expander, then primitives.ot (13 uses) loads before core.ot defines the macros. Fix: rewrite those stage-0 uses with if (or define when/unless with defmacro at the top of primitives.ot), then delete the expander, convert.ot and compiler.c special cases so when/unless are plain macros that can be shadowed. %call-with-cleanup is in special-forms but not reserved too; look at it at the same time. Prefer removing special names over adding checks.


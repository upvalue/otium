# ✨ Otium docs

Otium is an experimental Lisp. It comes mostly from Scheme, and it borrows some ideas from Clojure (namespaces) and Janet (practicality, tables). It has two runtimes. The Chez Scheme runtime is the main one and is the faster of the two. The C runtime is experimental and is meant for small embedded systems.

This repository also holds two programs written in Otium:

- **Manse** is a terminal multiplexer built on libghostty. To manage terminals, it scrolls through them, like the Niri Wayland compositor. Its code is in `manse/`.
- **otos** is a RISC-V unikernel operating system that runs the Otium C runtime. Its code is in `otos/`.

## Docs

- [Otium docs](README.md): this page. It covers what is here and how the docs are made.
- [Otium language guide](language-guide.md): the language, the `otium` command, namespaces, errors, live reloading and the standard library.
- [Manse guide](manse/guide.md): how to build, run and change Manse.
- [otos guide](otos/guide.md): how to build, boot and change otos.

Design notes and proposals are in `specs/`. They may describe features that don't exist yet. Agents writing Otium code should read `AGENT-LANG.md` first. It is a short summary of the language.

## How the docs are made

Some docs are written by Claude Code and some are written by hand. You can tell them apart by two signs:

- A generated doc has a `.md.spec` file next to it, and its title starts with ✨. For example, `docs/language-guide.md` is generated from `docs/language-guide.md.spec`.
- A hand-written doc has no spec and no ✨ in its title.

`tools/generate-docs` writes the generated docs. `tools/check-docs` checks that each doc in `docs/` follows the ✨ rule. It also checks that repository paths named in backticks exist, and that docs are kept only in allowed places such as `docs/` and `specs/`.

### Specs

A spec starts with front matter that has two required keys:

- `title`: the doc's title, without the ✨.
- `prompt`: the main instruction to Claude.

After the front matter, you can add a body with more detail, such as what to cover and which files to read. The tool adds the body to the prompt.

```markdown
---
title: "Manse guide"
prompt: |
  Write a guide to Manse for someone who wants to run it and change it.
---

Cover how to build and run it, which file owns what, and how to
run its tests. Link to the language guide rather than repeating it.
```

The front matter parser only handles a small part of YAML: `key: value` lines, quoted strings, and `|` or `>` blocks.

For each spec, the tool runs `claude -p` with read-only tools. It also passes a set of rules: put ✨ in the title, describe the code as it is today, and link only to docs that exist. The reply is written to the file next to the spec. If the reply doesn't start with `# ✨`, the tool reports an error and leaves the existing file alone.

### Regenerating

```sh
just docs                          # regenerate every doc that has a spec
just docs docs/language-guide.md   # regenerate one doc (the .md or .md.spec path both work)
just docs -n                       # print the prompts without running Claude
```

`-j N` sets how many docs are generated at once. The default is 4.

The output can change from one run to the next, and it can be wrong. Read the diff of each generated doc before you commit it, and run `tools/check-docs`.

### Adding a generated doc

1. Create `docs/<name>.md.spec` with a `title`, a `prompt` and, if you need one, a body. Specs can be in subdirectories, like `docs/manse/guide.md.spec`.
2. Run `just docs docs/<name>.md`.
3. Review the result and commit both files.

When the tool builds a new doc, it gives Claude a list of all the docs, so other generated docs can link to the new one once they are regenerated.

### Making a generated doc hand-written

1. Delete the `.md.spec` file.
2. Remove the ✨ from the doc's title.
3. Edit the doc however you like. `just docs` won't touch it again.

If you do only one of the first two steps, `tools/check-docs` will report an error.

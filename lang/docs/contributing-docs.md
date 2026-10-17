# Maintaining the docs

> ✨ LLM-generated draft.

`docs/` contains maintained user and contributor documentation. Astro renders the user-facing Markdown pages listed in `site/navigation.mjs` directly. Contributor and implementation notes stay in the repository. `specs/` contains design contracts, experiments, and proposals that may drift from the implementation. Specs stay in the repository and are linked where useful; they are not published as site pages.

## Build and preview

From `lang/`, install Python 3, Node.js 22.12 or newer, make, and Emscripten with C23 support. The tested local Emscripten version is 6.0.1.

```sh
npm --prefix site ci
make site
python3 -m http.server 8000 --directory site/dist
```

Open `http://localhost:8000`. For live Markdown editing, run `npm --prefix site run dev` and use the URL Astro prints. Python is only used for extracting reference text and running checks; Astro handles Markdown rendering and the static build. The site also works under a GitHub Pages repository prefix: its assets and internal page links are relative. Do not open the HTML using `file://`; workers and WASM need an HTTP server.

A configured `emcc` must be on `PATH`, or pass `EMCC=/path/to/emcc` to make. If a Homebrew Emscripten install lacks its config, point `EM_CONFIG` at a local configuration containing `LLVM_ROOT`, `BINARYEN_ROOT`, `NODE_JS`, and a writable `CACHE`. Use the tools bundled with that Emscripten installation.

## Edit a page

Edit Markdown in `docs/`, then rebuild. Add navigation entries in `site/navigation.mjs` for new user-facing pages; this list also controls which pages are published. Relative Markdown links to published pages are converted to site links. Other documentation links point to their repository sources. Links to repository source files go to GitHub. Standard Markdown, fenced code, tables, and heading anchors are supported.

Keep the page-level ✨ notice until generated writing has been reviewed and replaced. The site template also identifies the draft site. All newly generated source comments use ✨. Existing source documentation is reused without adding individual markers to every reference entry.

Do not edit README files as part of this workflow.

## Regenerate the reference

The build reads `shared/documentation/runtime.json` and public docstrings in `shared/prelude/core.ot`. Prelude docstrings override catalog entries, matching runtime help precedence. Native C bindings and forms use the shared catalog. The site generator records each entry's source and emits `site/dist/reference.md` along with its HTML.

Update the catalog or definition docstring, then rebuild. Do not edit the generated reference. If the catalog changes, also run `tools/python shared/documentation/generate.py` to refresh the runtime's generated files.

## Verify changes

```sh
make test-site
```

This builds the site and WASM runtime, runs the C-dialect conformance fixtures in WASM, evaluates guide and playground examples, checks persistent definitions and error recovery, and validates local links. Native C changes also need their normal runtime tests. Changes around managed values require the optional `make gc-lint` check when LLVM development tools are installed.

## Publish with GitHub Pages

The repository workflow `.github/workflows/language-pages.yml` builds and tests the site, then uploads `lang/site/dist` as a Pages artifact. Set the repository's Pages source to GitHub Actions and run the workflow manually on the branch you want to publish. It deliberately has no automatic push trigger, so merging this experiment does not replace an existing site by itself.

The Pages workflow deploys to the repository's Pages site. Review that destination before running it if the repository already serves another site. No custom domain is configured here.

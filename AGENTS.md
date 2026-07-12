# AGENTS

This document is the working guide for coding agents and contributors in this repository.

## What this project is

scalpel-editor is a Wayland-only text editor built from a refactored Scintilla core. It is not a general UI toolkit, not cross-platform, and not designed for embedding. It does one thing: edit text in one window on Wayland.

The project has two goals of equal weight. First, a small, sharp editor. Second, a demonstration that a mature codebase can be refactored until both humans and AI models can read it: every feature findable by name, documentation next to implementation, no layer that exists only to serve platforms or uses this project does not have. Judge changes against both goals.

## Direction principles

- One platform (Wayland), one encoding (UTF-8), one renderer. Delete generality that serves absent platforms or absent uses.
- A feature's name, documentation, and implementation belong in one greppable place. No numeric message dispatch, no indirection that severs the link between a name and its code.
- Prefer deleting indirection over adding abstraction. When tempted to make something more generally useful, stop and make it do its one job better instead.
- Keep the tree small enough to hold in one head. The core fitting in a single model context window is a measurable target, not a metaphor.
- Refactor in behavior-preserving steps with tests green, and make each step small enough to review as a diff.

## Repository layout

- `scintilla/` — the Scintilla 5.6.4 core, imported verbatim. `scintilla/UPSTREAM.md` records the release identity and the byte-for-byte verification of the import. This code is now this project's to change; the verbatim import commit is the baseline, and git history from that commit is the record of divergence. Do not update `UPSTREAM.md` as the code diverges — it describes the import, not the current state.
- `seed/` — working reference code copied from OnlyWayUi (see `ORIGINS.md`). It shows a Scintilla-on-Wayland editor built through RmlUi's abstractions. Mine it, do not build on it: absorb what a piece teaches into direct code, then delete the piece. It does not compile in this repository and that is expected.
- `ROADMAP.md` — the phase plan. Update it when a phase completes or the plan changes.

## Change rules for scintilla/

- Keep `scintilla/test/unit` green through every refactoring step. These upstream tests are the safety net; grow them when dissolving code they cover thinly.
- When a message moves out of the dispatch switch into a named method, move its documentation too: find its entry in `scintilla/ScintillaDoc.html` and rewrite it as a doc comment on the method. The goal state is that `ScintillaDoc.html` becomes empty of live content and is deleted.
- The unit tests are strong on `Document`, `CellBuffer`, and the container classes, and thin on `Editor` behavior. When dissolving a message whose behavior is untested, either keep the transformation mechanical enough that the diff is its own proof, or add a test first.

## Build and test

There is no build yet. The first roadmap phase stands up CMake for the core plus the upstream unit tests. Update this section with the real commands in the same change that creates them — do not let this file describe a state the code does not have.

## Source reading rule

Do not write code that calls an external API without reading the relevant source or local headers first. This includes Wayland, EGL, xkbcommon, FreeType, and the Scintilla core itself. If the needed source is not available, stop and ask for it. Do not guess signatures, types, ownership rules, or lifecycle rules.

## Style

Use simple, direct names and explanations. Favor code that shows lifetime, ownership, and control flow clearly. Avoid vague framework jargon in code, comments, commit messages, and docs — describe the concrete behavior.

New code follows the naming and layout of the file it lives in. The Scintilla core keeps its existing conventions; do not restyle code while refactoring it.

## Documentation guidance

Do not leave documentation describing a state the code no longer has. When a change removes or alters something the docs describe, update or delete that description in the same commit.

In Markdown files, do not hard-wrap prose. Write each paragraph and each list item as a single line and let the editor soft-wrap it.

## Tooling notes

- grepai (semantic search): make sure the index includes `.cxx` and `.iface` files — a missing extension silently returns no results for most of the Scintilla core.
- Do not add a `.*/` pattern to `.gitignore`. grepai's ignore matcher mis-reads it as "every directory" and indexes nothing. The checked-in `.gitignore` avoids it deliberately; keep it that way or list hidden directories explicitly.

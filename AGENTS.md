# AGENTS

scalpel-editor is a Wayland-only plain-text editor built from a substantially refactored, project-owned Scintilla 5.6.4 core. Application features use named typed operations, and the platform layer directly uses Wayland, EGL, OpenGL, FreeType, HarfBuzz, and Fontconfig.

## Repository

- `scintilla/` — editor core, renderer, platform definitions, and core tests.
- `app/` — production editor host, application platform definitions, standalone executable, and application tests.
- `docs/` — current product, architecture, and design documentation.
- `.plans/` — ignored cross-session working plans plus their tracked guidance and template.

## Development rules

- If you find a flaw or deficiency in the existing framework while implementing something, report it instead of silently working around it.
- Before calling an external API, read its relevant source or local headers. This includes Wayland, EGL, xkbcommon, FreeType, and the Scintilla core. If the needed source is unavailable, stop and ask for it; do not guess signatures, types, ownership, or lifecycle behavior.
- Prefer deleting indirection over adding abstraction. Make code do its current job better instead of making it more generally useful.
- Keep documentation consistent with changed behavior.

## Work routing

- For substantial work, follow [the local planning guidance](.plans/README.md).
- Before configuring or selecting a build tree, read [BUILDING.md](BUILDING.md).
- Before building or testing code, read [TESTING.md](TESTING.md) and use the smallest target and focused test pattern that cover the change.
- Before editing Markdown or other documentation, read [the documentation instructions](docs/AGENTS.md).
- More specific `AGENTS.md` files add instructions for work in their directory trees.

## Commits

- Run `git add`, validation commands, and `git commit` as separate shell calls. Do not combine them with shell operators.
- Do not use `$'...'` shell quoting for commit messages. Invoke `git` directly so the configured command rules apply.
- Give each commit an extended message that concisely explains what changed and why.
- Hard-wrap commit messages at 68 characters; the commit hook enforces an absolute maximum of 72 characters.

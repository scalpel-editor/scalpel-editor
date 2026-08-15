---
name: scalps-code-search
description: Locate relevant C++ function and method definitions with scalps. Use at the start of unfamiliar C++ code discovery, when determining where behavior is implemented, or before broad text searches for an operation or concern in an indexed C++ project.
---

# Search C++ definitions

## Select the platform inputs

Read `/etc/os-release` before invoking scalps. When `ID=nixos`, use the
NixOS commands in this skill. Otherwise, use the standard commands.

The checkout is shared between openSUSE and NixOS, but their compilation
databases contain different compiler and dependency paths. Keep their scalps
indexes separate for the same reason that their CMake build trees are separate.

On openSUSE, the standard inputs are:

- compilation database: `build/compile_commands.json` (the scalps default)
- index: `.scalps/index.sqlite` (the scalps default)

On NixOS, always use:

- compilation database: `build-nixos/compile_commands.json`
- index: `.scalps/index-nixos.sqlite`
- the pinned environment: prefix `status` and `index` with `nix develop
  --command`

Current scalps versions automatically authorize the exact compiler wrappers
from `/nix/store` when all compilation drivers resolve there. Do not add a
broad query-driver allowlist. If `build-nixos/compile_commands.json` does not
exist, report that the NixOS development tree must be configured and fall back
to ordinary repository search; do not silently use the openSUSE database.

## Check freshness

On openSUSE, start with:

```sh
scalps status
```

On NixOS, start with:

```sh
nix develop --command scalps status \
  --compile-commands build-nixos/compile_commands.json \
  --index .scalps/index-nixos.sqlite
```

If the index is stale, run the matching `scalps index` command directly with
the environment's command tool (for example `exec_command` /
`run_terminal_command`). Do not wrap indexing in a helper script that can
outlive the command session.

## Waiting for indexing

- On openSUSE, run `scalps index` as a single direct command.
- On NixOS, run the following as a single direct command:

  ```sh
  nix develop --command scalps index \
    --compile-commands build-nixos/compile_commands.json \
    --index .scalps/index-nixos.sqlite
  ```

- Wait for the indexing command to finish.
- If the tool returns a `session_id` without an `exit_code`, retain that session handle and poll with `write_stdin` (or the environment's equivalent session poll) until the process exits. Do not use `functions.wait` for the underlying command.
- Only after indexing exits successfully, run the matching platform `status`
  command again.
- Proceed to `search` or `context` only when status reports the index is fresh.
- If another process is already indexing (`indexing already in progress`, or status prints `Indexing is in progress.`), do not launch a replacement `scalps index`. Wait for that refresh to finish, then re-check status.
- Never treat a yielded or backgrounded indexing invocation as completed work.

## Search

Run searches from the project root:

```sh
scalps search --limit 5 "<short operation and object>"
```

On NixOS, select its separate index (search does not need `nix develop`):

```sh
scalps search --index .scalps/index-nixos.sqlite --limit 5 \
  "<short operation and object>"
```

Retain the platform's index selection on every later `search` and `context`
invocation, including refined and verbose searches.

Prefer behavior-oriented queries, such as:

- `format search result`
- `parse command arguments`
- `replace definition index`
- `find text in target`

After selecting a result, print its indexed definition with the qualified name reported by search:

```sh
scalps context "<qualified name>"
```

On NixOS, select its separate index:

```sh
scalps context --index .scalps/index-nixos.sqlite "<qualified name>"
```

Use `--before 1` or `--after 1` when whole neighboring definitions help explain the target. If an overloaded name is ambiguous, use the reported `--file` and `--line` selectors to choose the intended definition. If context reports stale source, reindex before retrying. If search or context reports that indexing is in progress, wait for that refresh instead of searching the previous generation.

If the first search is weak, refine it once by using a more precise operation, adding the affected object or concern, or trying a likely identifier-shaped term.

When choosing between nearby results, rerun the search with `-v` and read the score explanation. Prefer strong name, operation, and signature contributions over a result supported only by body text.

After finding the likely definition, use ordinary repository tools to locate its callers, references, declarations, tests, configuration, and related non-definition text.

Do not treat scalps output as a complete repository search. If the command or index cannot be used, briefly note that and fall back to the repository's normal search tools.

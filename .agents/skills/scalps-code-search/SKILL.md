---
name: scalps-code-search
description: Locate relevant C++ function and method definitions with scalps. Use at the start of unfamiliar C++ code discovery, when determining where behavior is implemented, or before broad text searches for an operation or concern in an indexed C++ project.
---

# Search C++ definitions

Run searches from the project root:

```sh
scalps search --limit 5 "<short operation and object>"
```

Prefer behavior-oriented queries, such as:

- `format search result`
- `parse command arguments`
- `replace definition index`
- `find text in target`

After selecting a result, print its indexed definition with the qualified name reported by search:

```sh
scalps context "<qualified name>"
```

Use `--before 1` or `--after 1` when whole neighboring definitions help explain the target. If an overloaded name is ambiguous, use the reported `--file` and `--line` selectors to choose the intended definition. If context reports stale source, reindex before retrying.

If the first search is weak, refine it once by using a more precise operation, adding the affected object or concern, or trying a likely identifier-shaped term.

When choosing between nearby results, rerun the search with `-v` and read the score explanation. Prefer strong name, operation, and signature contributions over a result supported only by body text.

After finding the likely definition, use ordinary repository tools to locate its callers, references, declarations, tests, configuration, and related non-definition text.

Do not treat scalps output as a complete repository search. If the command or index cannot be used, briefly note that and fall back to the repository's normal search tools.

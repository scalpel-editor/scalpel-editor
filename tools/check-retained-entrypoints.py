#!/usr/bin/env python3
"""Audit retained Scintilla.iface callables against the phase-4 named-method state.

For every fun/get/set in Scintilla.iface (and the MESSAGE_REMOVAL.md inventory):

- Feature-to-delete entries must have no case Message::Name in scintilla/src.
- Retained callables must have a temporary Message:: case (parameter unpack + call)
  and a typed named operation (method, or EditorCommand for keyboard commands).
- Notifications (evt) are classified only; they are not message switch cases.

Exits 0 when all checks pass. Run from any cwd; paths resolve relative to the repo root.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IFACE = ROOT / "scintilla" / "include" / "Scintilla.iface"
GUIDE = ROOT / "MESSAGE_REMOVAL.md"
SRC = ROOT / "scintilla" / "src"
COMMANDS_H = SRC / "EditorCommands.h"
COMMANDS_CXX = SRC / "EditorCommands.cxx"
DISPATCH_FILES = (SRC / "Editor.cxx", SRC / "ScintillaBase.cxx")

# Message names whose typed operation uses a different spelling after migration.
# The harness accepts any of the listed definition names.
METHOD_ALIASES: dict[str, tuple[str, ...]] = {
    "GetFocus": ("HasFocus",),
    "StartRecord": ("StartRecording",),
    "StopRecord": ("StopRecording",),
    # Annotation visibility setters use Set* order; getters keep Annotation* names.
    "AnnotationSetVisible": ("SetAnnotationVisible",),
    "EOLAnnotationSetVisible": ("SetEOLAnnotationVisible",),
    # Selection mode: ChangeSelectionMode is SetSelectionMode(..., setMoveExtends=false).
    "ChangeSelectionMode": ("SetSelectionMode",),
    "SetSelectionMode": ("SetSelectionMode",),
    # Hit-testing messages share PositionFromLocation with flag combinations.
    "PositionFromPoint": ("PositionFromLocation",),
    "PositionFromPointClose": ("PositionFromLocation",),
    "CharPositionFromPoint": ("PositionFromLocation",),
    "CharPositionFromPointClose": ("PositionFromLocation",),
    # Clipboard range copy.
    "CopyRange": ("CopyRangeToClipboard",),
    # Multi-selection drop uses DropSelection(size_t).
    "DropSelectionN": ("DropSelection",),
    # SetSelection replaces the whole selection model with one stream range.
    "SetSelection": ("SetStreamSelection",),
    # Property expanded form is not separately implemented.
    "GetPropertyExpanded": ("GetProperty",),
    # Pattern/minimal replace share ReplaceTarget(ReplaceType, ...).
    "ReplaceTargetRE": ("ReplaceTarget",),
    "ReplaceTargetMinimal": ("ReplaceTarget",),
    "ReplaceTarget": ("ReplaceTarget",),
    # Line height in pixels (message name was TextHeight).
    "TextHeight": ("TextHeightPixels",),
}

# Retained entries allowed without a Message:: case (must still have a named method).
# Empty after step 17 fixes; kept for explicit future exemptions.
ALLOW_MISSING_CASE: set[str] = set()

# Entries that are intentionally not methods and not EditorCommand members.
SKIP_NAMED_CHECK: set[str] = set()


def iface_callables() -> dict[str, str]:
    """name -> kind (fun|get|set|evt).

    iface lines look like: fun void AddText=2001(position length, string text)
    """
    out: dict[str, str] = {}
    for line in IFACE.read_text().splitlines():
        m = re.match(r"^(fun|get|set|evt)\s+\S+\s+(\w+)=", line)
        if m:
            out[m.group(2)] = m.group(1)
    return out


def inventory_names() -> set[str]:
    """Names from the Interface Inventory exhaustive lists (`fun Name=num`)."""
    text = GUIDE.read_text()
    inv = text.split("## Interface Inventory", 1)[1]
    names: set[str] = set()
    for m in re.finditer(r"`(?:fun|get|set|evt)\s+(\w+)=", inv):
        names.add(m.group(1))
    return names


def deleted_names() -> set[str]:
    """Names listed in decision rows classified Feature to delete."""
    deleted: set[str] = set()
    for line in GUIDE.read_text().splitlines():
        if "Feature to delete" not in line or "|" not in line:
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if not cells:
            continue
        entries_cell = cells[0]
        # Backtick names
        for e in re.findall(r"`([A-Za-z][A-Za-z0-9_]*)`", entries_cell):
            deleted.add(e)
        # Bare CamelCase tokens (rows that list without backticks on every name)
        for e in re.findall(r"\b([A-Z][A-Za-z0-9_]+)\b", entries_cell):
            if e in ("Feature", "Not", "All", "Entries"):
                continue
            deleted.add(e)
    return deleted


def message_cases() -> set[str]:
    cases: set[str] = set()
    for path in DISPATCH_FILES:
        text = path.read_text()
        for m in re.finditer(r"case\s+Message::(\w+)", text):
            cases.add(m.group(1))
    return cases


def editor_commands() -> set[str]:
    text = COMMANDS_H.read_text()
    m = re.search(r"enum class EditorCommand\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("EditorCommand enum not found")
    body = m.group(1)
    names = set(re.findall(r"\b([A-Z][A-Za-z0-9_]*)\b", body))
    names.discard("None")
    return names


def command_from_message_keys() -> set[str]:
    text = COMMANDS_CXX.read_text()
    return set(re.findall(r"case\s+Message::(\w+)\s*:\s*return\s+EditorCommand::", text))


def named_method_index() -> dict[str, list[str]]:
    """Map short method name -> list of 'file:line' definition sites in src/."""
    index: dict[str, list[str]] = {}
    # Definitions: Type Class::Name( or Type Name( at column 0 for free functions
    # Prefer Class::Name patterns used throughout the core.
    pat = re.compile(
        r"^(?:[\w:<>*&,\s]+)?\b(?:Editor|ScintillaBase|Document|EditModel|EditView|ViewStyle|"
        r"AutoComplete|CallTip|KeyMap|LexState)::(\w+)\s*\(",
        re.M,
    )
    for path in sorted(SRC.glob("*.cxx")) + sorted(SRC.glob("*.h")):
        text = path.read_text()
        for m in pat.finditer(text):
            name = m.group(1)
            line = text.count("\n", 0, m.start()) + 1
            index.setdefault(name, []).append(f"{path.relative_to(ROOT)}:{line}")
    return index


def case_bodies() -> dict[str, list[str]]:
    """Map each Message name to its effective WndProc case bodies.

    Consecutive labels share the first non-empty body after them. Keep every body
    when Editor and ScintillaBase both handle a message so both paths are checked.
    """
    bodies: dict[str, list[str]] = {}
    for path in DISPATCH_FILES:
        text = path.read_text()
        parts = re.split(r"\n\tcase Message::", text)
        parsed: list[tuple[str, str]] = []
        for part in parts[1:]:
            name = part.split(":", 1)[0].strip()
            lines: list[str] = []
            for line in part.splitlines()[1:]:
                if line.startswith("\tcase Message::") or line.startswith("\tdefault:"):
                    break
                # stop at unindented closing of function is rare inside switch
                if line.startswith("}") and not line.startswith("\t"):
                    break
                lines.append(line)
            parsed.append((name, "\n".join(lines)))

        effective_body = ""
        for name, body in reversed(parsed):
            if body.strip():
                effective_body = body
            bodies.setdefault(name, []).append(effective_body)
    return bodies


def is_thin_forwarder(body: str) -> bool:
    """Allow parameter unpack + single call / StringResult / ExecuteCommand / return."""
    # Strip block and line comments
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//.*?$", "", body, flags=re.M)
    # Remove pure fall-through empty bodies (shared with next case)
    stripped = body.strip()
    if not stripped:
        return True
    # Forbidden: nested switches, large local functions, assignment to editor state
    # without an obvious named call — keep this heuristic conservative.
    if re.search(r"\bswitch\s*\(", stripped):
        return False
    # Count statements roughly
    statements = [
        s.strip()
        for s in re.split(r"[;\n]", stripped)
        if s.strip() and s.strip() not in ("{", "}", "break", "break;")
    ]
    # Allow structure field copies for FormatRange-style wideners
    if len(statements) > 20:
        return False
    # Must mention a call-like token or simple return of named getter
    callish = re.search(
        r"\b(ExecuteCommand|CommandFromMessage|StringResult|"
        r"[A-Z][A-Za-z0-9_]*\s*\(|return\s+)",
        stripped,
    )
    return callish is not None


def forwards_to_named_operation(
    body: str,
    candidates: tuple[str, ...],
    has_command: bool,
    has_cmd_map: bool,
) -> bool:
    """Whether a case reaches its matching method or EditorCommand."""
    if any(re.search(rf"\b{re.escape(candidate)}\s*\(", body) for candidate in candidates):
        return True
    if not has_command:
        return False
    if has_cmd_map and re.search(r"\bCommandFromMessage\s*\(\s*iMessage\s*\)", body):
        return True
    return any(
        re.search(rf"\bEditorCommand::{re.escape(candidate)}\b", body)
        for candidate in candidates
    )


def main() -> int:
    callables = iface_callables()
    inv = inventory_names()
    deleted = deleted_names()
    cases = message_cases()
    commands = editor_commands()
    cmd_from_msg = command_from_message_keys()
    methods = named_method_index()
    bodies = case_bodies()

    errors: list[str] = []
    warnings: list[str] = []

    # Inventory membership already checked by check-message-inventory.sh;
    # still report drift so this tool stands alone.
    iface_names = set(callables)
    if inv != iface_names:
        only_inv = sorted(inv - iface_names)
        only_iface = sorted(iface_names - inv)
        if only_inv:
            errors.append(f"inventory-only names ({len(only_inv)}): {only_inv[:10]}")
        if only_iface:
            errors.append(f"iface-only names ({len(only_iface)}): {only_iface[:10]}")

    # Deleted names must be subset of iface
    for name in sorted(deleted - iface_names):
        warnings.append(f"deleted-table name not in iface (ignored): {name}")

    deleted = deleted & iface_names

    for name, kind in sorted(callables.items()):
        if kind == "evt":
            # Notifications: no Message case required. MacroRecord path deleted is special-cased.
            if name in deleted:
                if name in cases:
                    errors.append(f"deleted notification still has Message case: {name}")
            continue

        if name in deleted:
            if name in cases:
                errors.append(f"deleted entry still has case Message::{name}")
            continue

        candidates = METHOD_ALIASES.get(name, (name,))
        has_command = name in commands or any(a in commands for a in candidates)
        has_cmd_map = name in cmd_from_msg
        has_method = any(a in methods for a in candidates)

        # Retained callable
        if name not in cases:
            if name not in ALLOW_MISSING_CASE:
                errors.append(f"retained entry missing case Message::{name}")
            # Still require a named operation even when the case is absent.
        else:
            entry_bodies = bodies.get(name, [])
            if not entry_bodies:
                errors.append(f"case Message::{name} body was not found in WndProc")
            for body in entry_bodies:
                if not is_thin_forwarder(body):
                    errors.append(
                        f"case Message::{name} does not look like a thin forwarder"
                    )
                elif not forwards_to_named_operation(
                    body, candidates, has_command, has_cmd_map
                ):
                    wanted = " / ".join(candidates)
                    errors.append(
                        f"case Message::{name} does not forward to {wanted}"
                    )

        if name in SKIP_NAMED_CHECK:
            continue

        # Keyboard-command path: either mapped command or method (some dual-role ops).
        if not has_method and not has_command:
            wanted = " / ".join(f"{c}()" for c in candidates)
            errors.append(
                f"retained entry {name}: no named method ({wanted}) "
                f"and no EditorCommand::{name}"
            )
        elif has_command and name in cases:
            entry_bodies = bodies.get(name, [])
            uses_message_map = any(
                "CommandFromMessage" in body for body in entry_bodies
            )
            if uses_message_map and not has_cmd_map:
                errors.append(
                    f"command entry {name}: ExecuteCommand path without "
                    f"CommandFromMessage mapping"
                )

    # Unexpected Message cases not in iface
    for name in sorted(cases - iface_names):
        errors.append(f"case Message::{name} has no iface entry")

    # Summary
    retained = [n for n, k in callables.items() if k != "evt" and n not in deleted]
    print(f"retained-entry audit: {len(callables)} iface entries "
          f"({len(retained)} retained callables, {len(deleted)} deleted callables, "
          f"{sum(1 for k in callables.values() if k == 'evt')} notifications)")
    print(f"  Message cases: {len(cases)}")
    print(f"  EditorCommand members: {len(commands)}")
    print(f"  named method index keys: {len(methods)}")

    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)

    if errors:
        print(f"FAILED: {len(errors)} problem(s)", file=sys.stderr)
        for e in errors:
            print(f"  {e}", file=sys.stderr)
        return 1

    print("retained-entry audit: all retained callables have thin Message cases "
          "and a named method or EditorCommand; deletions have no dispatch cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# Scintilla core boundary

The `scintilla/` tree is the editor core owned by this project. It is based on Scintilla 5.6.4 and retains Scintilla's document model, editing behavior, layout, styling, autocomplete, call-tip, printing, and lexer attachment points. The standalone application is its only editor host.

## Typed application interface

The core does not expose Scintilla's generated numeric message interface. Application-facing behavior uses narrow named methods, internal operations stay within their owning concern, and bindable zero-argument editing actions use `EditorCommand`.

Editor implementations are grouped by concern in `Editor*.cxx` and `ScintillaBase.cxx`. Descriptions of retained operations live beside their declarations or definitions, and focused tests exercise the named path directly.

Notifications use the project-owned types in `EditorNotifications.h`. Macro recording uses owning typed action values. Parameter packing, generated client wrappers, message constants, and a general embedding API are not part of the project.

## Application host

`ApplicationEditor` is the production `ScintillaBase` host. It owns the application window state, renderer connection, damage, editor timers and idle work, clipboard and primary-selection requests, text-input state, and the set of retained Scintilla documents.

One retained document is active for input and painting. Inactive documents keep their bytes, undo state, save point, selection, and scroll snapshot. `DocumentWorkspace` maps application tabs and file paths onto the opaque `DocumentId` values exposed by the host.

Application chrome calls named host operations for undo, redo, cut, copy, paste, select all, scrolling, document switching, and file state. It does not reproduce editing behavior outside Scintilla.

## Text and layout contract

The editor core uses UTF-8. Invalid bytes remain stored unchanged and act as one character for movement, width, deletion, and drawing. This preserves arbitrary file bytes while keeping layout and editing behavior deterministic.

Screen measurement and drawing use the shaped-run path described in [rendering.md](rendering.md). The retained direction field and screen-line layout interface allow future layout work without adding a second measurement model.

## Lexilla-facing contract

`Sci_Position.h`, `ILexer.h`, and `ILoader.h` remain the external lexer and loader contract. Their `SCI_METHOD`, position types, and interface version values keep the definitions required by Lexilla. `IDocument::CodePage()` reports UTF-8 and `IsDBCSLeadByte()` reports false.

`lexilla/` is a Markdown-only extract of Lexilla 5.5.3: `License.txt`, `version.txt`, `lexers/LexMarkdown.cxx`, and the `lexlib` files that lexer compiles against. Those files are unmodified upstream sources. Replace them from the matching paths in an official Lexilla zip when updating; do not edit them, and do not import the rest of the catalogue.

The static `lexilla` target is built from that extract plus project-owned glue in `lexilla-compat/`. It is excluded from the default `all` target so a unit-only build does not compile it; `scalpel_application` and test targets that link it still build it. A private `Scintilla.h` supplies the fold-level, property-type, line-end, and `KEYWORDSET_MAX` constants used while compiling Lexilla. A private `SciLexer.h` supplies `SCLEX_MARKDOWN`, `SCLEX_AUTOMATIC`, and the `SCE_MARKDOWN_*` style numbers. Neither header is the generated message interface, and neither is on a consumer's include path. Application-owned `MarkdownStyles.h` repeats the Markdown style numbers for the host.

`CreateLexer("markdown")` returns a fresh Markdown `ILexer5`. Any other name returns null. The factory smoke test creates that lexer, checks its name and numeric identifier, and calls `Release`. The application host and `editorTest` attach that lexer through `SetILexer`. The named `SetDocumentLanguage` operation owns attachment and release for each retained document. Lexer attachment must use these retained interfaces and must not recreate the removed message surface.

## Verification boundary

`unitTest` covers platform-free document and container behavior. `editorTest` links the full editor concern set against a deterministic test platform so missing definitions fail the link and host interactions remain observable. Application tests cover the production host and the boundary between Scintilla state and application policy.

`tools/check-no-message-layer.sh` guards against reintroducing generated messages, dispatch switches, packed parameters, or documentation that directs callers through the old interface.

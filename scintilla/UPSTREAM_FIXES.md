# Upstream Scintilla fixes

This ledger records fixes adapted from Scintilla after the project-owned core diverged. It identifies the upstream review boundary and each incorporated change without treating the local tree as a vendored copy.

## Source base

- Imported release: Scintilla 5.6.4
- Imported archive changeset: `ec5a4a39853007fa6b811ee7646cd154c8672069`
- Archive identity: `.hg_archival.txt`

## Review position

- Reviewed through changeset: `ed369e0541faf053a8a723e6bdf707ecde056c38`
- Review date: 2026-08-07
- Latest release encountered: Scintilla 5.6.5

The next review starts after the recorded changeset. Update this position after every completed review, including one that finds no fixes to incorporate.

## Incorporated fixes

### Avoid asynchronous machinery for single-threaded layout

- Upstream release: Scintilla 5.6.5
- Upstream changesets: `3e2cdc3aedcd86cd89f6e97f14918988a0a5a717`, `b9a388c1c6de9d5aee607a13fe067ddd7fe8e826`, and `ab90562730b73887d3cbcd2e5648252dbc3e30b3`
- Local areas: `src/EditView.cxx` and `src/EditorWrapping.cxx`
- Adaptation: run layout work directly when one worker is selected, avoid locking the line-layout cache on that path, and reserve `std::async` and `std::future` for multiple workers. Retrieve each future result so worker failures are not discarded.
- Reason: upstream reported crashes with some runtime libraries, while direct execution also removes needless work from the default single-worker path.
- Local constraint: current rendering surfaces report that width measurement is not thread-safe, so they always select one worker. The multiple-worker branch remains available for a future surface that supports concurrent measurement.

## Maintenance

- Put an incorporated fix, its ledger entry, and any focused test changes in the same local commit.
- Describe the behavior and reason in the commit subject and body. Name every adapted upstream changeset and explain material differences from upstream.
- Use full Mercurial changeset identifiers in this ledger. A local Git hash is unnecessary because the ledger's file history identifies the incorporating commit.
- Keep entries for incorporated fixes. Do not list routine cleanup or every reviewed-but-rejected upstream change.
- When a review incorporates nothing, commit only the updated review position with a subject such as `Record Scintilla review through <short-node>`.

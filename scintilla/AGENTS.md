# Scintilla instructions

## Upstream fixes

When reviewing or incorporating upstream Scintilla fixes:

- Record incorporated fixes and the most recently reviewed upstream changeset in `UPSTREAM_FIXES.md`.
- Commit each adapted fix with its ledger entry and focused tests. Name the upstream Mercurial changesets in the commit message, explain why the fix applies, and describe material local adaptations.
- Update the ledger review position after every completed upstream review, including a review that incorporates nothing.
- Do not catalog routine cleanup or every rejected upstream change.

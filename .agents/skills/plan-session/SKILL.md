---
name: plan-session
description: Plan one scalpel-editor ROADMAP.md session selected with a compact phase and step identifier such as p6s11. Use only when explicitly invoked to inspect the selected roadmap step, repository state, and relevant code, then propose an approval-ready sequence of commits without implementing it.
---

# Plan Session

Turn one numbered roadmap step into a concrete coding-session plan.

## Resolve the session

1. Read the selector following the skill invocation. Require exactly one selector in the form `p<phase>s<step>`, case-insensitively.
2. Locate the matching `## Phase <phase>` section and numbered step in `ROADMAP.md`.
3. If the selector is missing, malformed, absent from the roadmap, or ambiguous, stop and ask for a corrected selector.
4. If the selected step is marked complete, report that fact and stop. Do not silently choose another step.
5. Read the phase introduction, selected step, phase deliverable, and any earlier phase steps needed to understand the current baseline. Do not plan unrelated later steps.

## Investigate

1. Inspect `git status --short` and recent commit subjects. Preserve existing user changes and call out any overlap with the selected work.
2. Follow the repository's code-discovery guidance to find the implementation, tests, local headers, and source references relevant to the selected step.
3. Read enough of those files to base the plan on current code rather than roadmap wording alone.
4. Identify prerequisites, unclear requirements, and deficiencies in the existing framework. Ask a question only when its answer would materially change the plan; otherwise state the assumption.

## Propose the plan

Present an approval-ready plan with this structure:

```markdown
# Phase <phase>, step <step> — <roadmap step title>

<One short paragraph describing the intended outcome and current baseline.>

## Commits

1. `<imperative commit subject>`
   - Change: <cohesive implementation scope>
   - Verify: <smallest focused build and tests>

2. `<imperative commit subject>`
   - Change: <cohesive implementation scope>
   - Verify: <smallest focused build and tests>

## Final verification

- <required handoff checks from AGENTS.md and the roadmap>

## Assumptions or questions

- <only material assumptions, questions, risks, or framework deficiencies; omit this section when empty>
```

Make each commit independently coherent and reviewable. Use as few commits as the work naturally permits, but do not combine unrelated production code, tests, or documentation merely to shorten the plan. Include documentation and `ROADMAP.md` updates in the commit whose behavior they describe. Use simple commit subjects suitable for the repository's commit-message rules.

Distinguish focused iteration checks from the final workflow. Include the full sanitizer matrix only when `AGENTS.md`, the roadmap step, or the risk of the change requires it.

Stop after presenting the plan and ask for approval. Do not edit project files or begin implementation during this invocation. Read-only investigation is the default; refreshing a stale local discovery index, such as by running `scalps index`, is allowed.

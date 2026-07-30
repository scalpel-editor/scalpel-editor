---
name: plan-session
description: Plan one current scalpel-editor development topic without implementing it. Use only when explicitly invoked with a topic, local plan path, or item from `.plans/ROADMAP.md` to inspect current plans, repository state, and relevant code, then propose an approval-ready sequence of commits.
---

# Plan Session

Turn one current development topic into a concrete coding-session plan.

## Resolve the topic

1. Read the topic or plan path following the skill invocation.
2. When the user names a file under `.plans/`, read that file. Otherwise, read `.plans/ROADMAP.md` when it exists and look for one matching current or future item.
3. If the request and local roadmap do not identify one clear topic, stop and ask for a more precise selection.
4. Read any current architecture document needed to understand the intended boundary. Do not reconstruct completed milestones or use deleted phase records.

## Investigate

1. Inspect `git status --short` and recent commit subjects. Preserve existing user changes and call out any overlap with the selected work.
2. Follow the repository's code-discovery guidance to find the implementation, tests, and local headers relevant to the topic.
3. Read enough of those files to base the plan on current code rather than roadmap wording alone.
4. Identify prerequisites, unclear requirements, and deficiencies in the existing framework. Ask a question only when its answer would materially change the plan; otherwise state the assumption.

## Propose the plan

Present an approval-ready plan with this structure:

```markdown
# <Topic>

<One short paragraph describing the intended outcome and current baseline.>

## Commits

1. `<imperative commit subject>`
   - Change: <cohesive implementation scope>
   - Verify: <smallest focused build and tests>

2. `<imperative commit subject>`
   - Change: <cohesive implementation scope>
   - Verify: <smallest focused build and tests>

## Final verification

- <required handoff checks from AGENTS.md and the risk of the change>

## Assumptions or questions

- <only material assumptions, questions, risks, or framework deficiencies; omit this section when empty>
```

Make each commit independently coherent and reviewable. Use as few commits as the work naturally permits, but do not combine unrelated production code, tests, or documentation merely to shorten the plan. Include current documentation changes in the commit whose behavior they describe. Use simple commit subjects suitable for the repository's commit-message rules.

Distinguish focused iteration checks from final verification. Include the full sanitizer matrix only when `AGENTS.md`, the selected plan, or the risk of the change requires it.

Stop after presenting the plan and ask for approval. Do not edit project files or begin implementation during this invocation. Read-only investigation is the default; refreshing a stale local discovery index is allowed.

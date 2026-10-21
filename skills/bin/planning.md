# Skill: Planning
## When to use
For non-trivial tasks that involve 3+ distinct steps (multi-file changes,
multi-stage builds, refactors spanning several components). Skip planning
for simple single-file edits or quick lookups.

## Instructions
1. When the user gives a multi-step task, create a plan:
   - `TOOL:PLAN set <goal>` — state the goal in one sentence
   - `TOOL:PLAN add <step>` — add 3-7 concrete, verifiable steps
2. Work through steps in order. After completing a step:
   - `TOOL:PLAN advance` — mark it done and move to the next
3. If a step is no longer relevant:
   - `TOOL:PLAN skip <n>` — mark it skipped (0-indexed)
4. Check progress at any time with:
   - `TOOL:PLAN` — displays the current plan as markdown
5. When all steps are done, the plan is complete. Summarise the result.

## Rules
- Keep steps concrete and actionable ("Modify X in file Y", not "improve things")
- Do not add more than 7 steps — if the task is bigger, break it into sub-tasks
- Always advance the plan after completing a step — do not leave steps stuck on Active
- If the plan becomes stale (requirements changed), set a new goal and re-add steps

## SESSION.md
When calling TOOL:RESTART, the active plan is automatically appended to
SESSION.md. On restart, read SESSION.md and resume from the first
non-Done step.
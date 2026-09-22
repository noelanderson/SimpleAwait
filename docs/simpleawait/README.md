# SimpleAwait Code Generation Pack

Extract this archive directly into the root of the SimpleAwait repository.

Resulting layout:

```text
AGENTS.md
.github/
  copilot-instructions.md
  agents/
    REVIEW.md
  prompts/
    PRIMARY_AGENT_PROMPT.md
docs/
  simpleawait/
    SimpleAwait_Implementation_Spec.md
    ARCHITECTURE.md
    V1_API_CONTRACT.md
    IMPLEMENTATION_PLAN.md
```

## Agent entry points

Primary coding agent:

`/.github/prompts/PRIMARY_AGENT_PROMPT.md`

Independent competing-model reviewer:

`/.github/agents/REVIEW.md`

Repository-wide agent instructions:

`/AGENTS.md`

The canonical specification documents are under `/docs/simpleawait/`.

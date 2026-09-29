---
name: bearing-security-review
description: >-
  Use for security-sensitive changes, taint/source-to-sink questions, injection risk,
  path traversal, XSS, command/code/sql injection, auth/input/file/db/exec reviews.
---

# GitNexus Security Review

<!-- BEGIN GENERATED: graph-uncertainty — bearing regenerates this block; edits here are replaced on update -->
## The graph can be wrong

A zero is not absence; a near-0.5 `r.confidence` edge is a lead, not proof (~92% of `USES`); a count
can be a floor — `impact` says which in `epistemic`. Before a conclusion that matters, confirm with a
scoped `Grep` (allowed here, not a gate violation) and say which check you ran.
<!-- END GENERATED: graph-uncertainty -->


Use this when a task touches untrusted input, auth/session data, file paths, shell/process execution, dynamic code, HTML rendering, database queries, or external webhooks.

## Workflow

```
1. query({ search_query: "<feature/security surface>", task_context, goal: "sources sinks validators" })
2. context({ name: "<entry or sink symbol>", repo: "fo4-rapport" })
3. gitnexus_explain({ target: "<file-or-symbol>", repo: "fo4-rapport" })
4. gitnexus_pdg_query({ mode: "flows", target: "<function-or-file>", variable: "<inputVar>", repo: "fo4-rapport" })
5. gitnexus_pdg_query({ mode: "controls", target: "<function-or-file>", repo: "fo4-rapport" })
6. impact({ target: "<changed symbol>", direction: "upstream", mode: "pdg", repo: "fo4-rapport" }) when PDG layer exists
7. detect_changes({ scope: "unstaged", repo: "fo4-rapport" }) before done
```

If PDG/taint returns “no layer”, do **not** call the code safe. Say the repo needs `npm run bearing:pdg` / pre-commit PDG refresh, then fall back to graph + targeted reads.

## Tool routing

| Question | Tool |
| --- | --- |
| “Any taint findings here?” | `gitnexus_explain({ target })` |
| “Where does variable X flow?” | `gitnexus_pdg_query({ mode: "flows", target, variable })` |
| “What guard controls this sink?” | `gitnexus_pdg_query({ mode: "controls", target })` |
| “How can source A reach sink B?” | `gitnexus_trace({ from, to })` |
| “What is affected by changing this validator/sink?” | `impact({ mode: "pdg", direction: "upstream" })` |

## Reporting

Summarize:

1. Source(s) and sink(s) reviewed.
2. Taint findings found or “no taint layer / no persisted findings” with caveat.
3. Guards/sanitizers verified by PDG/control flow or source read.
4. Residual risk and tests to run.

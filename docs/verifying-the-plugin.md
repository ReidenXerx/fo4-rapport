# Verifying the plugin

Rapport ships an F4SE DLL, and "how do I know this binary is not malware" is a fair
question that argument cannot answer. This is what is actually in place, what it proves,
and — more importantly — what it does **not** prove yet.

## What exists now

**The source is public.** GPL-3.0, `github.com/ReidenXerx/fo4-rapport`, all of it.

**Every commit is compiled in public.** `.github/workflows/build.yml` builds the plugin on a
clean GitHub-hosted runner from that exact commit, prints the resulting DLL's SHA256 into
the job summary, and uploads the DLL as an artifact. The log is public and cannot be edited
after the fact.

**Chemistry has no binary at all** — one Papyrus script and one generated ESP. There is
nothing there to be suspicious of, and its page now says so.

## What this does NOT prove, and it matters

**The hash in the build log does not match the DLL on the Nexus page.** Measured, not
assumed:

| | size | sha256 |
| --- | --- | --- |
| local build, shipped as 0.1.1 | 1,192,960 | `ca6a46a8…` |
| CI build of the same source | 1,192,960 | `83f18919…` |

Identical size, and **83% of the bytes differ**. That is not a timestamp — it is genuinely
different code generation, from a different MSVC toolset on the runner than the one on the
author's machine. (The CI file's PE `TimeDateStamp` reads as 1993, which is MSVC's
deterministic-build marker rather than a real date.)

So today the CI build proves *the source compiles cleanly in public and here is what that
produces*. It does **not** let a user hash their download and compare. Claiming otherwise
would be the exact kind of unverifiable assurance this whole exercise exists to replace.

## The fix, and it is small

**Ship the CI artifact.** If the DLL published to Nexus *is* the file the public build
produced, then the hash in the build log is the hash of the download, and the claim becomes
checkable by anyone. That means `make-release.ps1` takes the artifact from the build rather
than from `build/Release/`, and the release is cut from a tag.

Until that lands, the honest wording is the one now on the mod page: source, build machine
and result are all visible. Not: "verify your download against this hash."

## VirusTotal — check before publishing, do not publish blind

A VT link is worth having **only if it comes back clean**. F4SE plugins hook a running
process and routinely trip heuristic detections; a link showing "2/70" is considerably worse
than no link at all, because it hands the accusation a citation.

So: upload it, look at the result, and link it only if it is clean. Never link it
unexamined. The file is not currently in VT's database, and uploading needs an account, so
this one is a manual step by design.

## Gotchas from setting the workflow up

- **A PowerShell here-string cannot live in a YAML block scalar.** Here-string content must
  sit at column 0, and column 0 terminates the scalar — the first version of the workflow
  was unparseable, which GitHub reports as a failed run with *no log and no jobs*. Build
  multi-line output from a string array instead. **Validate the YAML locally before
  pushing**; the round trip to discover this was entirely avoidable.
- **Backtick-dollar escapes in PowerShell double-quoted strings.** A markdown code span
  around `$h` prints the literal text, not the hash — a green build publishing a useless
  summary, which is worse than a red one.
- **`^{commit}` unquoted is a PowerShell script block.** Quote any commit-ish.
- **The runner's vcpkg is shallow AND at HEAD**, which are two separate failures.
  Shallow means the pinned baseline is unreachable and nothing resolves. At HEAD means the
  ports tree is newer than the pinned version database, so a port carries a version the
  baseline never heard of (`rapidcsv 9.07` against a database ending at 8.99). Deepening
  fixes only the first. **Check the whole tree out AT the baseline and re-bootstrap** — and
  never "fix" this by dropping the pin, because an unpinned baseline makes the published
  hash unreproducible, which is the entire point of publishing it.

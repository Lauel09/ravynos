# <topic — the gap, not the technology>

**Opened:** YYYY-MM-DD
**Nominated by:** <build failure / boot failure / API mismatch / in-tree TODO>
**Status:** open | answered | answered / no change needed

## Gap

One sentence. If it needs two, the investigation is two investigations.

> Example: "IOServiceOpen on a user client returns kIOReturnNotPrivileged in
> ravynOS where macOS returns kIOReturnSuccess, so no kext user client works."

## Subject

What was analysed, and why it is the smallest subject that answers the
question. Prefer a probe we wrote over an Apple binary; prefer one Apple binary
over a framework.

| Subject | Path | sha256 | Why |
|---|---|---|---|
| probe | `probe/probe.c` | — | exercises exactly the API in question |
| framework | `/System/Library/Frameworks/…` | `…` | the dispatch table the probe reaches |

## Evidence

| id | file | shows |
|---|---|---|
| e1 | `evidence/probe-run-macos.json` | probe's observed behaviour on macOS |
| e2 | `evidence/iokit-open.json` | the call path through the framework |

Provenance for each file is recorded in `evidence/PROVENANCE.md` (subject path,
sha256, tool versions, exact command).

## Answer

The behaviour, stated as a claim that could have been wrong. Include the
observation that would have falsified it.

> Example: "IOServiceOpen checks the caller's task entitlement before the user
> client's external method runs; ravynOS never performs that check, so the
> default-deny path is skipped. A probe without the entitlement succeeding on
> macOS would have falsified this; it does not."

## Change

The exact change in the fork, or "no change needed".

| Files | Commit | Summary |
|---|---|---|
| `Frameworks/IOKit/…` | `abc1234` | check the entitlement before dispatch |

## Verify

How the change was checked (build, probe re-run, boot marker), and what remains
unverified — typically "behavioural confirmation waits for Phase 2 userspace".

---

### Checklist before closing

- [ ] the gap sentence is still the one that was nominated
- [ ] every claim in **Answer** points at an evidence id
- [ ] provenance recorded for every Apple-side subject
- [ ] change committed with `re: work/<topic>` in the message, or a written
      no-change decision
- [ ] `PROGRESS.md` updated with the outcome

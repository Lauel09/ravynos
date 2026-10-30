# Reverse-engineering workspace

This directory holds the evidence trail for Phase 4: the loop that turns a
*specific observed gap* into a fix in the fork, using this Mac as ground truth.

The doctrine is deliberately narrow. Scope creep is the usual failure mode of
reverse-engineering work, so the rules are fixed and not negotiable per
investigation.

## Doctrine

1. **No standing targets.** An investigation starts only when a Phase 1/2/3
   failure or a compatibility gap nominates it, and the nomination is written
   down first, in one sentence, before any tool runs.
2. **Smallest subject that answers the question.** In order of preference:
   1. a self-authored probe (a 50-line Mac program that exercises exactly the
      API or behaviour in question) plus the Apple framework path it reaches;
   2. a single small Apple binary, when cloning one utility's behaviour;
   3. data formats (plists, keyed archives, bundle layouts) via tiny samples;
   4. an Apple IPS crash report, when the question surfaced via a failure.
3. **Open source gets read, not decompiled.** XNU, dyld, launchd,
   CoreFoundation and libdispatch are all readable at source. Decompiling them
   is slower, harder to justify, and produces worse conclusions.
4. **Only software we are entitled to inspect.** Nothing else.
5. **Every investigation closes.** It ends in a commit that references the
   note, or in a written "no change needed" with the reason. There is no third
   option, and nothing stays "in progress" indefinitely.

## Layout

```
work/
├── README.md                 this file
├── _template/                copy this to start an investigation
│   ├── NOTES.md
│   ├── probe/
│   └── evidence/
└── <topic>/
    ├── NOTES.md              question, evidence, conclusion, decision
    ├── probe/                the smallest subject (our own code)
    └── evidence/             rea output: *.json + provenance
```

A directory is named after the gap, not the technology:
`work/kext-loading-no-kextd/` is good; `work/kexts/` is not.

## Evidence format

`rea` writes JSON into `evidence/`. Every evidence file needs provenance, either
in `evidence/PROVENANCE.md` or as a sibling `.provenance.json`:

```json
{
  "subject": "/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit",
  "sha256": "…",
  "source_host": "macOS 26.3.1, arm64e",
  "tool": "rea 6.7.0 + Ghidra 12.1.4",
  "command": "rea analyze --subject … --out …",
  "date": "2026-10-30",
  "question": "which dispatch table entry serves IOServiceOpen for user clients?"
}
```

Without that, the evidence is a screenshot: convincing, unfalsifiable and
useless next month.

## The note (`NOTES.md`)

Six sections, no more:

```markdown
# <topic>

## Gap
One sentence, with the failure that nominated it.

## Subject
What exactly was analysed, and why it is the smallest thing that answers
the question.

## Evidence
Evidence ids + what each shows. Paths under evidence/.

## Answer
The behaviour, stated as a claim that could have been wrong.

## Change
The exact fork change (files, commit) or "no change needed" plus why.

## Verify
How the change was checked, and what remains unverified until Phase 2
boots userspace.
```

## Nominations

Nominated gaps live in `NOTES.md` files as they are opened. Nothing is
"nominated but not started" for more than a week: it either starts or it is
deleted.

Current nominations: none. Phase 1/2 have not yet produced a gap that needs
Apple-side evidence — the build failures so far are all host assumptions,
documented in `PROGRESS.md` §3.2.

## Housekeeping

* Evidence is text/JSON, small, and committed. Large binaries and Ghidra
  projects are not committed; record the subject path and hash instead.
* Nothing in `work/` is built or installed. It is documentation with proof
  attached.

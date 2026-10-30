# Provenance

Every evidence file in this directory needs an entry. Without one it is a
screenshot, not evidence: it cannot be re-derived, checked, or trusted later.

| file | subject | sha256 | tool | command | date |
|---|---|---|---|---|---|
| `example.json` | `/System/Library/Frameworks/Example.framework/Versions/A/Example` | `<sha256>` | rea 6.7.0 + Ghidra 12.1.4 | `rea analyze --subject … --out example.json` | YYYY-MM-DD |

Notes:

* `sha256` is of the subject as it exists on the analysis machine, so a future
  reader can tell whether Apple has changed it since.
* Record the **source host** (macOS version, architecture) in the tool column
  when it matters — behaviour differs between arm64e and x86_64.
* Our own probes do not need a hash, but they do need the commit they were
  built from.

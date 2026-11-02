# efsw (Windows): use-after-free in `DestroyWatch` + double-armed IRPs in `WatchCallback`

> Material prepared for an upstream issue/PR to `SpartanJ/efsw`.
> Patch base: efsw **1.4.1** (klogg pins `GIT_TAG 1.4.1` via CPM). Both defects are
> still present in `master` as of 2026-08 (`DestroyWatch` and the zero-byte re-arm
> path are unchanged; the `Extended`/`StopNow` additions do not affect them).

## Summary

On Windows, klogg (a log viewer using efsw for file monitoring) suffered silent
heap corruption at a **30–43% rate** in test runs that rapidly attach/detach files
(each attach/detach adds/removes a watch on the same directory). Two independent
defects in efsw's Win32 backend combine into a use-after-free:

1. `DestroyWatch` frees the `OVERLAPPED` structure and the 63 KB notify buffer
   **before the kernel has completed the cancelled IRP** — the kernel writes both
   at IRP completion time, into heap blocks that have already been reused.
2. `WatchCallback` **arms two concurrent IRPs on the same `OVERLAPPED`** after a
   zero-byte completion, making the race window much wider.

With both fixed, crash rate drops from 30–43% to ~0.3% (1 residual failure in 320
runs, unrelated signature), verified by 100+100 loop runs and 120 ASAN runs.

## Bug 1 — `DestroyWatch` frees OVERLAPPED + buffer before IRP completion

`src/efsw/WatcherWin32.cpp` (upstream 1.4.1 and master):

```cpp
void DestroyWatch( WatcherStructWin32* pWatch ) {
    if ( pWatch ) {
        WatcherWin32* tWatch = pWatch->Watch;
        tWatch->StopNow = true;
        CancelIoEx( pWatch->Watch->DirHandle, &pWatch->Overlapped );
        CloseHandle( pWatch->Watch->DirHandle );
        efSAFE_DELETE_ARRAY( pWatch->Watch->DirName );
        efSAFE_DELETE( pWatch->Watch );   // <-- owns Buffer + Overlapped
        efSAFE_DELETE( pWatch );
    }
}
```

`CancelIoEx` only *requests* cancellation; the IRP completes **asynchronously**
(potentially much later when minifilter drivers — e.g. antivirus — are in the I/O
stack). At completion the kernel writes the `OVERLAPPED` and the notify buffer.
By then both are freed and typically already reused (in our dumps: Qt
`ConnectionData` and `QString` buffers) → silent heap corruption, detected later
at arbitrary allocation points (`STATUS_HEAP_CORRUPTION`, 0xC0000409 fastfail).

This explains the classic signature: detection point random; single watch/unwatch
cycles in a short-lived process never crash (nobody touches the poisoned block);
dense add/remove sequences crash at high rates.

### Rejected alternative: synchronous wait

A first attempt used `CancelIoEx` + `GetOverlappedResult(..., TRUE)` to wait for
the IRP. This **deadlocks** in the wild: an antivirus minifilter can hold the
cancel for a long time while the efsw `run()` thread is simultaneously blocked on
`mWatchesLock` held by the waiting thread (captured in a hang dump: worker stuck
in `GetOverlappedResult`, efsw run thread stuck on `mWatchesLock`). A deferred
("graveyard") approach is strictly safer: worst case is a bounded leak until the
completion arrives, never a hang, never a use-after-free.

### Fix

- `DestroyWatch`: after submitting the cancel, move the watch into a graveyard
  list (`FileWatcherWin32::deferWatchDestruction`) when a cancel was submitted
  (or any error other than `ERROR_NOT_FOUND` occurred). Only when nothing was
  pending (`ERROR_NOT_FOUND`) are the structures freed immediately.
- The efsw `run()` thread reaps the cancelled IRP's final completion packet from
  the IOCP and only then frees the structures (`finalizeWatchDestruction`). The
  run loop is extended to also handle `res == FALSE` completions with
  `lpOverlapped != NULL` (cancelled IRPs complete with an error code — the
  upstream loop discarded these packets).
- `FileWatcherWin32.hpp` gains `mWatchGraveyard` + the two methods; locking via
  the existing `mWatchesLock`.

## Bug 2 — `WatchCallback` arms two IRPs on one OVERLAPPED

`src/efsw/WatcherWin32.cpp` (upstream 1.4.1 and master):

```cpp
if ( dwNumberOfBytesTransfered == 0 ) {
    ...
    RefreshWatch( tWatch );          // (1) re-arm in the zero-byte branch
}
...
if ( !pWatch->StopNow ) {
    RefreshWatch( tWatch );          // (2) re-arm again at function end
}
```

A zero-byte completion (buffer overflow / lost events — common under event
storms on busy directories like `%TEMP%`) takes both paths, leaving **two
concurrent IRPs pending on the same `OVERLAPPED` + buffer**. A later
`DestroyWatch` can then reap at most one of them; the survivor completes into
freed heap (bug 1 amplifier). The two alternating completions also feed
duplicated/garbled event data to the parser.

### Fix

- Re-arm **exactly once** per completion: skip parsing when
  `dwNumberOfBytesTransfered == 0` (lost events are covered by the caller's own
  polling fallback / `handleMissedFileActions` in master) and keep a single
  `RefreshWatch` at the end.
- Early-return when `pWatch->StopNow` is set (a cancelled watch must not re-arm).

## Reproduction

Scenario that triggers it reliably (klogg test-suite pattern):

1. Create files in a busy directory (`%TEMP%` works well — constant background
   change storms).
2. In a loop: `addWatch(dir)` → small delay → `removeWatch(dir)` (klogg does this
   per opened/closed log file; ~1.4M-line test files index in ~100 ms, so
   add/remove cycles are milliseconds apart).
3. Run the loop a few hundred times on a multi-core machine. Without the fix:
   ~30–43% of suite runs die with heap corruption. With the fix: 200/200 clean.

## Verification (klogg tree, efsw 1.4.1 + patch)

| Check | Result |
|---|---|
| Baseline (unpatched) | [event] 9/30 crashed, [api] 13/30 crashed |
| Control (native watch disabled) | 0/60 |
| Patched | 100×[event] + 100×[api] = 0 failures; final 30×+30× = 0 |
| AddressSanitizer (+ RtlAllocateHeap hooks) | 60×[event] + 60×[api], no reports |
| Hang watchdog (90 s × 260 runs) | no hangs |

## Patch contents

Vendored in `3rdparty/patches/efsw-1.4.1/` (klogg overlays these onto the
CPM-fetched tree at configure time):

- `WatcherWin32.cpp` — `DestroyWatch` graveyard deferral; `WatchCallback`
  single re-arm + `StopNow` early return (patch comments marked `klogg patch:`)
- `FileWatcherWin32.cpp` — `deferWatchDestruction` / `finalizeWatchDestruction`;
  run loop reaps `res == FALSE` completions and finalizes graveyard entries
- `FileWatcherWin32.hpp` — `mWatchGraveyard` + method declarations

## Bug 3 — stale completion packet misattributed to a recycled watch address (v2, 2026-08-29)

The graveyard fix above still contained a residual use-after-free (~0.3–1.4% of
suite runs). Root cause:

`DestroyWatch`'s immediate-free fast path fired whenever `CancelIoEx` reported
`ERROR_NOT_FOUND`, i.e. the IRP had already completed **but its completion packet
could still be queued in the IOCP**. Freeing the watch then:

1. returned the `WatcherStructWin32`/`OVERLAPPED` memory to the heap while the
   stale packet (`lpOverlapped` = that address) was still in flight, and
2. allowed a *subsequent* `CreateWatch` to reallocate the **same address** for a
   new watch (same-size allocations recycle reliably on the CRT heap).

When the `run()` thread finally dequeued the stale packet, its membership test
`mWatches.find(watch)` matched the **new** watch at that address and ran
`WatchCallback` for it, arming a **second** IRP on the new watch's `OVERLAPPED`.
From then on the watch had two pending IRPs; at its own destruction the
graveyard reaped only the first completion packet and freed the structures, and
the surviving IRP completed into freed heap — the kernel writing a
`FILE_NOTIFY_EXTENDED_INFORMATION` record into memory already reused by other
allocations. In klogg this surfaced as fastfail/AV/bad_alloc at arbitrary later
points (ASAN-invisible: the write is performed by the kernel, not by code).

Locking does not save this: `mWatchesLock` only guarantees the stale packet is
processed *after* the new watch is registered.

### Fix (v2)

Remove the immediate-free fast path: `DestroyWatch` **always** defers to the
graveyard. The graveyard holds the struct (and therefore its address) until the
watch's final completion packet is reaped, so a stale packet can only ever match
the graveyard entry — never a live watch — and every watch carries at most one
pending IRP, keeping the reap accounting exact.

Trade-off: a watch whose last completion failed (`res == FALSE`, no re-arm, no
IRP, no packet) never gets reaped and leaks its graveyard entry once. That path
is practically unreachable for live directory handles, bounded, and strictly
better than a use-after-free.

### Verification of the v2 fix (klogg tree)

- Baseline with v1 patch: 5/800 runs crashed (natural heap), ~27% under the NT
  debug heap; debug heap reported `Free Heap block ... modified after it was
  freed`, and full-memory dumps showed the overwritten bytes to be a
  `FILE_NOTIFY_EXTENDED_INFORMATION` record (4 identical FILETIMEs + the watched
  file name) — a kernel write into freed heap.
- Control with native watch disabled: 500/500 clean.
- With v2: 400/400 (natural heap) + 150/150 (debug heap) + 100/100 API suite —
  zero crashes.

## Note for upstream PR

- `master` additionally calls `handleMissedFileActions` in the zero-byte branch —
  keep that call; only remove the *second* re-arm.
- The graveyard fix is platform-local (Win32 backend only) and does not change
  the public efsw API.
- klogg's own deadlock fix (unrelated to efsw) is *not* part of this material.

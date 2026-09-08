# Browser database transaction transport

The host has a real atomic database-image store, and Nitmod now has the
corresponding asynchronous mod queue, copied completion contexts, and staged
commit/rollback integration. Runtime activation remains **disabled** until the
engine lifecycle that drains before destructive module transitions is approved
and integrated. Automatic approval review rejected that lifecycle mutation;
it is provided as a separate reviewable patch, not silently applied.

`trap_NitmodDatabaseStorage1` is negotiated through the existing engine extension
mechanism. Without `NITMOD_WEB_DB_LIFECYCLE`, its support operation returns zero
and all data operations return failure. The module consequently retains its
prior storage mode. Defining a capability without the real drain lifecycle is
not a valid way to enable the feature.

## Implemented contract

`src/web/database_storage.js` is included in the engine glue by `--pre-js`.
Its default IndexedDB database is `etlegacy-database-images-v1`, object store
`images`. This database is separate from all IDBFS mounts: an old tab's
`FS.syncfs(false)` cannot overwrite another tab's database transaction.
Keys identify the engine's writable file namespace. `FS_WebDatabaseKey()` uses
exactly `FS_BuildOSPath(fs_homepath->string, fs_gamedir, qpath)`, as
`FS_FOpenFileWrite()` does, and rejects absolute/traversal/truncated qpaths.
Repeated homepath separators are normalized; game directories remain distinct.
The IndexedDB namespace is shared by same-origin pages in the same browser
storage partition. It does not synchronize different origins, private browser
profiles, devices, or a remote native game server.

A record contains an integer revision and an owned byte image. Revisions start
at one; absence is revision zero; overflow is refused. The maximum image size
is 64 MiB, with eight outstanding retained requests per service instance.
SQLite owns image integrity/schema checks; the host validates only the image
size/signature and the storage record shape.

A compare-and-swap operation executes get, revision comparison, and put inside
one IndexedDB `readwrite` transaction with `durability: "strict"`. Only the
transaction's `oncomplete` callback reports `committed`; `put.onsuccess` does
not acknowledge persistence. Conflicting revisions return the current image
and revision without writing. Aborts/errors never report commit. A browser
that cannot provide the requested strict durability reports an error instead
of silently weakening the contract. This is browser-managed durability, not
protection against explicit browser data deletion or storage eviction.

## Engine API

The functions in `sys_web.c` remain asynchronous despite their C signatures:

| Function | Contract |
| --- | --- |
| `Sys_WebDatabaseRequest(op,qpath,revision,image,length)` | `op=1` reads, `op=2` compares/writes. Returns a positive request token or zero if rejected; a token is never commit success. Input bytes are copied before returning. |
| `Sys_WebDatabasePoll(token,&revision,&length,error,errorSize)` | `0` pending, `1` completed read, `2` committed, `3` revision conflict, `-1` error, `-2` cancelled, `-3` unknown token. |
| `Sys_WebDatabaseCopy(token,image,capacity)` | Copies the completed owned result; returns its length or `-1`. Short destinations are refused. |
| `Sys_WebDatabaseRelease(token)` | Releases only completed results; pending outcomes cannot be discarded. |
| `Sys_WebDatabaseCancel(token)` | Requests a real transaction abort. The caller must still poll its final outcome; completion may already have won the race. |

The current engine runs on the browser's main thread without Asyncify.
`cmake/ETLEmscripten.cmake` documents prior SDL audio callback reentrancy as
its reason for excluding Asyncify. Its deployment also intentionally works
without COOP/COEP or SharedArrayBuffer. Neither was changed. The older
`queueSyncFs()` in `shell.html` only serializes this tab's IDBFS work.

## Required mod connection â€” still outstanding

A thin `Flush()` wrapper cannot safely connect this API: existing callers
immediately make success, rollback, privilege, kick/mute, and XP decisions from
its boolean return value. Returning true at request creation would announce
uncommitted data; returning false while allowing the request to commit would
persist an operation its caller has rolled back.

Nitmod implements this connection in `g_nitmod_database_async.c`, with the
storage/cache adapter in `g_nitmod_database.c`. Commit adopts BEFORE, captures
AFTER, restores BEFORE immediately, copies callback context, then processes a
serial queue. The callback alone authorizes success effects. Native storage
still completes synchronously through the same callback interface.

The queue has these phases:

1. Capture an operation's GUIDs, arguments, before-image, and client connection
   generation; create its changed image in a separate staging database. Keep
   the visible committed cache unchanged while pending. Queue subsequent DB
   mutations instead of permitting unrelated writes into that staging image.
2. Request the current host image. On the first import, initialize it with
   expected revision zero; competing initializers use the winning image.
   Retain the source/generation imports as immutable inputs. After adoption,
   this IndexedDB store must be authoritative for reads and writes; do not
   continue writing an independent IDBFS generation head.
3. Merge staged changes against the fresh image using the existing typed
   `NITMOD_DBMergeImages()` / `NITMOD_DBMergeUserImages()` rules. Issue CAS with
   that image's revision. A revision conflict means read/merge/retry; a same
   field/schema conflict is a rejected operation, never last-writer-wins.
4. Poll from the server frame. After confirmed commit, install the committed
   account data and run the saved success continuation. Before that point,
   issue no success message or dependent privilege/kick/mute/XP effect.
5. On abort/storage/merge error, discard the staging image and run the existing
   failure continuation. Because the committed cache was not changed, failure
   does not require pretending a pending host write was rolled back.

Concrete continuation boundaries in Nitmod are:

| Existing boundary | Required deferred behavior |
| --- | --- |
| `g_nitmod_accounts.c` Persist, AccountSave, AccountsSaveAll | Capture account/XP data by GUID. Apply committed cache updates and success-dependent state only after completion; disconnect saves must not reference a reused client slot. |
| AccountUserinfo -> DatabaseSyncUser -> first RestoreXP | Hold first account/XP activation until the read/merge finishes. A connection-generation guard rejects stale continuations after disconnect/reconnect. Repeated userinfo is not a second XP load. |
| `g_nitmod_admin.c` PenaltyStore, delrecords, dbsave | Store ban/mute/records mutations before their successful command continuation. No kick/mute or success print on `pending`. |
| Admin level migration and later configuration-file failure | Preserve the existing two-step rollback contract: if the later file operation fails, the inverse database update is itself a new CAS transaction and must finish before reporting rollback complete. A blind old-image replacement would destroy other instances' later changes. |
| `g_nitmod_nxac.c` ban persistence | Persist first, then continue the sanction according to the existing failure policy; keep the slot-generation/identity guard. |
| `g_nitmod_records.c` update | Commit staged record changes before exposing them as saved. |
| SaveAs/load/shutdown | Reserve a new key with CAS revision zero. Engine-controlled map/module shutdown must drain pending operations before unloading their continuations. Browser `beforeunload` cannot promise completion of asynchronous writes; only already confirmed commits are durable. |

The queue and mod continuations are implemented. `DatabaseCommit()` and
`DatabaseSubmitImages()` consume their SQLite-export arguments on every path;
return values are 0 failed, 1 completed, 2 pending, with exactly one completion
callback. Cache installation preserves account epoch and working generation.
SaveAs is a path barrier: it requires an empty queue and rejects new operations
until its result establishes the active path. The queue allows
`2 * MAX_CLIENTS + 16` operations under a separate 128 MiB image budget; a single
host transaction is active at a time. A queued sync for an absent GUID serves
as a boot-ready waiter without inventing an account.

The remaining activation gate is the reviewed engine lifecycle patch. It must
capture shutdown XP exactly once, stop new submissions, drain existing and
callback-generated compensation operations, and only then destroy/restart the
VM. Abnormal unload uses `Sys_WebDatabaseReset()`: active requests are aborted
where still possible; already committed transactions remain committed. JS owns
its buffers and never asynchronously writes into old VM memory. Until the
lifecycle patch is integrated, the explicit capability gate remains off and
the prior WASM snapshot mode retains its documented cross-instance limitation.
Native SQLite remains synchronous.

## Focused offline acceptance

`tools/web-smoke/database-storage.mjs` loads the production JS in two blank
same-origin browser pages. No wasm game engine, map, player, retail asset,
real user database, or deployment is involved. It verifies concurrent CAS
winner/conflict behavior, retry, cross-tab reads, reload persistence, actual
queued-transaction cancellation, owned input bytes, capacity bounds, and
explicit pending states.

`build-database-storage-c.py` extracts the exact current function bodies of
`FS_ReplaceSeparators`, `FS_BuildOSPath`, `FS_WebDatabaseKey`, and the five C API
functions into an isolated WASM fixture. Only their engine environment
(globals and simple string helpers) is controlled. Their production code is
not copied into a second maintained implementation. The browser test then
calls this WASM adapter and reads its committed result from the other page.

Observed result: **31 storage/C checks and 14 filesystem-key checks passed** in
Chromium 136.0.7103.25 without cross-origin isolation. Source hashes and command
lines are recorded in `build/database-storage-c/acceptance.json` and
`source-manifest.json`. The engine-wide build is separate from this acceptance.

```sh
python tools/web-smoke/build-database-storage-c.py --emcc /path/to/emcc
node tools/web-smoke/database-storage.mjs build/database-storage-c
```

The existing Playwright dependency is used. `ETL_CHROMIUM_EXECUTABLE` can select
an already installed Chromium binary; no browser installation is required by
the fixture itself.

## Standards basis

IndexedDB serializes overlapping readwrite transactions and commits their
changes atomically: [W3C IndexedDB transaction scheduling](https://www.w3.org/TR/IndexedDB-3/#transaction-scheduling).
The strict durability request is part of the
[W3C transaction options contract](https://www.w3.org/TR/IndexedDB-3/#dom-idbtransactionoptions-durability).
These guarantees apply to the transaction service described above; they do not
turn a synchronous mod return value into an asynchronous commit continuation.

## Mod integration acceptance (2026-09-08)

The Nitmod repository contains `tools/run_nitmod_database_async_checks.py`:
`--mode controlled` executes the actual SQLite/cache/queue code with deliberately
delayed host responses, including abort/error/conflict/rollback paths.
`--mode browser` executes two actual SQLite/mod-queue WASM instances in blank
browser pages against this production IndexedDB service. Only engine imports
and cvars are synthetic; persistence uses real browser transactions. It checks
concurrent initialization, disjoint commits, same-field conflict failure,
SyncUser, and a fresh third module loading committed data. Neither mode starts
a game or activates the gated product lifecycle.

Exact final counts, source hashes, and run commands are in Nitmod's
`build/database-async-fixture/controlled-manifest.json`, `browser-manifest.json`,
`controlled-output.log`, and `browser-result.json`. The historical official
`nitmod_database_test` also passed on Windows x64 and x86 without changing its
assertions.

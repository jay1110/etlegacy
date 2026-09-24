# Omni-bot distribution review

Last reviewed: 2026-09-24

The web package distributes `omnibot_et.wasm32.so` and
`omni-bot-data.zip`. This review follows the actual Emscripten Ninja object
graph, not every directory present below `vendor/omni-bot`.

## Confirmed compiled components

| Component | Evidence | Terms found | Status |
| --- | --- | --- | --- |
| Omni-bot 0.8x/0.93 source snapshot | `vendor/omni-bot/source/Omnibot` | The upstream `Omnibot/License.txt` contains only `File: Omni-bot Licence` | permission/provenance unresolved |
| GameMonkey Script 1.26 | `gmMachine.h`; objects in the Ninja graph | MIT-style permission notice in `gmMachine.h` | notice required |
| PhysicsFS | PhysicsFS objects in the Ninja graph | zlib-style licence in `dependencies/physfs/LICENSE.txt` | notice required |
| PhysicsFS zlib 1.2.3 copy | zlib objects in the Ninja graph | zlib terms in `dependencies/physfs/zlib123/README` | notice required |
| LZMA SDK 4.57 decoder | LZMA objects in the Ninja graph | multiple options and static-link exception in `dependencies/physfs/lzma/lzma.txt` | retain full terms; modification check required |
| Wild Magic 3 subset | Wild Magic objects in the Ninja graph | file headers refer to an external WildMagic3 licence and prohibit copying except under that agreement | permission/terms unresolved |
| gmbinder2 and mathlib | objects in the Ninja graph | embedded notices need consolidation | review |

SQLite and Recast source trees are present but are not compiled into the
current `omnibot_et.wasm32.so` according to the generated Ninja graph.

## Runtime data

The distributed data archive contains GameMonkey scripts and navigation data.
`vendor/omni-bot/data/README.txt` identifies their origin but does not provide
complete redistribution terms or author/source records for each data class.
The data archive therefore remains a separate provenance and permission item.

## Material unresolved issues

1. Obtain an authoritative licence for the exact Omni-bot source snapshot.
   The 26-byte placeholder is not a usable grant or notice. Git history shows
   the placeholder already existed in the initial import
   `771b16ad0eab8a39dd460ae2688699a3b30dfd16` (2010-08-26) and remained in the
   stable-branch import `0b114a2e6912a215eb3af1436715565e37baf431`
   (2013-03-04). The original `omni-bot-0.93.zip` contains dependency and game
   SDK licences, but no separate main Omni-bot licence.
2. Obtain an authoritative copy of the Wild Magic 3 version 1.0c licence and
   preserve it with the source. A contemporary Debian legal-list archive
   reproduces terms allowing use, modification, copying and distribution for
   non-commercial products, which appears consistent with this deployment,
   but the original PDF URL is no longer available and the repository does
   not contain the agreement itself.
3. Establish redistribution terms and provenance for the shipped ET scripts
   and navigation meshes.
4. Determine whether the vendored LZMA sources were modified. If modified,
   select and fulfil either LGPL or CPL obligations rather than relying on the
   unmodified-code linking exception.
5. After those questions are resolved, generate an Omni-bot-specific notice
   file and include it beside the side module and data ZIP.

Per the operator's instruction, this review does not disable Omni-bot. Its
release status remains `permission-needed` until the two missing grants and
the runtime-data provenance are resolved.

## Outreach prepared

- `outreach/omnibot-licence-request.md` is ready to post as an issue in the
  authoritative Omni-bot repository.
- `outreach/wildmagic3-licence-request.md` is ready to email to Geometric
  Tools. No message has been sent from this repository review.

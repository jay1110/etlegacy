# ET: Legacy Web compliance work

This directory tracks the provenance, licences, notices and distribution
obligations of the ET: Legacy Web build. It is an engineering aid, not legal
advice and not a declaration that every listed component has already been
cleared for distribution.

No game, mod or map is disabled merely because its review is incomplete. The
`reviewStatus` field records what still needs to be established so that those
questions can be resolved deliberately.

## Files

- `distribution-surfaces.yml` lists every known public service and download
  surface that forms part of the deployment.
- `operations.yml` records non-publication-sensitive facts about the operator
  and the outstanding provider-information/privacy review.
- `data-flows.md` is the technical data-flow register used to prepare an
  accurate privacy notice and retention policy.
- `components.yml` is the central component and licence review register.
- `dependencies.yml` records code dependencies, their exact use in the web
  build or production services, and dependencies explicitly excluded from the
  current Emscripten build.
- `distribution-inventory.json` is generated from a concrete web build and
  records every distributed file, its size, SHA-256 digest and classification.
- `../tools/compliance/generate-distribution-inventory.mjs` generates the
  inventory without modifying the build.

## Generate an inventory

Build or assemble the web distribution, then run:

```sh
node tools/compliance/generate-distribution-inventory.mjs \
  dist/etlegacy-web compliance/distribution-inventory.json
```

For a local CMake build the first argument can also be `build-wasm`. The script
only reads that directory and writes the requested JSON file.

## Review status

- `verified`: provenance, applicable licence and distribution obligations have
  been checked and documented.
- `review`: the component is known, but one or more obligations still need to
  be checked.
- `permission-needed`: distribution depends on permission or a legal answer
  that has not yet been obtained.
- `unknown`: the applicable licence or provenance is not sufficiently clear.
- `blocked`: a known term appears to prohibit the intended distribution. This
  is a review status only; product changes require a separate decision.

## Completion rule

A component can become `verified` only when the register identifies its exact
source/version, all applicable licence and notice files, local modifications,
the distributed artifacts, and how the corresponding source is provided.

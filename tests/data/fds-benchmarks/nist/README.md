# NIST FDS benchmark cases

These inputs are copied without modification from the official
[`firemodels/fds`](https://github.com/firemodels/fds) repository.  The pristine
files remain in each case's `source` directory.  FireCAE calculations must use
a copy in the sibling `firecae-run` directory so generated results never alter
the source baseline.

## `simple_test`

- Upstream: `Verification/Fires/simple_test.fds`
- Purpose: fast end-to-end regression for mesh, reaction, burner, obstruction,
  open vent, boundary output, vector slice, solver launch, result discovery and
  Smokeview playback.
- Source SHA-256:
  `DC9CF35913691DBB297793BCE13243E374FD4E77018DAB19308D30E73F164B2E`

## `couch`

- Upstream: `Verification/Fires/couch.fds`
- Purpose: broader feature benchmark covering eight repeated meshes, species
  and reaction definitions, three materials, multilayer pyrolysis, burn-away
  upholstery, several obstructions, static ignition particles, an open vent,
  five boundary quantities and two slices.
- Source SHA-256:
  `83A0B5DE88162CD1AB3D642F470F7F00975A7AEECDCE6AD9EDA466798354CBD8`

The upstream file states that its fabricated material properties make it a
teaching and functionality test.  It must not be presented as a validated
real-product fire model.

## Acceptance

For each run verify:

1. FireCAE starts the pinned FDS executable and streams its output.
2. FDS terminates normally and creates the expected `.out` and `.smv` files.
3. FireCAE automatically adds the result case to the Results object tree.
4. Smokeview opens inside the result workspace and can animate the data.
5. The source and run-copy `.fds` files have identical SHA-256 hashes.

These cases validate the calculation and post-processing path.  A case counts
as a complete pre-processing acceptance test only after FireCAE can recreate
the model objects through its Qt UI and deterministically export the same FDS
semantics.

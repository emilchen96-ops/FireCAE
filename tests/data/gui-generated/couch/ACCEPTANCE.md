# FireCAE `couch` full-run acceptance

- 26 editable business objects have stable, unique UUIDs.
- Mesh multiplier, fuel species, layered materials, surfaces, obstructions,
  ignitor particle, initial condition and vent links use UUID references.
- Save/reopen preserves every UUID and the generated FDS text.
- Official and FireCAE canonical FDS inputs have the same SHA-256 and are
  byte-identical after the documented canonicalization step.
- FDS revision `FDS-6.11.1-0-gff928db-release` completed the full 600.00000 s
  case using 8 MPI processes.
- FDS wall-clock time was 4533.215 s; FireCAE's `FdsRunner` measured
  4533664 ms and received `STOP: FDS completed successfully (CHID: couch)`.
- The FireCAE result scanner loaded all 208/208 supported FDS result files with
  Ready status. The inventory includes 40 boundary files, 40 slice files,
  24 Smoke3D files and 8 particle files.
- The solver output contains no line beginning with `WARNING:` and no fatal
  FDS error.
- The maximum sampled HRR is 1955.7398 kW at 255.01009 s; the HRR at 600 s is
  0.26043643 kW.
- Trapezoidal integration of the output-sampled `MLR_POLYURETHANE` history is
  22.264533 kg. Its sampled peak is 0.086913151 kg/s.

No independently generated full-duration PyroSim result set is available yet,
so this report does not claim a PyroSim numerical match. The shorter
`couch_smoke_12s` regression already has byte-identical physics result files.
Visible GUI/mouse and Smokeview playback acceptance remain deferred at the
user's request.

# FireCAE `couch_smoke_12s` acceptance

- 26 editable business objects with stable UUIDs.
- Mesh multiplier, fuel species, layered materials, surfaces, obstructions,
  ignitor particle, initial condition, vent, and output links use UUID references.
- Save/reopen preserves UUIDs and generated FDS text.
- Official and FireCAE canonical FDS inputs are byte-identical.
- FDS 6.11.1 completed at 12.00000 s using 8 MPI processes in 58332 ms.
- All 225 compared physics result files are present and SHA-256 identical to the
  reference result set.
- Final heat release rate is 538.27895 kW.

Visible GUI/mouse acceptance remains deferred at the user's request.

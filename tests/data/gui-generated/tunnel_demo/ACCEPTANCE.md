# FireCAE `tunnel_demo` acceptance

- 11 editable business objects with stable UUIDs.
- The mesh multiplier and fire-surface links are stored as UUID references.
- Save/reopen preserves UUIDs and generated FDS text.
- Official and FireCAE canonical FDS inputs are byte-identical.
- FDS 6.11.1 completed the full 30.00000 s case using 8 MPI processes.
- FDS wall-clock time was 708.057 s and the run ended with
  `STOP: FDS completed successfully`.
- The FireCAE result directory contains the complete 134-file reference
  inventory plus `tunnel_demo_cpu.csv`; no reference result filename is missing.
- The official reference run stops at 28.621181 s. Its complete HRR and pressure
  iteration CSV files are byte-identical prefixes of the completed FireCAE run.
- At the last common time, 28.621181 s, both runs report 8232.2610 kW HRR.
- FireCAE continues to 30.00000 s and reports a final HRR of 9832.9590 kW.

Visible GUI/mouse acceptance remains deferred at the user's request.

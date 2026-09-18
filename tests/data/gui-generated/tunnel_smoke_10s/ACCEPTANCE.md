# FireCAE `tunnel_smoke_10s` acceptance

- 11 editable business objects with stable UUIDs.
- Mesh-to-multiplier and vent-to-fire-surface relationships use UUID references.
- `PBX` plane-boundary VENT validation and export are supported.
- Save/reopen preserves UUIDs and generated FDS text.
- Official and FireCAE canonical FDS inputs are byte-identical.
- FDS 6.11.1 completed at 10.00000 s using 8 MPI processes in 262231 ms.
- All 130 compared physical result files are present and SHA-256 identical to
  the reference result set.
- Final heat release rate is 8313.3098 kW.

Visible GUI/mouse acceptance remains deferred at the user's request.

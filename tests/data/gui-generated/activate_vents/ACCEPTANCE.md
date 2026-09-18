# activate_vents — FireCAE command-line acceptance

- Date: 2026-08-28
- FireCAE project: `activate_vents.firecae`
- FireCAE input: `activate_vents.fds`
- Reference: `tests/data/fds-tutorials/activate_vents/activate_vents.fds`
- Solver: FDS 6.11.1, serial/OpenMP path
- End time: 20.0 s
- Solver result: `STOP: FDS completed successfully`
- FireCAE runner elapsed time: 9326 ms

## Object model

| Namelist/group | Count |
|---|---:|
| MESH | 1 |
| TIME | 1 |
| SURF | 7 |
| PART | 7 |
| VENT | 12 |
| DEVC | 6 |
| CTRL | 3 |
| RAMP | 7 |
| Total editable business objects | 44 |

Every object has a unique FireCAE UUID. Object references are stored as UUIDs
in the project and resolved to FDS IDs only by `FdsWriter`.

## Semantic comparison

Both inputs were parsed independently by `FdsImporter` and written by
`FdsWriter` to canonical FDS. Each side produced 44 objects with no warnings.
The two canonical files are exactly equal (2796 characters each).

## Result checks

FDS generated the Smokeview case, particle data, control CSV and device/control
transition log. Important transitions include controller 1 true at 3.0 s,
controller 1 false at 6.0 s, controller 3 true at 8.1 s, timer 7b true at
11.0 s, and controllers 1/4 true at 12.0 s.

Visible GUI clicking and Smokeview playback remain deferred until the user
explicitly authorizes unified interface control.

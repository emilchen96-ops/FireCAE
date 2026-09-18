# FireCAE FDS tutorial acceptance suite

This suite uses the examples installed with FDS 6.11.1. Each input is also
described in the official FDS User Guide, so the expected behavior can be
checked independently of FireCAE.

## Important scope distinction

All seven acceptance cases now have FireCAE tutorial factories that construct
real `FcObject`/`FcFdsNamelist` trees with stable UUID references. The generated
canonical FDS text matches the corresponding reference input byte for byte.
Reference `.fds` files are used only for analysis and regression comparison;
they are not imported by the tutorial implementation. Visible mouse-driven GUI
acceptance is still deferred at the user's request.

## Selected cases

| Case | User Guide location | What it checks | Expected result |
| --- | --- | --- | --- |
| `HVAC_aircoil` | Section 10.2.6, Figure 10.8 | HVAC nodes, duct, air coil and device output | Heat exchange about 45.2 kW and outlet temperature about 58.9 C |
| `activate_vents` | Section 18.5.11, Figure 18.8 | Devices, ramps, controls and timed vent activation | Seven colored vents change state at the documented times |
| `bucket_test_2` | Section 18.3.1, Figure 18.3 | Sprinkler property, tabulated spray pattern and droplets | About 5 kg of water reaches the floor |
| `tunnel_demo` | Section 21.3, Figure 21.6 | Eight meshes, tunnel pressure preconditioner and MPI | The 8 MW sloped-tunnel case reaches 30 s and writes pressure-iteration diagnostics |
| `couch` | Section 9.3, Figure 9.4 | Layered materials, solid pyrolysis, burn-away and ignition particles | Fire spreads along the couch over the documented 600 s run |

## Acceptance results (2026-08-28)

| Case executed through FireCAE | Mode | Result |
| --- | --- | --- |
| `HVAC_aircoil` | 1 MPI process | Completed 1 s; 45.243 kW heat exchange and 58.869 C outlet temperature |
| `activate_vents` | 1 MPI process | Completed 20 s; control/device state-change log generated |
| `bucket_test_2` | 1 MPI process | Completed 15 s; 4.958 kg accumulated water |
| `couch_smoke_12s` | 8 MPI processes | Completed 12 s; final total HRR about 538 kW; Smoke3D, slice, boundary and particle results generated |
| `tunnel_smoke_10s` | 8 MPI processes | Completed 10 s; final total HRR about 8.31 MW; pressure-iteration diagnostics generated |
| `tunnel_demo` | 8 MPI processes | Completed the full 30 s; final total HRR 9832.959 kW; all 134 reference result names are present |
| `couch` | 8 MPI processes | Completed the full 600 s; 208/208 supported results loaded; peak HRR 1955.740 kW at 255.010 s |

The smoke-test variants preserve the physical definitions of the official
inputs and change only the case identity, end time and (for the couch) output
cadence. The original `tunnel_demo` 30 s case has now completed. Any directories
named `partial_*` remain archived interrupted runs and must not be treated as
completed reference results.

## Files

The runnable copies and their result files are under:

```text
D:\FireCAE\tests\data\fds-tutorials\<case-name>
```

The unmodified installed inputs are under:

```text
D:\FireCAE\third_party\fds-smv-runtime\firemodels\FDS6\Examples
```

The accompanying guide is:

```text
D:\FireCAE\third_party\fds-smv-runtime\firemodels\FDS6\Documentation\Guides_and_Release_Notes\FDS_User_Guide.pdf
```

## Run through FireCAE

In the desktop application, choose **Simulation > Run FDS Case...** for a
single-process case or **Simulation > Run FDS Parallel...** for a multi-mesh
case. After completion, choose **Results > Open FDS Results...** and select the
generated `.smv` file.

The command-line acceptance harness invokes the same `FdsRunner` used by the
desktop application:

```text
FireCAEBenchmarkRunner.exe <input.fds> <fds.exe> [process-count]
```

The unattended harness defaults to a 360-minute safety timeout, configurable
with `FIRECAE_BENCHMARK_TIMEOUT_MINUTES`. The desktop runner is not subject to
this harness-only timeout.

## Preprocessor implementation requirements exposed by the suite

- `couch`: `SPEC`, `MATL`, multi-layer `SURF`, `PART`, `INIT`, `MULT`,
  `BULK_DENSITY`, and `BURN_AWAY`.
- `activate_vents`: `PART`, `DEVC`, `CTRL`, `RAMP`, and mesh-boundary vents.
- `bucket_test_2`: `PART`, `PROP`, `TABL`, sprinkler devices, and particle
  boundary/device output.
- `HVAC_aircoil`: HVAC node, duct and air-coil objects plus HVAC device output.
- `tunnel_demo`: mesh multipliers, `MISC`, `PRES`, `DUMP`, multi-mesh editing,
  and MPI process assignment.

These preprocessor objects, deterministic serialization, UUID/type validation,
CTRL cycle checks, HVAC duct-node checks, MULT expansion, mesh-overlap checks,
mesh-gap and adjacent-boundary continuity diagnostics, SLCF domain checks,
project round-trip persistence, GUI copy/reorder actions, and an in-application
physical CSV comparison table are now implemented. The remaining acceptance
work is visible GUI/Smokeview execution after user authorization and an
independently generated PyroSim result set.

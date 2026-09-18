# P03 — End-to-end execution and PyroSim comparison baseline

## Product target

FireCAE must support one reproducible workflow:

1. Create or import the model in FireCAE.
2. Validate it and export an FDS input file.
3. Run the same NIST FDS solver used for the acceptance baseline.
4. Monitor output and stop a running calculation safely.
5. Load the generated result set automatically.
6. Inspect time-dependent results in the integrated Smokeview workspace.
7. Compare FireCAE and PyroSim runs numerically, not only by screenshots.

## Completed in P03

- Serial FDS execution from the Qt application.
- Local MPI execution with a user-selected process count.
- Live solver output in the Messages panel.
- Run/stop action state management and process-tree cancellation.
- Automatic result discovery after a successful run.
- Embedded Smokeview result workspace and animation controls.
- A real bundled-FDS integration test in CTest.
- Seven editable tutorial object factories with stable UUID references and
  deterministic canonical FDS output.
- `.firecae` project save/open with object UUID, order, parameters and reference
  round-trip coverage.
- Reference-aware validation for type errors, dangling references, CTRL cycles,
  HVAC topology, MULT-expanded meshes, mesh overlap/gaps, adjacent boundary-grid
  discontinuity and out-of-domain slices.
- Tree copy/reorder operations and validation-message UUID navigation.
- In-application comparison of two `.smv` result cases: physical CSV files are
  matched independently of CHID, values are aligned by time interpolation, and
  final value, absolute peak, RMSE and normalized RMSE are reported per quantity.
- Export of the comparison table as a machine-readable CSV report.
- Comparison provenance records each available FDS input SHA-256, solver
  revision, completed simulation time and normal-termination state.

The `plume_average.fds` acceptance case was run from FireCAE with FDS 6.11.1.
It completed successfully and produced a result set that FireCAE opened without
manual re-selection.

## Current numerical baseline

The fresh FireCAE run is stored under:

`tests/data/comparison/plume_average/firecae`

It was compared with the repository's official FDS reference result. The
following files are byte-for-byte identical:

- `plume_average_hrr.csv`
- `plume_average.smv`

`plume_average_steps.csv` is intentionally not used as a physics acceptance
metric because it contains machine- and run-time performance information.

This proves the FireCAE launch/result pipeline does not alter this case. It is
not yet a PyroSim comparison: a separately exported and run PyroSim result set
is still required.

The seven-tutorial solver suite is also complete. In particular, the full
`couch` tutorial reached 600.00000 s with 8 MPI processes, normal FDS
termination and 208/208 supported result files loaded. Its evidence is under
`tests/data/gui-generated/couch`.

## Fair FireCAE/PyroSim comparison protocol

Use the same case definition and the same FDS solver revision in both programs.
Store the PyroSim output under:

`tests/data/comparison/plume_average/pyrosim`

For every comparison record:

- the SHA-256 of both `.fds` inputs;
- FDS version and revision from each `.out` file;
- termination status and simulated end time;
- HRR time history, including maximum absolute error and RMSE;
- device CSV histories when devices exist;
- mass and energy balance warnings from the solver output;
- representative Smokeview views at identical simulation times.

If the generated `.fds` inputs differ, first classify the differences as
ordering/formatting or physical-model differences. A visual similarity alone
is not an acceptance result.

## Remaining critical path

1. After the user authorizes GUI control, execute the seven visible mouse-driven
   tutorial workflows and Smokeview playback checks.
2. Connect IFC component geometry to reviewed FDS obstructions.
3. Recreate the same selected cases independently in PyroSim, run both with the
   pinned solver version, and generate numerical comparison reports.
4. Extend the numerical comparison view with plotted time histories.

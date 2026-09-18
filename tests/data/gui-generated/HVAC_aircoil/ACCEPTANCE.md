# FireCAE `HVAC_aircoil` acceptance

- 20 editable business objects with stable UUIDs.
- Inlet/outlet vents, HVAC nodes, duct, aircoil, and devices are connected by
  nine UUID reference targets, including the circular node/duct relationship.
- Save/reopen preserves UUIDs and generated FDS text.
- Official and FireCAE canonical FDS inputs are byte-identical.
- FDS 6.11.1 completed at 1.00000 s in 941 ms.
- The complete device CSV is SHA-256 identical to the reference.
- Final aircoil heat exchange is 45.243122 kW and outlet node temperature is
  58.868922 C.
- Non-key HRR differences are limited to machine-roundoff mass residuals near
  1e-16; the device results are exact.

Visible GUI/mouse acceptance remains deferred at the user's request.

# FireCAE third-party notices

The FireCAE Windows portable package contains redistributable runtime components from the following projects. This notice is an inventory, not a replacement for the license files copied beside it.

| Component | Purpose | License/source |
| --- | --- | --- |
| Qt 6 | Windows GUI runtime | LGPL/GPL/commercial multi-license; the package uses the dynamically linked LGPL runtime. See `Qt/`. |
| Open Cascade Technology | CAD import, geometry and 3D display | GNU LGPL 2.1 with OCCT exception. See `OpenCascade/`. |
| IfcOpenShell / IfcConvert | Isolated IFC geometry worker | LGPL; see `IfcOpenShell/`. |
| Fire Dynamics Simulator (FDS) | Solver runtime | NIST notice; see `FDS-LICENSE.md`. |
| Smokeview | FDS result visualization | NIST notice; see `SMOKEVIEW-LICENSE.md`. |
| Microsoft Visual C++ runtime | MSVC runtime DLLs deployed by Qt tooling | Microsoft redistributable runtime terms. |

FireCAE invokes FDS, Smokeview, and IfcConvert as separate processes. No proprietary PyroSim code or commercial CAD SDK is included.

Before public redistribution, the FireCAE project owner must add the final license terms for FireCAE's own source and binaries.

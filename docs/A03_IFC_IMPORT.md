# A03 - IFC Basic Import

## Scope

A03 establishes the first complete IFC import path. The Import Geometry action
accepts `.ifc`, runs the standalone IfcOpenShell `IfcConvert` process, reads its
XML decomposition output into FireCAE business objects, and reads its GLB output
into OpenCascade for display.

The imported tree is generated exclusively from `FcIfcObject` children. Each
item stores the FireCAE object UUID in `Qt::UserRole`; IFC `GlobalId` is
metadata and is never used as the internal identity key.

FireCAE geometry uses SI metres internally. IfcOpenShell resolves the source
IFC unit assignment and emits metre-based GLB geometry before OCCT loads it.

## Process boundary

`IfcConvert.exe` is deployed under `ifcopenshell` beside each FireCAE executable
and test executable. It is never loaded into the FireCAE process. This prevents
the OCCT 7.8.1 runtime used by the supplied IfcOpenShell build from colliding
with FireCAE's OCCT 7.8.0 runtime.

## Transactional behavior

Conversion and parsing complete before the model is changed. If conversion,
XML parsing, GLB reading, document insertion, or display fails, no partial IFC
object remains in the project tree.

## A03 limitations

- The full IFC spatial decomposition and basic identity metadata are retained.
- The complete converted model is one viewer presentation attached to the IFC
  model root.
- Element-level geometry mapping, visibility, filtering, and full property sets
  belong to A04.
- IFC-to-FDS obstruction conversion belongs to A05.
- STL, OBJ, and GLB user import belongs to A06.

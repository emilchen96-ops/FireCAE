#include "import/IfcImportService.h"

#include "geometry/FcIfcObject.h"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <Message_ProgressRange.hxx>
#include <RWGltf_CafReader.hxx>
#include <TCollection_AsciiString.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <XCAFPrs.hxx>
#include <XCAFPrs_IndexedDataMapOfShapeStyle.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace
{
constexpr int kStartTimeoutMs = 10000;
constexpr int kConversionTimeoutMs = 10 * 60 * 1000;

QString decodedProcessOutput(const QByteArray& bytes)
{
    if (bytes.contains('\0')) {
        return QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.constData()),
                                  bytes.size() / 2)
            .trimmed();
    }
    return QString::fromLocal8Bit(bytes).trimmed();
}

bool runConverter(const QString& converter,
                  const QString& input,
                  const QString& output,
                  const QStringList& options,
                  int progressStart,
                  int progressEnd,
                  const QString& stage,
                  const IfcImportProgressCallback& progress,
                  const IfcImportCancellationCheck& cancelled,
                  bool* wasCancelled,
                  QString* errorMessage)
{
    QProcess process;
    process.setProgram(converter);
    QStringList arguments{QStringLiteral("--no-progress")};
    arguments.append(options);
    arguments.append(input);
    arguments.append(output);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(kStartTimeoutMs)) {
        *errorMessage = QStringLiteral("IfcConvert could not start: %1")
                            .arg(process.errorString());
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while (!process.waitForFinished(100)) {
        if (cancelled && cancelled()) {
            process.kill();
            process.waitForFinished(5000);
            if (wasCancelled) *wasCancelled = true;
            *errorMessage = QStringLiteral("IFC import cancelled.");
            return false;
        }
        if (timer.elapsed() >= kConversionTimeoutMs) break;
        if (progress) {
            const int span = qMax(1, progressEnd - progressStart);
            const int pulse = qMin(span - 1,
                                   static_cast<int>(timer.elapsed() / 1000));
            progress(progressStart + pulse, stage);
        }
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished();
        *errorMessage = QStringLiteral("IFC conversion timed out.");
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
        !QFileInfo::exists(output)) {
        QString details = decodedProcessOutput(process.readAllStandardError());
        if (details.isEmpty()) {
            details = decodedProcessOutput(process.readAllStandardOutput());
        }
        *errorMessage = details.isEmpty()
                            ? QStringLiteral("IfcConvert failed with exit code %1.")
                                  .arg(process.exitCode())
                            : QStringLiteral("IfcConvert failed: %1").arg(details);
        return false;
    }
    if (progress) progress(progressEnd, stage);
    return true;
}

std::shared_ptr<FcIfcObject> parseIfcElement(QXmlStreamReader& reader,
                                             const QString& sourceFile,
                                             const QString& schema,
                                             bool modelRoot,
                                             const QString& inheritedStorey = {},
                                             const QString& inheritedSpace = {})
{
    const QString ifcClass = reader.name().toString();
    const QXmlStreamAttributes attributes = reader.attributes();
    const QString globalId = attributes.value(QStringLiteral("id")).toString();
    QString name = attributes.value(QStringLiteral("Name")).toString().trimmed();
    if (name.isEmpty()) {
        name = ifcClass;
    }

    auto object = std::make_shared<FcIfcObject>(name, ifcClass, globalId, modelRoot);
    object->setDescription(
        attributes.value(QStringLiteral("Description")).toString().trimmed());
    object->setSourceFile(sourceFile);
    object->setSchema(schema);
    QString storey = inheritedStorey;
    QString space = inheritedSpace;
    if (ifcClass.compare(QStringLiteral("IfcBuildingStorey"),
                         Qt::CaseInsensitive) == 0) {
        storey = name;
    }
    if (ifcClass.compare(QStringLiteral("IfcSpace"), Qt::CaseInsensitive) == 0) {
        space = name;
    }
    object->setFloorName(storey);
    QStringList tags{
        QStringLiteral("ifc-class:%1").arg(ifcClass),
        QStringLiteral("ifc-globalid:%1").arg(globalId)};
    if (!storey.isEmpty()) tags.append(QStringLiteral("ifc-storey:%1").arg(storey));
    if (!space.isEmpty()) tags.append(QStringLiteral("ifc-space:%1").arg(space));
    for (const QXmlStreamAttribute& attribute : attributes) {
        const QString key = attribute.name().toString();
        if (key.compare(QStringLiteral("id"), Qt::CaseInsensitive) == 0 ||
            key.compare(QStringLiteral("Name"), Qt::CaseInsensitive) == 0 ||
            key.compare(QStringLiteral("Description"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        const QString value = attribute.value().toString().trimmed();
        if (!value.isEmpty())
            tags.append(QStringLiteral("ifc-attribute:%1=%2").arg(key, value));
    }
    object->setTags(tags);

    while (reader.readNextStartElement()) {
        const QString childName = reader.name().toString();
        if (childName.startsWith(QStringLiteral("Ifc"))) {
            const auto child = parseIfcElement(reader, sourceFile, schema, false,
                                               storey, space);
            if (child) {
                object->addChild(child);
            }
        } else {
            reader.skipCurrentElement();
        }
    }
    return object;
}

std::shared_ptr<FcIfcObject> parseMetadata(const QString& xmlPath,
                                           const QString& sourceFile,
                                           QString* errorMessage)
{
    QFile file(xmlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *errorMessage = QStringLiteral("Cannot read IFC metadata: %1").arg(file.errorString());
        return {};
    }

    QXmlStreamReader reader(&file);
    QString schema;
    std::shared_ptr<FcIfcObject> root;
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) {
            continue;
        }
        if (reader.name() == QLatin1String("schema_identifiers")) {
            schema = reader.readElementText().trimmed();
        } else if (reader.name() == QLatin1String("decomposition")) {
            if (reader.readNextStartElement()) {
                root = parseIfcElement(reader, sourceFile, schema, true);
            }
        }
    }
    if (reader.hasError()) {
        *errorMessage = QStringLiteral("Invalid IFC metadata XML: %1")
                            .arg(reader.errorString());
        return {};
    }
    if (!root) {
        *errorMessage = QStringLiteral("IFC file contains no decomposition tree.");
    }
    return root;
}

using IfcObjectByGlobalId = QHash<QString, std::shared_ptr<FcIfcObject>>;

void indexIfcObjects(const std::shared_ptr<FcIfcObject>& object,
                     IfcObjectByGlobalId* objects)
{
    if (!object || !objects) {
        return;
    }
    if (!object->globalId().isEmpty()) {
        objects->insert(object->globalId(), object);
    }
    for (const FcObject::Ptr& child : object->children()) {
        indexIfcObjects(std::dynamic_pointer_cast<FcIfcObject>(child), objects);
    }
}

bool looksLikeIfcGlobalId(const QString& value)
{
    if (value.size() != 22) {
        return false;
    }
    for (const QChar character : value) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('_') &&
            character != QLatin1Char('$')) {
            return false;
        }
    }
    return true;
}

void mergeShape(QHash<QString, TopoDS_Shape>* shapes,
                const QString& globalId,
                const TopoDS_Shape& shape)
{
    if (!shapes || globalId.isEmpty() || shape.IsNull()) {
        return;
    }

    const auto existing = shapes->constFind(globalId);
    if (existing == shapes->cend()) {
        shapes->insert(globalId, shape);
        return;
    }
    if (existing.value().IsSame(shape)) {
        return;
    }

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, existing.value());
    builder.Add(compound, shape);
    shapes->insert(globalId, compound);
}

void collectNamedShapes(const TDF_Label& label,
                        QHash<QString, TopoDS_Shape>* shapes,
                        QHash<QString, QVector<TDF_Label>>* labels)
{
    Handle(TDataStd_Name) nameAttribute;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttribute)) {
        const TCollection_AsciiString name(nameAttribute->Get());
        const QString globalId = QString::fromUtf8(name.ToCString());
        if (looksLikeIfcGlobalId(globalId)) {
            mergeShape(shapes, globalId, XCAFDoc_ShapeTool::GetShape(label));
            (*labels)[globalId].append(label);
        }
    }

    for (TDF_ChildIterator iterator(label, Standard_False); iterator.More();
         iterator.Next()) {
        collectNamedShapes(iterator.Value(), shapes, labels);
    }
}

bool sourceAppearance(const XCAFPrs_Style& style, FcIfcAppearance& appearance)
{
    Quantity_ColorRGBA color;
    const Handle(XCAFDoc_VisMaterial)& material = style.Material();
    if (!material.IsNull() && !material->IsEmpty()) {
        QString name;
        if (!material->RawName().IsNull())
            name = QString::fromUtf8(material->RawName()->ToCString());
        if (name.isEmpty()) {
            Handle(TDataStd_Name) attribute;
            if (material->Label().FindAttribute(TDataStd_Name::GetID(), attribute))
                name = QString::fromUtf8(TCollection_AsciiString(attribute->Get()).ToCString());
        }
        // IfcConvert emits this even when the IFC has no surface style.
        // Do not misrepresent the converter's neutral grey as source material.
        if (name.compare(QStringLiteral("DefaultMaterial"), Qt::CaseInsensitive) == 0)
            return false;
        color = material->BaseColor();
        appearance.materialName = name;
    } else if (style.IsSetColorSurf()) {
        color = style.GetColorSurfRGBA();
    } else {
        return false;
    }
    appearance.red = color.GetRGB().Red();
    appearance.green = color.GetRGB().Green();
    appearance.blue = color.GetRGB().Blue();
    appearance.alpha = color.Alpha();
    appearance.origin = FcIfcAppearanceOrigin::ConvertedSource;
    return true;
}

void assignAppearance(const QVector<TDF_Label>& labels, FcIfcObject& object)
{
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(object.shape(), TopAbs_FACE, faces);
    // Apply containing-shape styles before more specific face styles.
    struct Assignment { TopoDS_Shape shape; FcIfcAppearance appearance; int count; };
    QVector<Assignment> assignments;
    for (const TDF_Label& label : labels) {
        XCAFPrs_IndexedDataMapOfShapeStyle styles;
        XCAFPrs::CollectStyleSettings(label, TopLoc_Location(), styles);
        for (int i = 1; i <= styles.Extent(); ++i) {
            FcIfcAppearance appearance;
            if (!sourceAppearance(styles.FindFromIndex(i), appearance)) continue;
            TopTools_IndexedMapOfShape styledFaces;
            TopExp::MapShapes(styles.FindKey(i), TopAbs_FACE, styledFaces);
            assignments.append({styles.FindKey(i), appearance, styledFaces.Extent()});
        }
    }
    std::stable_sort(assignments.begin(), assignments.end(),
                     [](const Assignment& a, const Assignment& b) { return a.count > b.count; });
    for (const Assignment& assignment : assignments) {
        if (assignment.shape.IsSame(object.shape())) object.setAppearance(assignment.appearance);
        TopTools_IndexedMapOfShape styledFaces;
        TopExp::MapShapes(assignment.shape, TopAbs_FACE, styledFaces);
        for (int i = 1; i <= styledFaces.Extent(); ++i) {
            const int index = faces.FindIndex(styledFaces(i));
            if (index > 0) object.setFaceAppearance(index, assignment.appearance);
        }
    }
}

int assignGlbShapes(const QString& glbPath,
                    const IfcObjectByGlobalId& objects,
                    int* unmatchedGeometryCount,
                    QStringList* unmatchedGeometryIds,
                    bool preserveMaterials,
                    QString* errorMessage)
{
    Handle(TDocStd_Document) document = new TDocStd_Document("BinXCAF");
    RWGltf_CafReader reader;
    reader.SetDocument(document);
    reader.SetSystemLengthUnit(1.0);
    if (!reader.Perform(TCollection_AsciiString(glbPath.toUtf8().constData()),
                        Message_ProgressRange())) {
        *errorMessage = QStringLiteral("OpenCascade could not read converted IFC geometry.");
        return 0;
    }

    const Handle(XCAFDoc_ShapeTool) shapeTool =
        XCAFDoc_DocumentTool::ShapeTool(document->Main());
    TDF_LabelSequence roots;
    shapeTool->GetFreeShapes(roots);
    QHash<QString, TopoDS_Shape> shapesByGlobalId;
    QHash<QString, QVector<TDF_Label>> labelsByGlobalId;
    for (Standard_Integer index = 1; index <= roots.Length(); ++index) {
        collectNamedShapes(roots.Value(index), &shapesByGlobalId, &labelsByGlobalId);
    }

    int assignedCount = 0;
    int unmatchedCount = 0;
    for (auto iterator = shapesByGlobalId.cbegin();
         iterator != shapesByGlobalId.cend(); ++iterator) {
        const auto object = objects.constFind(iterator.key());
        if (object == objects.cend()) {
            ++unmatchedCount;
            if (unmatchedGeometryIds) unmatchedGeometryIds->append(iterator.key());
            continue;
        }
        object.value()->setShape(iterator.value());
        if (preserveMaterials)
            assignAppearance(labelsByGlobalId.value(iterator.key()), *object.value());
        ++assignedCount;
    }
    if (unmatchedGeometryCount) {
        *unmatchedGeometryCount = unmatchedCount;
    }
    if (assignedCount == 0) {
        *errorMessage = QStringLiteral("Converted IFC file contains no displayable geometry.");
    }
    return assignedCount;
}

bool isSpatialIfcClass(const QString& ifcClass)
{
    const QString value = ifcClass.trimmed().toUpper();
    return value == QStringLiteral("IFCPROJECT") ||
           value == QStringLiteral("IFCSITE") ||
           value == QStringLiteral("IFCBUILDING") ||
           value == QStringLiteral("IFCBUILDINGSTOREY") ||
           value == QStringLiteral("IFCSPACE");
}

bool isProductIfcClass(const QString& ifcClass)
{
    const QString value = ifcClass.trimmed().toUpper();
    if (isSpatialIfcClass(value)) return value != QStringLiteral("IFCPROJECT");
    static const QSet<QString> exactProducts = {
        QStringLiteral("IFCWALL"), QStringLiteral("IFCWALLSTANDARDCASE"),
        QStringLiteral("IFCSLAB"), QStringLiteral("IFCROOF"),
        QStringLiteral("IFCCOLUMN"), QStringLiteral("IFCBEAM"),
        QStringLiteral("IFCDOOR"), QStringLiteral("IFCWINDOW"),
        QStringLiteral("IFCOPENINGELEMENT"), QStringLiteral("IFCSTAIR"),
        QStringLiteral("IFCSTAIRFLIGHT"), QStringLiteral("IFCRAMP"),
        QStringLiteral("IFCRAMPFLIGHT"), QStringLiteral("IFCCURTAINWALL"),
        QStringLiteral("IFCBUILDINGELEMENTPROXY"),
        QStringLiteral("IFCFURNISHINGELEMENT"),
        QStringLiteral("IFCFLOWTERMINAL"), QStringLiteral("IFCFLOWSEGMENT"),
        QStringLiteral("IFCFLOWFITTING"), QStringLiteral("IFCFLOWCONTROLLER"),
        QStringLiteral("IFCFLOWMOVINGDEVICE"),
        QStringLiteral("IFCFLOWSTORAGEDEVICE"),
        QStringLiteral("IFCFLOWTREATMENTDEVICE"),
        QStringLiteral("IFCDISTRIBUTIONELEMENT")};
    return exactProducts.contains(value) || value.endsWith(QStringLiteral("ELEMENT"));
}

QString detectLengthUnit(const QString& upperText)
{
    if (upperText.contains(
            QRegularExpression(QStringLiteral(
                R"(IFCSIUNIT\s*\([^;]*\.LENGTHUNIT\.[^;]*\.MILLI\.[^;]*\.METRE\.)")))) {
        return QStringLiteral("mm");
    }
    if (upperText.contains(
            QRegularExpression(QStringLiteral(
                R"(IFCSIUNIT\s*\([^;]*\.LENGTHUNIT\.[^;]*\.CENTI\.[^;]*\.METRE\.)")))) {
        return QStringLiteral("cm");
    }
    if (upperText.contains(
            QRegularExpression(QStringLiteral(
                R"(IFCSIUNIT\s*\([^;]*\.LENGTHUNIT\.[^;]*\.KILO\.[^;]*\.METRE\.)")))) {
        return QStringLiteral("km");
    }
    if (upperText.contains(
            QRegularExpression(QStringLiteral(
                R"(IFCSIUNIT\s*\([^;]*\.LENGTHUNIT\.[^;]*\$[^;]*\.METRE\.)")))) {
        return QStringLiteral("m");
    }
    if (upperText.contains(QStringLiteral("'FOOT'"))) return QStringLiteral("ft");
    if (upperText.contains(QStringLiteral("'INCH'"))) return QStringLiteral("in");
    return QStringLiteral("Unknown");
}

void collectIfcShapes(const std::shared_ptr<FcIfcObject>& object,
                      QVector<std::shared_ptr<FcIfcObject>>& shapes)
{
    if (!object) return;
    if (object->hasShape()) shapes.append(object);
    for (const FcObject::Ptr& child : object->children()) {
        collectIfcShapes(std::dynamic_pointer_cast<FcIfcObject>(child), shapes);
    }
}

void transformIfcObject(FcIfcObject& object, const gp_Trsf& transform)
{
    TopTools_IndexedMapOfShape oldFaces;
    TopExp::MapShapes(object.shape(), TopAbs_FACE, oldFaces);
    const auto appearances = object.faceAppearances();
    BRepBuilderAPI_Transform operation(object.shape(), transform, Standard_True, Standard_True);
    object.setShape(operation.Shape());
    TopTools_IndexedMapOfShape newFaces;
    TopExp::MapShapes(object.shape(), TopAbs_FACE, newFaces);
    for (auto it = appearances.cbegin(); it != appearances.cend(); ++it) {
        const int index = newFaces.FindIndex(operation.ModifiedShape(oldFaces(it.key())));
        if (index > 0) object.setFaceAppearance(index, it.value());
    }
}

void transformIfcObject(FcIfcObject& object, const IfcImportOptions& options)
{
    if (std::abs(options.additionalScale - 1.0) > 1.0e-12) {
        gp_Trsf scaling;
        scaling.SetScale(gp_Pnt(0.0, 0.0, 0.0), options.additionalScale);
        transformIfcObject(object, scaling);
    }
    if (options.sourceYAxisUp) {
        gp_Trsf rotation;
        rotation.SetRotation(
            gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)),
            std::acos(-1.0) / 2.0);
        transformIfcObject(object, rotation);
    }
    if (options.originX != 0.0 || options.originY != 0.0 ||
        options.originZ != 0.0) {
        gp_Trsf translation;
        translation.SetTranslation(
            gp_Vec(options.originX, options.originY, options.originZ));
        transformIfcObject(object, translation);
    }
}

TopoDS_Shape boundingBoxShape(const TopoDS_Shape& source)
{
    Bnd_Box bounds;
    BRepBndLib::Add(source, bounds);
    if (bounds.IsVoid()) return source;
    double xMin = 0.0, yMin = 0.0, zMin = 0.0;
    double xMax = 0.0, yMax = 0.0, zMax = 0.0;
    bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    if (xMax - xMin <= 1.0e-9 || yMax - yMin <= 1.0e-9 ||
        zMax - zMin <= 1.0e-9) {
        return source;
    }
    return BRepPrimAPI_MakeBox(gp_Pnt(xMin, yMin, zMin),
                               gp_Pnt(xMax, yMax, zMax)).Shape();
}

void applyIfcGeometryOptions(const std::shared_ptr<FcIfcObject>& root,
                             const IfcImportOptions& options)
{
    QVector<std::shared_ptr<FcIfcObject>> shapes;
    collectIfcShapes(root, shapes);
    for (const auto& object : shapes) {
        transformIfcObject(*object, options);
        if (options.simplification.compare(QStringLiteral("BOUNDING_BOX"),
                                           Qt::CaseInsensitive) == 0) {
            object->setShape(boundingBoxShape(object->shape()));
            // A box is an approximation with new topology, not the source faces.
            object->setAppearance(FcIfcObject::typeFallbackAppearance(object->ifcClass()));
        }
        object->setFdsConversionRoute(options.conversionRoute);
    }
}

bool filterIfcObject(const std::shared_ptr<FcIfcObject>& object,
                     const QSet<QString>& includedClasses,
                     int* filteredCount,
                     bool modelRoot = false)
{
    if (!object) return false;
    const std::vector<FcObject::Ptr> children = object->children();
    for (const FcObject::Ptr& child : children) {
        const auto ifcChild = std::dynamic_pointer_cast<FcIfcObject>(child);
        if (ifcChild && !filterIfcObject(ifcChild, includedClasses,
                                        filteredCount, false)) {
            object->removeChild(ifcChild->id());
        }
    }
    const QString ifcClass = object->ifcClass().trimmed().toUpper();
    const bool allowed = includedClasses.isEmpty() ||
                         includedClasses.contains(ifcClass) ||
                         isSpatialIfcClass(ifcClass) || modelRoot;
    if (!allowed && object->hasShape()) {
        object->clearShape();
        if (filteredCount) ++(*filteredCount);
    }
    return modelRoot || allowed || !object->children().empty();
}

void addShapeToCompound(BRep_Builder& builder, TopoDS_Compound& compound,
                        const std::shared_ptr<FcIfcObject>& object,
                        QVector<QPair<TopoDS_Shape, FcIfcAppearance>>& appearances,
                        bool clearShapes)
{
    if (!object) return;
    if (object->hasShape()) {
        builder.Add(compound, object->shape());
        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(object->shape(), TopAbs_FACE, faces);
        for (int i = 1; i <= faces.Extent(); ++i)
            appearances.append(qMakePair(faces(i), object->appearanceForFace(i)));
        if (clearShapes) object->clearShape();
    }
    for (const FcObject::Ptr& child : object->children()) {
        addShapeToCompound(builder, compound,
                           std::dynamic_pointer_cast<FcIfcObject>(child),
                           appearances,
                           clearShapes);
    }
}

void applyMergedAppearances(FcIfcObject& object,
                            const QVector<QPair<TopoDS_Shape, FcIfcAppearance>>& appearances)
{
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(object.shape(), TopAbs_FACE, faces);
    for (const auto& item : appearances) {
        const int index = faces.FindIndex(item.first);
        if (index > 0) object.setFaceAppearance(index, item.second);
    }
}

int applyMergeStrategy(const std::shared_ptr<FcIfcObject>& root,
                       const QString& strategy)
{
    if (!root) return 0;
    if (strategy.compare(QStringLiteral("MERGE_ALL"),
                         Qt::CaseInsensitive) == 0) {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        QVector<QPair<TopoDS_Shape, FcIfcAppearance>> appearances;
        addShapeToCompound(builder, compound, root, appearances, true);
        root->setShape(compound);
        applyMergedAppearances(*root, appearances);
        return 1;
    }
    if (strategy.compare(QStringLiteral("MERGE_BY_STOREY"),
                         Qt::CaseInsensitive) == 0) {
        int storeyShapeCount = 0;
        const std::function<void(const std::shared_ptr<FcIfcObject>&)> mergeStoreys =
            [&](const std::shared_ptr<FcIfcObject>& object) {
                if (!object) return;
                if (object->ifcClass().compare(QStringLiteral("IfcBuildingStorey"),
                                               Qt::CaseInsensitive) == 0) {
                    BRep_Builder builder;
                    TopoDS_Compound compound;
                    builder.MakeCompound(compound);
                    QVector<QPair<TopoDS_Shape, FcIfcAppearance>> appearances;
                    for (const FcObject::Ptr& child : object->children()) {
                        addShapeToCompound(
                            builder, compound,
                            std::dynamic_pointer_cast<FcIfcObject>(child), appearances, true);
                    }
                    object->setShape(compound);
                    applyMergedAppearances(*object, appearances);
                    ++storeyShapeCount;
                    return;
                }
                for (const FcObject::Ptr& child : object->children()) {
                    mergeStoreys(std::dynamic_pointer_cast<FcIfcObject>(child));
                }
            };
        mergeStoreys(root);
        if (storeyShapeCount > 0) return storeyShapeCount;
    }
    QVector<std::shared_ptr<FcIfcObject>> shapes;
    collectIfcShapes(root, shapes);
    return static_cast<int>(shapes.size());
}

void applyImportTags(const std::shared_ptr<FcIfcObject>& root,
                     const IfcImportOptions& options,
                     const IfcPreflightReport& report)
{
    if (!root) return;
    QStringList tags = root->tags();
    tags.append(QStringLiteral("ifc-source-unit:%1").arg(report.lengthUnit));
    tags.append(QStringLiteral("ifc-merge-strategy:%1").arg(options.mergeStrategy));
    tags.append(QStringLiteral("ifc-simplification:%1").arg(options.simplification));
    tags.append(QStringLiteral("fds-conversion-route:%1").arg(options.conversionRoute));
    tags.append(QStringLiteral("ifc-property-sets:%1").arg(report.propertySetCount));
    tags.append(QStringLiteral("ifc-material-relations:%1")
                    .arg(report.materialRelationshipCount));
    root->setTags(tags);
    root->setFdsConversionRoute(options.conversionRoute);
}
}

bool IfcImportResult::success() const
{
    return rootObject != nullptr && errorMessage.isEmpty();
}

IfcImportService::IfcImportService(const QString& converterPath)
    : m_converterPath(converterPath.isEmpty() ? defaultConverterPath() : converterPath)
{
}

IfcPreflightReport IfcImportService::inspectFile(const QString& filePath)
{
    IfcPreflightReport report;
    const QFileInfo source(filePath);
    report.sourceFile = source.absoluteFilePath();
    report.sourceBytes = source.size();
    if (!source.exists() || !source.isFile()) {
        report.errorMessage = QStringLiteral("IFC file does not exist: %1")
                                  .arg(filePath);
        return report;
    }
    QFile file(source.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        report.errorMessage = QStringLiteral("Cannot inspect IFC file: %1")
                                  .arg(file.errorString());
        return report;
    }
    const QString text = QString::fromUtf8(file.readAll()).toUpper();
    const QRegularExpression schemaExpression(
        QStringLiteral(R"(FILE_SCHEMA\s*\(\s*\(\s*'([^']+)')"));
    const QRegularExpressionMatch schemaMatch = schemaExpression.match(text);
    if (schemaMatch.hasMatch()) report.schema = schemaMatch.captured(1);
    report.lengthUnit = detectLengthUnit(text);

    const QRegularExpression entityExpression(
        QStringLiteral(R"(#\d+\s*=\s*(IFC[A-Z0-9_]+)\s*\()"));
    QRegularExpressionMatchIterator matches = entityExpression.globalMatch(text);
    while (matches.hasNext()) {
        const QString ifcClass = matches.next().captured(1);
        ++report.entityCount;
        ++report.classCounts[ifcClass];
        if (isProductIfcClass(ifcClass)) {
            ++report.productCount;
            if (!report.productClasses.contains(ifcClass))
                report.productClasses.append(ifcClass);
        }
        if (ifcClass == QStringLiteral("IFCBUILDINGSTOREY")) ++report.storeyCount;
        if (ifcClass == QStringLiteral("IFCSPACE")) ++report.spaceCount;
        if (ifcClass == QStringLiteral("IFCRELASSOCIATESMATERIAL"))
            ++report.materialRelationshipCount;
        if (ifcClass == QStringLiteral("IFCPROPERTYSET")) ++report.propertySetCount;
    }
    if (report.schema.isEmpty()) {
        report.warnings.append(QStringLiteral("IFC schema could not be identified."));
    }
    if (report.lengthUnit == QStringLiteral("Unknown")) {
        report.warnings.append(QStringLiteral(
            "IFC source length unit could not be identified; IfcConvert will normalize geometry to metres."));
    }
    if (report.productCount == 0) {
        report.warnings.append(QStringLiteral("No common IFC product entities were detected."));
    }
    if (report.entityCount == 0) {
        report.errorMessage = QStringLiteral("No IFC STEP entities were found.");
    }
    report.productClasses.sort(Qt::CaseInsensitive);
    return report;
}

IfcImportResult IfcImportService::importFile(
    const QString& filePath, const IfcImportOptions& options,
    const IfcImportProgressCallback& progress,
    const IfcImportCancellationCheck& cancelled) const
{
    IfcImportResult result;
    const auto reportProgress = [&progress](int percent, const QString& stage) {
        if (progress) progress(qBound(0, percent, 100), stage);
    };
    const auto stopIfCancelled = [&result, &cancelled]() {
        if (!cancelled || !cancelled()) return false;
        result.cancelled = true;
        result.errorMessage = QStringLiteral("IFC import cancelled.");
        result.rootObject.reset();
        return true;
    };
    reportProgress(2, QStringLiteral("Inspecting IFC metadata"));
    const QFileInfo input(filePath);
    if (!input.exists() || !input.isFile()) {
        result.errorMessage = QStringLiteral("IFC file does not exist: %1").arg(filePath);
        return result;
    }
    if (input.suffix().compare(QStringLiteral("ifc"), Qt::CaseInsensitive) != 0) {
        result.errorMessage = QStringLiteral("A03 supports .ifc files only.");
        return result;
    }
    if (!QFileInfo::exists(m_converterPath)) {
        result.errorMessage = QStringLiteral("IfcConvert was not found: %1")
                                  .arg(QDir::toNativeSeparators(m_converterPath));
        return result;
    }
    result.preflight = inspectFile(input.absoluteFilePath());
    result.warnings.append(result.preflight.warnings);
    if (!result.preflight.success()) {
        result.errorMessage = result.preflight.errorMessage;
        return result;
    }
    if (stopIfCancelled()) return result;

    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) {
        result.errorMessage = QStringLiteral("Could not create IFC import workspace.");
        return result;
    }
    const QString glbPath = temporaryDir.filePath(QStringLiteral("geometry.glb"));
    const QString xmlPath = temporaryDir.filePath(QStringLiteral("metadata.xml"));

    reportProgress(8, QStringLiteral("Preparing isolated IfcOpenShell conversion"));
    bool converterCancelled = false;
    if (!runConverter(m_converterPath, input.absoluteFilePath(), glbPath,
                      {QStringLiteral("--use-element-guids")}, 10, 44,
                      QStringLiteral("Converting IFC geometry"), progress,
                      cancelled, &converterCancelled, &result.errorMessage) ||
        !runConverter(m_converterPath, input.absoluteFilePath(), xmlPath, {},
                      45, 68, QStringLiteral("Converting IFC hierarchy"),
                      progress, cancelled, &converterCancelled,
                      &result.errorMessage)) {
        result.cancelled = converterCancelled;
        return result;
    }

    reportProgress(72, QStringLiteral("Building IFC floor and space hierarchy"));
    if (stopIfCancelled()) return result;
    result.rootObject = parseMetadata(xmlPath, input.absoluteFilePath(),
                                      &result.errorMessage);
    if (!result.rootObject) {
        return result;
    }

    IfcObjectByGlobalId objects;
    indexIfcObjects(result.rootObject, &objects);
    int unmatchedGeometryCount = 0;
    reportProgress(80, QStringLiteral("Matching IFC products to geometry"));
    if (stopIfCancelled()) return result;
    result.geometryObjectCount = assignGlbShapes(
        glbPath, objects, &unmatchedGeometryCount, &result.failedComponents,
        options.preserveMaterials,
        &result.errorMessage);
    if (result.geometryObjectCount == 0) {
        result.rootObject.reset();
        return result;
    }
    if (unmatchedGeometryCount > 0) {
        result.warnings.append(
            QStringLiteral("%1 IFC geometry nodes could not be matched to semantic objects.")
                .arg(unmatchedGeometryCount));
    }
    if (stopIfCancelled()) return result;
    reportProgress(90, QStringLiteral("Applying IFC filters and import strategy"));
    QSet<QString> includedClasses;
    for (const QString& ifcClass : options.includedClasses) {
        if (!ifcClass.trimmed().isEmpty())
            includedClasses.insert(ifcClass.trimmed().toUpper());
    }
    filterIfcObject(result.rootObject, includedClasses,
                    &result.filteredObjectCount, true);
    result.geometryObjectCount = applyMergeStrategy(
        result.rootObject, options.mergeStrategy);
    if (result.geometryObjectCount <= 0) {
        result.errorMessage = QStringLiteral(
            "The selected IFC filters removed every displayable geometry object.");
        result.rootObject.reset();
        return result;
    }
    applyIfcGeometryOptions(result.rootObject, options);
    if (options.simplification.compare(QStringLiteral("BOUNDING_BOX"), Qt::CaseInsensitive) == 0)
        result.warnings.append(QStringLiteral(
            "Bounding-box simplification replaces source faces; IFC type fallback colors are used."));
    applyImportTags(result.rootObject, options, result.preflight);
    result.rootObject->setVisible(options.initiallyVisible);
    result.rootObject->setSchema(
        result.rootObject->schema().isEmpty() ? result.preflight.schema
                                               : result.rootObject->schema());
    reportProgress(100, QStringLiteral("IFC import complete"));
    return result;
}

const QString& IfcImportService::converterPath() const
{
    return m_converterPath;
}

QString IfcImportService::defaultConverterPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ifcopenshell/IfcConvert.exe"));
}

#include "core/FcDocument.h"
#include "core/FcObjectGroup.h"
#include "core/FcProject.h"
#include "fds/FdsExamples.h"
#include "fds/FdsImporter.h"
#include "fds/FdsScene.h"

#include <QDir>

#include <iostream>
#include <memory>

namespace
{
int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}

const FdsScenePrimitive* firstPrimitive(const FdsScene& scene,
                                        const QString& keyword)
{
    for (const FdsScenePrimitive& primitive : scene.primitives) {
        if (primitive.keyword == keyword) return &primitive;
    }
    return nullptr;
}
}

int main()
{
    const auto simple = FdsExamples::createSimpleTestProject();
    const FdsScene simpleScene = FdsSceneBuilder::build(*simple);
    if (!simpleScene.hasDomainBounds || simpleScene.primitiveCount("MESH") != 1 ||
        simpleScene.primitiveCount("OBST") != 1 ||
        simpleScene.primitiveCount("VENT") != 2 ||
        simpleScene.primitiveCount("SLCF") != 1) {
        return fail("Simple test scene did not contain the expected FDS geometry.");
    }
    const FdsScenePrimitive* simpleSlice = firstPrimitive(simpleScene, "SLCF");
    if (!simpleSlice || simpleSlice->visible) {
        return fail("Slice planes must be hidden by default.");
    }

    const auto activate = FdsExamples::createActivateVentsProject();
    const FdsScene activateScene = FdsSceneBuilder::build(*activate);
    if (activateScene.primitiveCount("MESH") != 1 ||
        activateScene.primitiveCount("VENT") != 12 ||
        activateScene.primitiveCount("DEVC") != 6) {
        return fail("activate_vents scene mapping is incomplete.");
    }

    const auto bucket = FdsExamples::createBucketTest2Project();
    const FdsScene bucketScene = FdsSceneBuilder::build(*bucket);
    if (bucketScene.primitiveCount("MESH") != 1 ||
        bucketScene.primitiveCount("VENT") != 4 ||
        bucketScene.primitiveCount("DEVC") != 2) {
        return fail("bucket_test_2 scene mapping is incomplete.");
    }

    const auto couch = FdsExamples::createCouchProject();
    const FdsScene couchScene = FdsSceneBuilder::build(*couch);
    const FdsScenePrimitive* couchMesh = firstPrimitive(couchScene, "MESH");
    if (!couchMesh || couchScene.primitiveCount("MESH") != 8 ||
        couchScene.primitiveCountForObject(couchMesh->objectId) != 8 ||
        couchScene.primitiveCount("OBST") != 5 ||
        couchScene.primitiveCount("INIT") != 1 ||
        couchScene.primitiveCount("SLCF") != 2) {
        return fail("Couch MULT expansion or FDS geometry mapping is incomplete.");
    }
    if (couchScene.domainBounds.xMax != 5.0 ||
        couchScene.domainBounds.yMax != 5.0 ||
        couchScene.domainBounds.zMax != 2.4) {
        return fail("Couch MULT expansion produced incorrect domain bounds.");
    }

    const auto couchSmoke = FdsExamples::createCouchSmoke12sProject();
    const FdsScene couchSmokeScene = FdsSceneBuilder::build(*couchSmoke);
    if (couchSmokeScene.primitiveCount("MESH") != 8 ||
        couchSmokeScene.primitiveCount("OBST") != 5 ||
        couchSmokeScene.primitiveCount("VENT") != 1) {
        return fail("couch_smoke_12s scene mapping is incomplete.");
    }

    const auto hvac = FdsExamples::createHvacAircoilProject();
    const FdsScene hvacScene = FdsSceneBuilder::build(*hvac);
    if (hvacScene.primitiveCount("MESH") != 1 ||
        hvacScene.primitiveCount("VENT") != 6 ||
        hvacScene.primitiveCount("OBST") != 1 ||
        hvacScene.primitiveCount("HVAC") < 4 ||
        hvacScene.primitiveCount("DEVC") != 2 ||
        hvacScene.primitiveCount("SLCF") != 1) {
        return fail("HVAC_aircoil network visualization is incomplete.");
    }

    const auto tunnel = FdsExamples::createTunnelDemoProject();
    const FdsScene tunnelScene = FdsSceneBuilder::build(*tunnel);
    const FdsScenePrimitive* tunnelMesh = firstPrimitive(tunnelScene, "MESH");
    if (!tunnelMesh || tunnelScene.primitiveCount("MESH") != 8 ||
        tunnelScene.primitiveCountForObject(tunnelMesh->objectId) != 8 ||
        tunnelScene.domainBounds.xMin != 0.0 ||
        tunnelScene.domainBounds.xMax != 128.0 ||
        tunnelScene.primitiveCount("VENT") != 2 ||
        tunnelScene.primitiveCount("SLCF") != 2) {
        return fail("Tunnel MULT expansion or plane mapping is incomplete.");
    }

    const auto tunnelSmoke = FdsExamples::createTunnelSmoke10sProject();
    const FdsScene tunnelSmokeScene = FdsSceneBuilder::build(*tunnelSmoke);
    if (tunnelSmokeScene.primitiveCount("MESH") != 8 ||
        tunnelSmokeScene.domainBounds.xMax != 128.0) {
        return fail("tunnel_smoke_10s scene mapping is incomplete.");
    }

    const QString importedTunnelPath =
        QDir(QStringLiteral(FIRECAE_TEST_DATA_DIR))
            .filePath(QStringLiteral("fds-tutorials/tunnel_smoke_10s/tunnel_smoke_10s.fds"));
    const FdsImportResult importedTunnel = FdsImporter().importFile(importedTunnelPath);
    if (!importedTunnel.success()) {
        return fail("The on-disk FDS tutorial could not be imported for scene generation.");
    }
    const FdsScene importedTunnelScene = FdsSceneBuilder::build(*importedTunnel.project);
    if (importedTunnelScene.primitiveCount("MESH") != 8 ||
        importedTunnelScene.primitiveCount("VENT") != 2 ||
        importedTunnelScene.domainBounds.xMax != 128.0) {
        return fail("Imported FDS records did not generate the expected UUID scene.");
    }

    // Group visibility must propagate without changing UUID associations.
    couch->document()->meshesGroup()->setVisible(false);
    const FdsScene hiddenMeshScene = FdsSceneBuilder::build(*couch);
    const FdsScenePrimitive* hiddenMesh = firstPrimitive(hiddenMeshScene, "MESH");
    if (!hiddenMesh || hiddenMesh->visible || hiddenMesh->objectId != couchMesh->objectId) {
        return fail("Group visibility did not propagate through the UUID scene mapping.");
    }

    std::cout << "FireCAE A07 FDS scene tests passed.\n";
    return 0;
}

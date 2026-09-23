cmake_minimum_required(VERSION 3.21)

project(FireCAE VERSION 0.3.0 LANGUAGES CXX)

# Avoid rewriting unchanged runtime DLLs while independent solver runs use them.
set(FIRECAE_COPY_RUNTIME_DIRECTORY copy_directory)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
    set(FIRECAE_COPY_RUNTIME_DIRECTORY copy_directory_if_different)
endif()

# CMake 4.1 may decode localized MSVC /showIncludes output incorrectly when
# configured from a UTF-8 Windows terminal.  Ninja then misses every header
# dependency and can link stale ABI objects after public-header changes.
# Override the detected mojibake with the prefix emitted by Chinese MSVC.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets PrintSupport Concurrent)
find_package(OpenCASCADE REQUIRED
    COMPONENTS FoundationClasses ModelingData ModelingAlgorithms Visualization)

if(OpenCASCADE_MAJOR_VERSION LESS 7 OR
   (OpenCASCADE_MAJOR_VERSION EQUAL 7 AND OpenCASCADE_MINOR_VERSION LESS 8))
    message(FATAL_ERROR
        "FireCAE requires OpenCascade 7.8 or newer; found "
        "${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}.")
endif()

qt_standard_project_setup()

# Some Windows OCCT packages contain stale third-party paths in imported target
# interfaces. Use package-provided directories, but link only required libraries by path.
set(FIRECAE_OCCT_LIBRARY_NAMES
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
    TKPrim TKMesh TKHLR TKShHealing TKBO TKBool TKService TKV3d TKOpenGl
    TKCDF TKLCAF TKCAF TKVCAF TKXCAF TKXSBase TKRWMesh TKDE TKDEGLTF
    TKDEOBJ TKDESTEP TKDEIGES TKDESTL)

set(FIRECAE_OCCT_LIBRARIES)
set(FIRECAE_OCCT_RUNTIME_DLLS)
foreach(library_name IN LISTS FIRECAE_OCCT_LIBRARY_NAMES)
    set(library_path "${OpenCASCADE_LIBRARY_DIR}/${library_name}.lib")
    set(runtime_path "${OpenCASCADE_BINARY_DIR}/${library_name}.dll")
    if(NOT EXISTS "${library_path}")
        message(FATAL_ERROR "Required OCCT library not found: ${library_path}")
    endif()
    if(WIN32 AND NOT EXISTS "${runtime_path}")
        message(FATAL_ERROR "Required OCCT runtime not found: ${runtime_path}")
    endif()
    list(APPEND FIRECAE_OCCT_LIBRARIES "${library_path}")
    if(WIN32)
        list(APPEND FIRECAE_OCCT_RUNTIME_DLLS "${runtime_path}")
    endif()
endforeach()

set(FIRECAE_OCCT_3RDPARTY_ROOT "" CACHE PATH
    "Root containing the runtime DLLs required by this OCCT build")
if(WIN32 AND FIRECAE_OCCT_3RDPARTY_ROOT)
    set(FIRECAE_OCCT_3RDPARTY_DLL_NAMES
        openvr_api.dll FreeImage.dll freetype.dll avcodec-57.dll avformat-57.dll
        swscale-4.dll avutil-55.dll jemalloc.dll tbb12.dll)
    foreach(dll_name IN LISTS FIRECAE_OCCT_3RDPARTY_DLL_NAMES)
        file(GLOB_RECURSE dll_candidates LIST_DIRECTORIES false
            "${FIRECAE_OCCT_3RDPARTY_ROOT}/${dll_name}")
        if(NOT dll_candidates)
            message(FATAL_ERROR
                "Required OCCT third-party runtime ${dll_name} was not found under "
                "${FIRECAE_OCCT_3RDPARTY_ROOT}")
        endif()
        list(GET dll_candidates 0 dll_path)
        list(APPEND FIRECAE_OCCT_RUNTIME_DLLS "${dll_path}")
    endforeach()
elseif(WIN32)
    message(WARNING
        "FIRECAE_OCCT_3RDPARTY_ROOT is empty. OCCT third-party DLLs must be on PATH.")
endif()

add_library(FireCAECore STATIC
    src/core/FcDocument.cpp
    src/core/FcDocument.h
    src/core/FcFloorObject.cpp
    src/core/FcFloorObject.h
    src/core/FcObject.cpp
    src/core/FcObject.h
    src/core/FcObjectGroup.cpp
    src/core/FcObjectGroup.h
    src/core/FcProject.cpp
    src/core/FcProject.h
    src/core/FcScenario.cpp
    src/core/FcScenario.h
    src/results/FcResultCase.cpp
    src/results/FcResultCase.h
    src/results/FcResultFile.cpp
    src/results/FcResultFile.h)
target_link_libraries(FireCAECore PUBLIC Qt6::Core)
target_include_directories(FireCAECore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(FireCAEFdsModel STATIC
    src/fds/FcFdsModel.cpp
    src/fds/FcFdsModel.h
    src/fds/FdsScene.cpp
    src/fds/FdsScene.h
    src/fds/FdsImporter.cpp
    src/fds/FdsImporter.h
    src/fds/FdsExamples.cpp
    src/fds/FdsExamples.h
    src/fds/FcProjectSerializer.cpp
    src/fds/FcProjectSerializer.h
    src/fds/FdsWriter.cpp
    src/fds/FdsWriter.h
    src/fds/FdsSchema.cpp
    src/fds/FdsSchema.h
    src/fds/FdsPropertyLibrary.cpp
    src/fds/FdsPropertyLibrary.h
    src/fds/FdsBlockConversionService.cpp
    src/fds/GeometryDerivedUpdateService.cpp
    src/fds/GeometryDerivedUpdateService.h
    src/modeling/GeometryEditDependencyService.cpp
    src/modeling/GeometryEditDependencyService.h
    src/fds/FdsBlockConversionService.h)
target_link_libraries(FireCAEFdsModel PUBLIC FireCAECore Qt6::Core)
target_include_directories(FireCAEFdsModel PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(FireCAEReliability STATIC
    src/reliability/CrashDiagnostics.cpp
    src/reliability/CrashDiagnostics.h
    src/reliability/ProjectRecoveryManager.cpp
    src/reliability/ProjectRecoveryManager.h
    src/settings/ApplicationSettings.cpp
    src/settings/ApplicationSettings.h)
target_link_libraries(FireCAEReliability PUBLIC FireCAEFdsModel Qt6::Core)
target_include_directories(FireCAEReliability PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(FireCAEReliability PRIVATE
    FIRECAE_OCCT_VERSION="${OpenCASCADE_VERSION}")

add_library(FireCAEGeometry STATIC
    src/geometry/FcGeometryObject.cpp
    src/geometry/FcGeometryObject.h
    src/geometry/FcIfcObject.cpp
    src/geometry/FcIfcObject.h
    src/modeling/BuildingGeometryService.cpp
    src/modeling/BuildingGeometryService.h
    src/modeling/GeometryEditService.cpp
    src/modeling/GeometryEditService.h
    src/modeling/SnapManager.cpp
    src/modeling/SnapManager.h)
target_link_libraries(FireCAEGeometry
    PUBLIC FireCAECore
    PRIVATE ${FIRECAE_OCCT_LIBRARIES})
target_include_directories(FireCAEGeometry
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${OpenCASCADE_INCLUDE_DIR}")
target_link_libraries(FireCAEFdsModel PUBLIC FireCAEGeometry)

add_library(FireCAEImport STATIC
    src/import/GeometryImportService.cpp
    src/import/GeometryImportService.h
    src/import/IfcImportService.cpp
    src/import/IfcImportService.h)
target_link_libraries(FireCAEImport
    PUBLIC FireCAEGeometry
    PRIVATE Qt6::Core ${FIRECAE_OCCT_LIBRARIES})
target_include_directories(FireCAEImport
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${OpenCASCADE_INCLUDE_DIR}")

add_library(FireCAEResults STATIC
    src/comparison/FdsComparisonReport.cpp
    src/comparison/FdsComparisonReport.h
    src/comparison/FdsInputComparator.cpp
    src/comparison/FdsInputComparator.h
    src/comparison/ThreeWayComparisonEvidence.cpp
    src/comparison/ThreeWayComparisonEvidence.h
    src/results/FdsCsvData.cpp
    src/results/FdsCsvData.h
    src/results/FdsResultComparator.cpp
    src/results/FdsResultComparator.h
    src/results/FdsResultScanner.cpp
    src/results/FdsResultScanner.h
    src/results/FdsSliceReader.cpp
    src/results/FdsSliceReader.h
    src/results/FdsSliceImageRenderer.cpp
    src/results/FdsSliceImageRenderer.h
    src/results/SmokeviewLauncher.cpp
    src/results/SmokeviewLauncher.h
    src/results/SmokeviewFrameRenderer.cpp
    src/results/SmokeviewFrameRenderer.h)
target_link_libraries(FireCAEResults
    PUBLIC FireCAECore FireCAEFdsModel Qt6::Core Qt6::PrintSupport)
target_include_directories(FireCAEResults PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(FireCAEResources STATIC
    src/resources/ProjectResourceManager.cpp
    src/resources/ProjectResourceManager.h)
target_link_libraries(FireCAEResources
    PUBLIC FireCAEFdsModel FireCAEGeometry FireCAEResults Qt6::Core)
target_include_directories(FireCAEResources PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(FireCAESimulation STATIC
    src/simulation/FdsRunner.cpp
    src/simulation/FdsRunner.h
    src/simulation/SolverBackend.cpp
    src/simulation/SolverBackend.h
    src/simulation/SimulationTaskManager.cpp
    src/simulation/SimulationTaskManager.h)
target_link_libraries(FireCAESimulation PUBLIC Qt6::Core)
target_include_directories(FireCAESimulation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(FireCAEVisualization STATIC
    src/visualization/FdsSceneSynchronizer.cpp
    src/visualization/FdsSceneSynchronizer.h
    src/visualization/GeometryDisplayManager.cpp
    src/visualization/GeometryDisplayManager.h
    src/visualization/OccViewer.cpp
    src/visualization/OccViewer.h
    src/visualization/OccViewWidget.cpp
    src/visualization/OccViewWidget.h)
target_link_libraries(FireCAEVisualization
    PUBLIC FireCAEGeometry FireCAEFdsModel Qt6::Widgets
    PRIVATE ${FIRECAE_OCCT_LIBRARIES})
target_include_directories(FireCAEVisualization
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${OpenCASCADE_INCLUDE_DIR}")

set(FIRECAE_UI_SOURCES
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/ui/ApplicationSettingsDialog.cpp
    src/ui/ApplicationSettingsDialog.h
    src/ui/BuildingElementDialog.cpp
    src/ui/BuildingElementDialog.h
    src/ui/FdsObjectEditorDialog.cpp
    src/ui/FdsObjectEditorDialog.h
    src/ui/FdsRecordEditor.cpp
    src/ui/FdsRecordEditor.h
    src/ui/FdsSchemaEditorWidget.cpp
    src/ui/FdsSchemaEditorWidget.h
    src/ui/FdsWorkflowDialogs.cpp
    src/ui/FdsWorkflowDialogs.h
    src/ui/FloorEditorDialog.cpp
    src/ui/FloorEditorDialog.h
    src/ui/GeometryImportWizard.cpp
    src/ui/GeometryImportWizard.h
    src/ui/MessageWidget.cpp
    src/ui/MessageWidget.h
    src/ui/ModelTreeWidget.cpp
    src/ui/ModelTreeWidget.h
    src/ui/ObjectReferenceDialog.cpp
    src/ui/ObjectReferenceDialog.h
    src/ui/NativeResultViewerWidget.cpp
    src/ui/NativeResultViewerWidget.h
    src/ui/PropertiesWidget.cpp
    src/ui/PropertiesWidget.h
    src/ui/ProjectResourcesDialog.cpp
    src/ui/ProjectResourcesDialog.h
    src/ui/ScenarioManagerDialog.cpp
    src/ui/ScenarioManagerDialog.h
    src/ui/SurfaceAssignmentDialog.cpp
    src/ui/SurfaceAssignmentDialog.h
    src/ui/SmokeviewHostWidget.cpp
    src/ui/SmokeviewHostWidget.h
    src/ui/SimulationTaskCenterWidget.cpp
    src/ui/SimulationTaskCenterWidget.h
    src/ui/SimulationStatusWidget.cpp
    src/ui/SimulationStatusWidget.h
    src/ui/SimulationParametersDialog.cpp
    src/ui/SimulationParametersDialog.h
    src/ui/SimulationRunDialog.cpp
    src/ui/SimulationRunDialog.h
    src/ui/StartPageWidget.cpp
    src/ui/StartPageWidget.h
    src/ui/TutorialGuideWidget.cpp
    src/ui/TutorialGuideWidget.h
    src/ui/UiLanguage.cpp
    src/ui/UiLanguage.h)

qt_add_executable(FireCAE WIN32
    src/main.cpp
    ${FIRECAE_UI_SOURCES}
    resources/resources.qrc)
target_link_libraries(FireCAE PRIVATE
    FireCAEVisualization FireCAEImport FireCAEResults FireCAESimulation
    FireCAEResources FireCAEReliability FireCAEFdsModel Qt6::Concurrent)
target_include_directories(FireCAE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(FIRECAE_APPLICATION_OUTPUT_DIRECTORY "" CACHE PATH "Optional isolated application output directory")
if(FIRECAE_APPLICATION_OUTPUT_DIRECTORY)
    set_target_properties(FireCAE PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${FIRECAE_APPLICATION_OUTPUT_DIRECTORY}")
endif()
set(FIRECAE_BUILD_LABEL "" CACHE STRING "Optional visible build identifier")
if(FIRECAE_BUILD_LABEL)
    target_compile_definitions(FireCAE PRIVATE FIRECAE_BUILD_LABEL="${FIRECAE_BUILD_LABEL}")
endif()

add_executable(FireCAECoreTests tests/core_tests.cpp)
target_link_libraries(FireCAECoreTests PRIVATE
    FireCAEImport FireCAEResults FireCAESimulation FireCAEResources FireCAEReliability
    FireCAEFdsModel Qt6::Core)
target_compile_definitions(FireCAECoreTests PRIVATE
    FIRECAE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

add_executable(FireCAEUiTests tests/ui_smoke_tests.cpp tests/ui_feedback_tests.cpp ${FIRECAE_UI_SOURCES})
target_link_libraries(FireCAEUiTests PRIVATE
    FireCAEVisualization FireCAEImport FireCAEResults FireCAESimulation
    FireCAEResources FireCAEReliability FireCAEFdsModel Qt6::Concurrent)
target_include_directories(FireCAEUiTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(FireCAEUiTests PRIVATE
    FIRECAE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

if(WIN32)
    target_link_libraries(FireCAE PRIVATE user32)
    target_link_libraries(FireCAEUiTests PRIVATE user32)
endif()

if(MSVC)
    foreach(target FireCAECore FireCAEFdsModel FireCAEReliability FireCAEResources FireCAEGeometry FireCAEImport FireCAEResults FireCAESimulation
                   FireCAEVisualization FireCAE FireCAECoreTests FireCAEUiTests)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endforeach()
endif()

enable_testing()
add_executable(FireCAEUiLanguageTests tests/ui_language_tests.cpp src/ui/UiLanguage.cpp)
target_include_directories(FireCAEUiLanguageTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(FireCAEUiLanguageTests PRIVATE Qt6::Widgets)
if(MSVC)
    target_compile_options(FireCAEUiLanguageTests PRIVATE /W4 /permissive- /utf-8)
endif()
add_test(NAME FireCAEUiLanguageTests COMMAND FireCAEUiLanguageTests)
add_test(NAME FireCAEUiFeedbackTests COMMAND FireCAEUiTests --ui-feedback-smoke)
set_tests_properties(FireCAEUiLanguageTests FireCAEUiFeedbackTests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_test(NAME FireCAECoreTests COMMAND FireCAECoreTests)
add_test(NAME FireCAEUiTests COMMAND FireCAEUiTests)
add_test(NAME FireCAEP18WorkspaceUiTests COMMAND FireCAEUiTests --p18-workspace-smoke)
add_test(NAME FireCAEP19FloorDrawingUiTests COMMAND FireCAEUiTests --p19-floor-drawing-smoke)
add_test(NAME FireCAEP20ProfessionalEditorsUiTests COMMAND FireCAEUiTests --p20-professional-editors-smoke)
target_sources(FireCAEUiTests PRIVATE tests/m07_object_type_switch_tests.cpp)
add_test(NAME FireCAEM07ObjectTypeSwitchTests COMMAND FireCAEUiTests --m07-object-type-switch)
set_tests_properties(FireCAEM07ObjectTypeSwitchTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL TRUE)
add_test(NAME FireCAEP21RecordSourceMapUiTests COMMAND FireCAEUiTests --p21-record-source-map-smoke)
add_test(NAME FireCAEP22ImportMaturityUiTests COMMAND FireCAEUiTests --p22-import-maturity-smoke)
set_tests_properties(FireCAEP22ImportMaturityUiTests PROPERTIES TIMEOUT 180)
add_test(NAME FireCAEP23SimulationTaskUiTests COMMAND FireCAEUiTests --p23-simulation-task-smoke)
add_test(NAME FireCAEP24ResultsMaturityUiTests COMMAND FireCAEUiTests --p24-results-maturity-smoke)
add_test(NAME FireCAEP25ScenarioReliabilityUiTests COMMAND FireCAEUiTests --p25-scenario-reliability-smoke)
add_test(NAME FireCAEP26GuidedTutorialUiTests COMMAND FireCAEUiTests --p26-guided-tutorial-smoke)
add_test(NAME FireCAEP27FirstFireE2ETests COMMAND FireCAEUiTests --p27-first-fire-e2e)
set_tests_properties(FireCAEP27FirstFireE2ETests PROPERTIES TIMEOUT 900)
add_test(NAME FireCAEReacceptanceMultipleOutputsE2ETests
    COMMAND FireCAEUiTests --reacceptance-multiple-outputs-e2e)
set_tests_properties(FireCAEReacceptanceMultipleOutputsE2ETests PROPERTIES TIMEOUT 900)
add_test(NAME FireCAEP27ComplexIfcE2ETests COMMAND FireCAEUiTests --p27-complex-ifc-e2e)
set_tests_properties(FireCAEP27ComplexIfcE2ETests PROPERTIES TIMEOUT 900)
add_test(NAME FireCAEP29FiveTutorialE2ETests COMMAND FireCAEUiTests --p29-five-tutorial-e2e)
set_tests_properties(FireCAEP29FiveTutorialE2ETests PROPERTIES TIMEOUT 900)
add_test(NAME FireCAEP32ParticleSprayUiTests
    COMMAND FireCAEUiTests --p32-particle-spray-smoke)
set_tests_properties(FireCAEP32ParticleSprayUiTests PROPERTIES TIMEOUT 180)
set_tests_properties(FireCAEP23SimulationTaskUiTests PROPERTIES TIMEOUT 180)
target_sources(FireCAEUiTests PRIVATE tests/m08_device_control_tests.cpp)
add_test(NAME FireCAEM08DeviceControlTests COMMAND FireCAEUiTests --m08-device-control)
set_tests_properties(FireCAEM08DeviceControlTests PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME FireCAEA10WorkflowUiTests COMMAND FireCAEUiTests --a10-workflow-smoke)
add_test(NAME FireCAEA10BlankTutorialUiTests
    COMMAND FireCAEUiTests --a10-blank-tutorials-smoke)
set_tests_properties(FireCAEA10BlankTutorialUiTests PROPERTIES TIMEOUT 900)
add_test(NAME FireCAEA12ScenarioUiTests COMMAND FireCAEUiTests --a12-scenario-smoke)
add_test(NAME FireCAEA13NativeResultsUiTests COMMAND FireCAEUiTests --a13-native-results-smoke)
add_test(NAME FireCAEA13Smoke3dRenderTests COMMAND FireCAEUiTests --a13-render-cache-smoke)
set_tests_properties(FireCAEA13Smoke3dRenderTests PROPERTIES TIMEOUT 90)
add_test(NAME FireCAEA14GeometryImportUiTests COMMAND FireCAEUiTests --a14-import-smoke)
add_test(NAME FireCAEA15ReliabilityUiTests COMMAND FireCAEUiTests --a15-reliability-smoke)
add_test(NAME FireCAEA16ComparisonUiTests COMMAND FireCAEUiTests --a16-comparison-smoke)

add_executable(FireCAEFdsModelTests tests/fds_model_tests.cpp)
target_link_libraries(FireCAEFdsModelTests PRIVATE FireCAEFdsModel Qt6::Core)
target_compile_definitions(FireCAEFdsModelTests PRIVATE
    FIRECAE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
if(MSVC)
    target_compile_options(FireCAEFdsModelTests PRIVATE /W4 /permissive-)
endif()
add_test(NAME FireCAEFdsModelTests COMMAND FireCAEFdsModelTests)

add_executable(FireCAEFdsSceneTests tests/fds_scene_tests.cpp)
target_link_libraries(FireCAEFdsSceneTests PRIVATE FireCAEFdsModel Qt6::Core)
target_compile_definitions(FireCAEFdsSceneTests PRIVATE
    FIRECAE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
if(MSVC)
    target_compile_options(FireCAEFdsSceneTests PRIVATE /W4 /permissive-)
endif()
add_test(NAME FireCAEFdsSceneTests COMMAND FireCAEFdsSceneTests)

# Acceptance regressions exercise previously untested data-integrity boundaries.
add_executable(FireCAEReacceptanceModelTests tests/reacceptance_model_tests.cpp)
target_link_libraries(FireCAEReacceptanceModelTests PRIVATE FireCAEFdsModel FireCAEResults Qt6::Core)
add_test(NAME FireCAEReacceptanceModelTests COMMAND FireCAEReacceptanceModelTests)

add_executable(FireCAEReacceptanceScenarioUiTests tests/reacceptance_scenario_ui_tests.cpp
    src/ui/ScenarioManagerDialog.cpp src/ui/UiLanguage.cpp)
target_link_libraries(FireCAEReacceptanceScenarioUiTests PRIVATE FireCAEFdsModel FireCAEResults Qt6::Widgets)
add_test(NAME FireCAEReacceptanceScenarioUiTests COMMAND FireCAEReacceptanceScenarioUiTests)
add_executable(FireCAEReacceptanceGeometryUiTests tests/reacceptance_geometry_ui_tests.cpp ${FIRECAE_UI_SOURCES})
target_link_libraries(FireCAEReacceptanceGeometryUiTests PRIVATE
    FireCAEVisualization FireCAEImport FireCAEResults FireCAESimulation
    FireCAEResources FireCAEReliability FireCAEFdsModel Qt6::Concurrent user32)
add_test(NAME FireCAEReacceptanceGeometryUiTests COMMAND FireCAEReacceptanceGeometryUiTests)
set_tests_properties(FireCAEReacceptanceGeometryUiTests PROPERTIES TIMEOUT 90)
get_target_property(FIRECAE_ACCEPTANCE_QMAKE Qt6::qmake IMPORTED_LOCATION)
execute_process(COMMAND "${FIRECAE_ACCEPTANCE_QMAKE}" -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE FIRECAE_ACCEPTANCE_QT_PLUGINS OUTPUT_STRIP_TRAILING_WHITESPACE)
set_tests_properties(FireCAEReacceptanceScenarioUiTests PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${FIRECAE_ACCEPTANCE_QT_PLUGINS}/platforms")

# No "Tests" in this executable basename: the isolated probe exercises the
# production MainWindow settings restore/save path, which tests normally skip.
add_executable(FireCAEUxLayoutProbe tests/ux_workspace_layout_tests.cpp ${FIRECAE_UI_SOURCES})
target_link_libraries(FireCAEUxLayoutProbe PRIVATE
    FireCAEVisualization FireCAEImport FireCAEResults FireCAESimulation
    FireCAEResources FireCAEReliability FireCAEFdsModel Qt6::Concurrent user32)
add_executable(FireCAEUxIfcAppearanceTests tests/ux_ifc_appearance_tests.cpp)
target_link_libraries(FireCAEUxIfcAppearanceTests PRIVATE FireCAEVisualization FireCAEImport FireCAEFdsModel Qt6::Core)
target_compile_definitions(FireCAEUxIfcAppearanceTests PRIVATE FIRECAE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
add_test(NAME FireCAEUxIfcAppearanceTests COMMAND FireCAEUxIfcAppearanceTests)
set_tests_properties(FireCAEUxIfcAppearanceTests PROPERTIES TIMEOUT 180)
add_executable(FireCAEUxGeometryEditTests tests/ux_geometry_edit_tests.cpp)
add_executable(FireCAEUxSolverCase tests/ux_solver_case.cpp)
target_link_libraries(FireCAEUxSolverCase PRIVATE FireCAEFdsModel FireCAEGeometry Qt6::Core)
target_link_libraries(FireCAEUxGeometryEditTests PRIVATE FireCAEFdsModel FireCAEGeometry Qt6::Core)
add_test(NAME FireCAEUxGeometryEditTests COMMAND FireCAEUxGeometryEditTests)
add_executable(FireCAEUxGeometryDerivedUpdateTests tests/ux_geometry_derived_update_tests.cpp)
target_link_libraries(FireCAEUxGeometryDerivedUpdateTests PRIVATE FireCAEFdsModel FireCAEGeometry Qt6::Core)
add_test(NAME FireCAEUxGeometryDerivedUpdateTests COMMAND FireCAEUxGeometryDerivedUpdateTests)
add_executable(FireCAEUxInteractionTests tests/ux_interaction_tests.cpp ${FIRECAE_UI_SOURCES})
target_link_libraries(FireCAEUxInteractionTests PRIVATE
    FireCAEVisualization FireCAEImport FireCAEResults FireCAESimulation
    FireCAEResources FireCAEReliability FireCAEFdsModel Qt6::Concurrent user32)
add_test(NAME FireCAEUxInteractionTests COMMAND FireCAEUxInteractionTests)
set_tests_properties(FireCAEUxInteractionTests PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${FIRECAE_ACCEPTANCE_QT_PLUGINS}/platforms")
add_executable(FireCAEUxGeometryPropertiesTests tests/ux_geometry_properties_tests.cpp
    src/ui/BuildingElementDialog.cpp src/ui/BuildingElementDialog.h src/ui/UiLanguage.cpp)
target_link_libraries(FireCAEUxGeometryPropertiesTests PRIVATE FireCAEGeometry FireCAEFdsModel Qt6::Widgets)
add_test(NAME FireCAEUxGeometryPropertiesTests COMMAND FireCAEUxGeometryPropertiesTests)
set_tests_properties(FireCAEUxGeometryPropertiesTests PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${FIRECAE_ACCEPTANCE_QT_PLUGINS}/platforms")
if(MSVC)
    foreach(target FireCAEUxIfcAppearanceTests FireCAEUxGeometryEditTests FireCAEUxGeometryPropertiesTests FireCAEUxInteractionTests FireCAEUxGeometryDerivedUpdateTests)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
endif()
add_executable(FireCAEUxModelTreeTests tests/ux_model_tree_tests.cpp
    src/ui/ModelTreeWidget.cpp src/ui/ModelTreeWidget.h src/ui/UiLanguage.cpp)
target_link_libraries(FireCAEUxModelTreeTests PRIVATE FireCAEGeometry FireCAEFdsModel Qt6::Widgets)
add_executable(FireCAEGeometryImportLayoutTests tests/geometry_import_layout_tests.cpp
    src/ui/GeometryImportWizard.cpp src/ui/GeometryImportWizard.h src/ui/UiLanguage.cpp)
target_link_libraries(FireCAEGeometryImportLayoutTests PRIVATE FireCAEImport Qt6::Widgets Qt6::Concurrent)
if(MSVC)
    foreach(target FireCAEUxLayoutProbe FireCAEUxModelTreeTests FireCAEGeometryImportLayoutTests)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
endif()
add_test(NAME FireCAEUxWorkspaceLayoutTests COMMAND FireCAEUxLayoutProbe $<TARGET_FILE:FireCAEReacceptanceSolverFixture>)
add_test(NAME FireCAEUxModelTreeTests COMMAND FireCAEUxModelTreeTests)
set_tests_properties(FireCAEUxWorkspaceLayoutTests FireCAEUxModelTreeTests PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${FIRECAE_ACCEPTANCE_QT_PLUGINS}/platforms")
foreach(scale 1 1.25 1.5 2)
    add_test(NAME FireCAEGeometryImportLayoutTests_${scale} COMMAND FireCAEGeometryImportLayoutTests)
    set_tests_properties(FireCAEGeometryImportLayoutTests_${scale} PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${scale};QT_QPA_PLATFORM_PLUGIN_PATH=${FIRECAE_ACCEPTANCE_QT_PLUGINS}/platforms")
endforeach()

add_executable(FireCAEReacceptanceLayoutProbe
    tests/reacceptance_layout_tutorial_tests.cpp ${FIRECAE_UI_SOURCES})
target_link_libraries(FireCAEReacceptanceLayoutProbe PRIVATE
    FireCAEVisualization FireCAEImport FireCAEResults FireCAESimulation
    FireCAEResources FireCAEReliability FireCAEFdsModel Qt6::Concurrent user32)
if(MSVC)
    target_compile_options(FireCAEReacceptanceLayoutProbe PRIVATE /W4 /permissive- /utf-8)
endif()
add_test(NAME FireCAEReacceptanceLayoutTutorialTests COMMAND FireCAEReacceptanceLayoutProbe)
set_tests_properties(FireCAEReacceptanceLayoutTutorialTests PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${FIRECAE_ACCEPTANCE_QT_PLUGINS}/platforms")

add_executable(FireCAEReacceptanceNativeViewerTests
    tests/reacceptance_native_viewer_tests.cpp
    src/ui/SmokeviewHostWidget.cpp src/ui/SmokeviewHostWidget.h
    src/ui/NativeResultViewerWidget.cpp src/ui/NativeResultViewerWidget.h src/ui/UiLanguage.cpp)
target_link_libraries(FireCAEReacceptanceNativeViewerTests PRIVATE
    FireCAEResults FireCAEFdsModel Qt6::Widgets)
add_test(NAME FireCAEReacceptanceNativeViewerTests COMMAND FireCAEReacceptanceNativeViewerTests)
set_tests_properties(FireCAEReacceptanceNativeViewerTests PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${FIRECAE_ACCEPTANCE_QT_PLUGINS}/platforms")

add_executable(FireCAEReacceptanceTaskCenterTests
    tests/reacceptance_task_center_lifetime_tests.cpp
    src/ui/SimulationTaskCenterWidget.cpp src/ui/SimulationTaskCenterWidget.h src/ui/UiLanguage.cpp)
target_link_libraries(FireCAEReacceptanceTaskCenterTests PRIVATE FireCAESimulation Qt6::Widgets)
add_test(NAME FireCAEReacceptanceTaskCenterTests COMMAND FireCAEReacceptanceTaskCenterTests)
set_tests_properties(FireCAEReacceptanceTaskCenterTests PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${FIRECAE_ACCEPTANCE_QT_PLUGINS}/platforms")
set(FIRECAE_TASK_CENTER_RED_DIRECTORY "" CACHE PATH "Historical task center sources for red regression")
if(FIRECAE_TASK_CENTER_RED_DIRECTORY)
    add_executable(FireCAEReacceptanceTaskCenterRed EXCLUDE_FROM_ALL
        tests/reacceptance_task_center_lifetime_tests.cpp
        "${FIRECAE_TASK_CENTER_RED_DIRECTORY}/ui/SimulationTaskCenterWidget.cpp"
        "${FIRECAE_TASK_CENTER_RED_DIRECTORY}/ui/SimulationTaskCenterWidget.h" src/ui/UiLanguage.cpp)
    target_include_directories(FireCAEReacceptanceTaskCenterRed BEFORE PRIVATE "${FIRECAE_TASK_CENTER_RED_DIRECTORY}")
    target_link_libraries(FireCAEReacceptanceTaskCenterRed PRIVATE FireCAESimulation Qt6::Widgets)
    if(MSVC)
        target_compile_options(FireCAEReacceptanceTaskCenterRed PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()
# Optional historical source is used only to reproduce the original defect.
# It is excluded from normal builds, CTest and portable distribution.
set(FIRECAE_NATIVE_VIEWER_RED_SOURCE "" CACHE FILEPATH "Historical NativeResultViewerWidget.cpp for red regression")
if(FIRECAE_NATIVE_VIEWER_RED_SOURCE)
    add_executable(FireCAEReacceptanceNativeViewerRed EXCLUDE_FROM_ALL
        tests/reacceptance_native_viewer_tests.cpp "${FIRECAE_NATIVE_VIEWER_RED_SOURCE}"
        src/ui/SmokeviewHostWidget.cpp src/ui/SmokeviewHostWidget.h
        src/ui/NativeResultViewerWidget.h src/ui/UiLanguage.cpp)
    target_link_libraries(FireCAEReacceptanceNativeViewerRed PRIVATE
        FireCAEResults FireCAEFdsModel Qt6::Widgets)
    if(MSVC)
        target_compile_options(FireCAEReacceptanceNativeViewerRed PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

add_executable(FireCAEReacceptanceSolverFixture tests/reacceptance_solver_fixture.cpp)
set_target_properties(FireCAEReacceptanceSolverFixture PROPERTIES OUTPUT_NAME fds
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/regression-fixture")
add_executable(FireCAEReacceptanceSolverResultsTests tests/reacceptance_solver_results_tests.cpp)
target_link_libraries(FireCAEReacceptanceSolverResultsTests PRIVATE FireCAEResults FireCAESimulation Qt6::Core)
add_test(NAME FireCAEReacceptanceSolverResultsTests
    COMMAND FireCAEReacceptanceSolverResultsTests $<TARGET_FILE:FireCAEReacceptanceSolverFixture>)
set_tests_properties(FireCAEReacceptanceModelTests FireCAEReacceptanceSolverResultsTests PROPERTIES TIMEOUT 60)
if(MSVC)
    foreach(target FireCAEReacceptanceModelTests FireCAEReacceptanceScenarioUiTests FireCAEReacceptanceGeometryUiTests
        FireCAEReacceptanceSolverResultsTests FireCAEReacceptanceSolverFixture FireCAEReacceptanceNativeViewerTests FireCAEReacceptanceTaskCenterTests)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
endif()

add_executable(FireCAEBenchmarkGenerator tests/generate_simple_test.cpp)
target_link_libraries(FireCAEBenchmarkGenerator PRIVATE FireCAEFdsModel Qt6::Core)
if(MSVC)
    target_compile_options(FireCAEBenchmarkGenerator PRIVATE /W4 /permissive-)
endif()

add_executable(FireCAEFdsRoundTrip tests/roundtrip_fds.cpp)
target_link_libraries(FireCAEFdsRoundTrip PRIVATE FireCAEFdsModel Qt6::Core)
if(MSVC)
    target_compile_options(FireCAEFdsRoundTrip PRIVATE /W4 /permissive-)
endif()

add_executable(FireCAEComparisonReporter tools/comparison_reporter.cpp)
target_link_libraries(FireCAEComparisonReporter PRIVATE
    FireCAEResults FireCAEFdsModel Qt6::Widgets Qt6::PrintSupport)
if(MSVC)
    target_compile_options(FireCAEComparisonReporter PRIVATE /W4 /permissive-)
endif()

add_executable(FireCAESliceInspector tools/slice_inspector.cpp)
target_link_libraries(FireCAESliceInspector PRIVATE FireCAEResults Qt6::Core)
if(MSVC)
    target_compile_options(FireCAESliceInspector PRIVATE /W4 /permissive-)
endif()

set(FIRECAE_FDS_RUNTIME_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fds-smv-runtime/firemodels/FDS6/bin"
    CACHE PATH "Directory containing the optional FDS runtime")
if(WIN32 AND EXISTS "${FIRECAE_FDS_RUNTIME_DIR}/fds.exe")
    add_executable(FireCAEFdsRunnerTests tests/fds_runner_tests.cpp)
    target_link_libraries(FireCAEFdsRunnerTests PRIVATE FireCAESimulation Qt6::Core)
    target_compile_definitions(FireCAEFdsRunnerTests PRIVATE
        FIRECAE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
        FIRECAE_TEST_FDS_EXECUTABLE="${FIRECAE_FDS_RUNTIME_DIR}/fds.exe")
    if(MSVC)
        target_compile_options(FireCAEFdsRunnerTests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME FireCAEFdsRunnerTests COMMAND FireCAEFdsRunnerTests)

    add_executable(FireCAEBenchmarkRunner tests/run_generated_case.cpp)
    target_link_libraries(FireCAEBenchmarkRunner PRIVATE
        FireCAESimulation FireCAEResults Qt6::Core)
    if(MSVC)
        target_compile_options(FireCAEBenchmarkRunner PRIVATE /W4 /permissive-)
    endif()

    add_custom_command(TARGET FireCAE POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:FireCAE>/fds/mpi"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FIRECAE_FDS_RUNTIME_DIR}/fds.exe"
            "${FIRECAE_FDS_RUNTIME_DIR}/fds_openmp.exe"
            "${FIRECAE_FDS_RUNTIME_DIR}/libiomp5md.dll"
            "$<TARGET_FILE_DIR:FireCAE>/fds"
        COMMAND "${CMAKE_COMMAND}" -E ${FIRECAE_COPY_RUNTIME_DIRECTORY}
            "${FIRECAE_FDS_RUNTIME_DIR}/mpi"
            "$<TARGET_FILE_DIR:FireCAE>/fds/mpi"
        COMMENT "Deploying the FDS solver runtime"
        VERBATIM)
else()
    message(STATUS
        "FDS runtime not bundled; FireCAE will use user configuration or PATH.")
endif()

if(WIN32)
    foreach(target FireCAE FireCAEUiTests)
        foreach(runtime_dll IN LISTS FIRECAE_OCCT_RUNTIME_DLLS)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${runtime_dll}" "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        endforeach()
    endforeach()

    set(FIRECAE_IFCCONVERT_EXECUTABLE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ifcopenshell/IfcConvert.exe"
        CACHE FILEPATH "Path to the standalone IfcOpenShell IfcConvert executable")
    if(NOT EXISTS "${FIRECAE_IFCCONVERT_EXECUTABLE}")
        message(FATAL_ERROR
            "IfcConvert was not found: ${FIRECAE_IFCCONVERT_EXECUTABLE}")
    endif()
    get_filename_component(FIRECAE_IFCCONVERT_DIR
        "${FIRECAE_IFCCONVERT_EXECUTABLE}" DIRECTORY)
    set(FIRECAE_IFCCONVERT_NOTICE_FILES
        "${FIRECAE_IFCCONVERT_DIR}/COPYING"
        "${FIRECAE_IFCCONVERT_DIR}/COPYING.LESSER"
        "${FIRECAE_IFCCONVERT_DIR}/NOTICE.md")
    foreach(notice_file IN LISTS FIRECAE_IFCCONVERT_NOTICE_FILES)
        if(NOT EXISTS "${notice_file}")
            message(FATAL_ERROR "IfcOpenShell notice file was not found: ${notice_file}")
        endif()
    endforeach()
    foreach(target FireCAE FireCAECoreTests FireCAEUiTests)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:${target}>/ifcopenshell"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${FIRECAE_IFCCONVERT_EXECUTABLE}"
                "$<TARGET_FILE_DIR:${target}>/ifcopenshell/IfcConvert.exe"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${FIRECAE_IFCCONVERT_NOTICE_FILES}
                "$<TARGET_FILE_DIR:${target}>/ifcopenshell"
            COMMENT "Deploying the isolated IfcOpenShell IFC worker"
            VERBATIM)
    endforeach()
endif()

set(FIRECAE_SMOKEVIEW_RUNTIME_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fds-smv-runtime/firemodels/SMV6"
    CACHE PATH "Directory containing the optional Smokeview runtime")
if(WIN32 AND EXISTS "${FIRECAE_SMOKEVIEW_RUNTIME_DIR}/smokeview.exe")
    foreach(target FireCAE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:${target}>/smokeview"
            COMMAND "${CMAKE_COMMAND}" -E ${FIRECAE_COPY_RUNTIME_DIRECTORY}
                "${FIRECAE_SMOKEVIEW_RUNTIME_DIR}"
                "$<TARGET_FILE_DIR:${target}>/smokeview"
            COMMENT "Deploying the optional Smokeview runtime"
            VERBATIM)
    endforeach()
else()
    message(STATUS
        "Smokeview runtime not bundled; FireCAE will use user configuration or PATH.")
endif()

if(WIN32 AND TARGET Qt6::windeployqt)
    # The application also uses Qt-owned standard dialogs and buttons.
    # Ship the Chinese Qt catalog even though other runtime translations are
    # intentionally excluded by windeployqt below.
    get_target_property(FIRECAE_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    execute_process(COMMAND "${FIRECAE_QMAKE_EXECUTABLE}" -query QT_INSTALL_TRANSLATIONS
        OUTPUT_VARIABLE FIRECAE_QT_TRANSLATIONS_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND "${FIRECAE_QMAKE_EXECUTABLE}" -query QT_INSTALL_PLUGINS
        OUTPUT_VARIABLE FIRECAE_QT_PLUGINS_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    # Release deployment contains qwindows only. UI regression tests must use
    # the installed offscreen plugin, and its path must point at platforms.
    set_property(TEST FireCAEUiLanguageTests FireCAEUiFeedbackTests FireCAEM07ObjectTypeSwitchTests APPEND PROPERTY
        ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${FIRECAE_QT_PLUGINS_DIR}/platforms")
    set(FIRECAE_QT_CHINESE_CATALOG "${FIRECAE_QT_TRANSLATIONS_DIR}/qtbase_zh_CN.qm")
    if(NOT EXISTS "${FIRECAE_QT_CHINESE_CATALOG}")
        message(FATAL_ERROR "Chinese Qt catalog not found: ${FIRECAE_QT_CHINESE_CATALOG}")
    endif()
    foreach(target FireCAE FireCAEUiTests FireCAEUiLanguageTests)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/translations"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${FIRECAE_QT_CHINESE_CATALOG}" "$<TARGET_FILE_DIR:${target}>/translations/qtbase_zh_CN.qm"
            VERBATIM)
    endforeach()
    add_custom_command(TARGET FireCAE POST_BUILD
        COMMAND Qt6::windeployqt
            --no-translations
            --dir "$<TARGET_FILE_DIR:FireCAE>"
            "$<TARGET_FILE:FireCAE>"
        COMMENT "Deploying the Qt runtime next to FireCAE"
        VERBATIM)
endif()

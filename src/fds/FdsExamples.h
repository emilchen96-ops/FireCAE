#pragma once

#include <memory>

class FcProject;

namespace FdsExamples
{
std::unique_ptr<FcProject> createSimpleTestProject();
std::unique_ptr<FcProject> createActivateVentsProject();
std::unique_ptr<FcProject> createBucketTest2Project();
std::unique_ptr<FcProject> createCouchProject();
std::unique_ptr<FcProject> createCouchSmoke12sProject();
std::unique_ptr<FcProject> createHvacAircoilProject();
std::unique_ptr<FcProject> createTunnelDemoProject();
std::unique_ptr<FcProject> createTunnelSmoke10sProject();
}

// Shared fixtures for the test suite.
#pragma once

#include <cadgeom/CadGeomRAII.h>

#include <catch2/catch_test_macros.hpp>

namespace cgtest {

/// An engine plus the leak check that M0 exists to defend.
///
/// The count is compared against the baseline taken at construction rather than
/// against zero, so a leak in one test cannot cascade into failures in every
/// test that runs after it.
class EngineFixture {
public:
    EngineFixture() : baseline_(CadGeom_GetLiveObjectCount()) {
        cadgeom::EngineDesc desc{};
        desc.applicationName = "cadgeom_tests";
        desc.logLevel = cadgeom::LogLevel::Off;
        engine_ = cadgeom::CreateEngine(desc);
        REQUIRE(engine_);
    }

    ~EngineFixture() {
        engine_.reset();
        CHECK(CadGeom_GetLiveObjectCount() == baseline_);
    }

    EngineFixture(const EngineFixture&) = delete;
    EngineFixture& operator=(const EngineFixture&) = delete;

    cadgeom::ICadEngine& Engine() { return *engine_; }
    cadgeom::IScene& Scene() { return *engine_->GetScene(); }

private:
    uint64_t baseline_;
    cadgeom::EnginePtr engine_;
};

} // namespace cgtest

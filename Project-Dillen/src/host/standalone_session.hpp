#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "authoring_pipeline.hpp"
#include "authoritative_world.hpp"
#include "initial_world_builder.hpp"
#include "kernel_runtime.hpp"

namespace dillen::host {

struct StandaloneSourceLayerConfig
{
    std::string name;
    std::filesystem::path root;
    int priority = 0;
    std::vector<std::string> replacePaths;
    std::string virtualPrefix;
    std::vector<std::string> includePatterns;
};

struct StandaloneSessionConfig
{
    std::vector<StandaloneSourceLayerConfig> sources;
    std::filesystem::path contentRoot;
    std::string sourceLayerName = "standalone_content";
    int sourcePriority = 0;
    authoring::AuthoringLaunchSelection rulesets;
};

enum class StandaloneSessionStatus
{
    Ready,
    InvalidConfiguration,
    FrontendRegistrationFailed,
    CatalogBuildFailed,
    ParseFailed,
    ResolveFailed,
    WorldBuildFailed
};

struct StandaloneSessionReport
{
    StandaloneSessionStatus status = StandaloneSessionStatus::Ready;
    std::string message;
    std::vector<std::string> diagnostics;
    std::vector<world::InitialWorldBuildIssue> worldIssues;

    explicit operator bool() const noexcept;
};

class StandaloneSession
{
public:
    bool Start(
        const StandaloneSessionConfig& config,
        StandaloneSessionReport& report
    );
    void Reset();

    bool IsReady() const noexcept;
    const kernel::FrozenRuntimeCatalog& Catalog() const;
    // Presentation, which is not in the Runtime Catalog and must not be: a
    // Presentation Package is outside the determinism closure, so it has its
    // own identity rather than a place in the one a save validates against.
    const std::vector<kernel::PresentationAsset>& PresentationAssets() const;
    kernel::PresentationFingerprint PresentationFingerprint() const;
    const world::AuthoritativeWorld& World() const;
    runtime::KernelRuntime& Runtime();
    const runtime::KernelRuntime& Runtime() const;

private:
    std::unique_ptr<authoring::AuthoringSession> authoring_;
    world::AuthoritativeWorld world_;
    std::unique_ptr<runtime::KernelRuntime> runtime_;
};

}

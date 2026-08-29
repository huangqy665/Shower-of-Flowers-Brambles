#include "standalone_session.hpp"

#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "resolver.hpp"
#include "template_registry.hpp"

namespace dillen::host {

namespace {

void AppendDiagnostics(
    const parser::DiagnosticBag& diagnostics,
    const parser::ParseWorkspace& workspace,
    std::vector<std::string>& output
)
{
    std::map<parser::SourceId, std::string> paths;
    for (const parser::ParsedFile& file : workspace.files)
    {
        paths.emplace(
            file.source.Id(),
            std::string(file.source.VirtualPath())
        );
    }
    for (const parser::Diagnostic& diagnostic : diagnostics.All())
    {
        const auto path = paths.find(diagnostic.span.begin.source);
        output.push_back(parser::FormatDiagnostic(
            diagnostic,
            path == paths.end() ? std::string_view{} : path->second
        ));
    }
}

bool ValidRulesetSelection(
    const authoring::SelectedRulesetVersion& selection
)
{
    return selection.id
        && !selection.canonicalName.empty()
        && selection.version != 0;
}

}

StandaloneSessionReport::operator bool() const noexcept
{
    return status == StandaloneSessionStatus::Ready;
}

bool StandaloneSession::Start(
    const StandaloneSessionConfig& config,
    StandaloneSessionReport& report
)
{
    Reset();
    report = {};
    std::vector<StandaloneSourceLayerConfig> sources = config.sources;
    if (sources.empty() && !config.contentRoot.empty())
    {
        sources.push_back({
            config.sourceLayerName,
            config.contentRoot,
            config.sourcePriority,
            {},
            {},
            {}
        });
    }
    if (sources.empty() || !ValidRulesetSelection(config.rulesets.root))
    {
        report.status = StandaloneSessionStatus::InvalidConfiguration;
        report.message =
            "at least one Source Layer and a Root Ruleset are required";
        return false;
    }
    std::set<std::string> sourceNames;
    for (const StandaloneSourceLayerConfig& source : sources)
    {
        std::error_code pathError;
        if (source.name.empty()
            || source.root.empty()
            || !sourceNames.emplace(source.name).second
            || !std::filesystem::is_directory(source.root, pathError)
            || pathError)
        {
            report.status = StandaloneSessionStatus::InvalidConfiguration;
            report.message = "Source Layers must be unique directories";
            return false;
        }
    }
    for (const authoring::SelectedRulesetVersion& extension
        : config.rulesets.extensions)
    {
        if (!ValidRulesetSelection(extension))
        {
            report.status = StandaloneSessionStatus::InvalidConfiguration;
            report.message = "every Ruleset extension must be versioned";
            return false;
        }
    }

    authoring_ = std::make_unique<authoring::AuthoringSession>(
        config.rulesets
    );
    parser::TemplateRegistry templates;
    parser::ParserRegistry parsers;
    parser::Resolver resolver;
    if (!authoring_->Register(templates, parsers, resolver))
    {
        report.status =
            StandaloneSessionStatus::FrontendRegistrationFailed;
        report.message = "Dillen Authoring frontend registration failed";
        Reset();
        return false;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    parser::DiagnosticBag diagnostics;
    parser::FileCatalog fileCatalog;
    parser::SourceLayerId sourceId = 1;
    for (StandaloneSourceLayerConfig& source : sources)
    {
        parser::SourceLayer layer;
        layer.id = sourceId++;
        layer.name = std::move(source.name);
        layer.root = std::move(source.root);
        layer.priority = source.priority;
        layer.replacePaths = std::move(source.replacePaths);
        layer.virtualPrefix = std::move(source.virtualPrefix);
        layer.includePatterns = std::move(source.includePatterns);
        if (!fileCatalog.AddLayer(std::move(layer)))
        {
            report.status = StandaloneSessionStatus::CatalogBuildFailed;
            report.message = "Source Layer registration failed";
            Reset();
            return false;
        }
    }
    if (!fileCatalog.Build(templates, diagnostics))
    {
        parser::ParseWorkspace emptyWorkspace;
        AppendDiagnostics(diagnostics, emptyWorkspace, report.diagnostics);
        report.status = StandaloneSessionStatus::CatalogBuildFailed;
        report.message = "external Authoring source catalog failed";
        Reset();
        return false;
    }

    parser::ParseWorkspace workspace;
    if (!fileCatalog.Parse(parsers, workspace, diagnostics))
    {
        AppendDiagnostics(diagnostics, workspace, report.diagnostics);
        report.status = StandaloneSessionStatus::ParseFailed;
        report.message = "external Authoring source parsing failed";
        Reset();
        return false;
    }
    if (!resolver.Resolve(workspace, diagnostics)
        || diagnostics.HasErrors())
    {
        AppendDiagnostics(diagnostics, workspace, report.diagnostics);
        report.status = StandaloneSessionStatus::ResolveFailed;
        report.message = "external Authoring resolution or compile failed";
        Reset();
        return false;
    }
    AppendDiagnostics(diagnostics, workspace, report.diagnostics);

    world::InitialWorldBuildReport worldReport;
    if (!world::InitialWorldBuilder{}.Build(
            authoring_->RuntimeCatalog(),
            world_,
            worldReport))
    {
        report.status = StandaloneSessionStatus::WorldBuildFailed;
        report.message = "initial Authoritative World construction failed";
        report.worldIssues = std::move(worldReport.issues);
        Reset();
        return false;
    }
    runtime_ = std::make_unique<runtime::KernelRuntime>(
        world_,
        authoring_->RuntimeCatalog()
    );
    report.status = StandaloneSessionStatus::Ready;
    report.message = "Standalone Dillen session is ready";
    return true;
}

void StandaloneSession::Reset()
{
    runtime_.reset();
    world_ = {};
    authoring_.reset();
}

bool StandaloneSession::IsReady() const noexcept
{
    return runtime_ != nullptr && authoring_ != nullptr;
}

const kernel::FrozenRuntimeCatalog& StandaloneSession::Catalog() const
{
    if (!IsReady())
    {
        throw std::logic_error("Standalone session is not ready");
    }
    return authoring_->RuntimeCatalog();
}

const world::AuthoritativeWorld& StandaloneSession::World() const
{
    if (!IsReady())
    {
        throw std::logic_error("Standalone session is not ready");
    }
    return world_;
}

runtime::KernelRuntime& StandaloneSession::Runtime()
{
    if (!IsReady())
    {
        throw std::logic_error("Standalone session is not ready");
    }
    return *runtime_;
}

const runtime::KernelRuntime& StandaloneSession::Runtime() const
{
    if (!IsReady())
    {
        throw std::logic_error("Standalone session is not ready");
    }
    return *runtime_;
}

}

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "diagnostic.hpp"
#include "parse_result.hpp"
#include "parser_registry.hpp"
#include "source_buffer.hpp"
#include "template.hpp"
#include "template_registry.hpp"

namespace dillen::parser {

using SourceLayerId = std::uint32_t;

enum class CatalogDisposition
{
    Active,
    Unclassified,
    ShadowedByFile,
    HiddenByReplacePath,
    ReadError
};

struct SourceLayer
{
    SourceLayerId id = 0;
    std::string name;
    std::filesystem::path root;
    int priority = 0;
    std::vector<std::string> replacePaths;
    std::string virtualPrefix;
    std::vector<std::string> includePatterns;
};

struct CatalogOptions
{
    bool caseInsensitivePaths = true;
    std::size_t probeBytes = 16 * 1024;
};

struct CatalogFile
{
    std::string virtualPath;
    std::filesystem::path physicalPath;
    SourceLayerId sourceLayer = 0;
    std::string sourceLayerName;
    int sourcePriority = 0;
    std::uintmax_t size = 0;
    std::uint64_t fingerprint = 0;
    SourceEncoding encoding = SourceEncoding::Unknown;
    CatalogDisposition disposition = CatalogDisposition::Unclassified;
    std::optional<TemplateMatch> match;
    SourceLayerId displacedByLayer = 0;
};

struct ParsedFile
{
    CatalogFile catalog;
    SourceBuffer source;
    ParseResult result;
};

struct ParseWorkspace
{
    std::vector<ParsedFile> files;

    void Clear();
};

class FileCatalog
{
public:
    explicit FileCatalog(CatalogOptions options = {});

    bool AddLayer(SourceLayer layer);
    void ClearLayers();
    bool Build(
        const TemplateRegistry& templates,
        DiagnosticBag& diagnostics
    );
    bool Parse(
        const ParserRegistry& parsers,
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics
    ) const;
    bool IsBuilt() const noexcept;
    const std::vector<SourceLayer>& Layers() const noexcept;
    const std::vector<CatalogFile>& Files() const noexcept;
    std::size_t ActiveClassifiedFileCount() const noexcept;

    static std::string NormalizeVirtualPath(
        std::string_view path,
        bool caseInsensitive = true
    );

private:
    CatalogOptions options_;
    std::vector<SourceLayer> layers_;
    std::vector<CatalogFile> files_;
    bool built_ = false;
};

}

#include "file_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <unordered_set>
#include <utility>

#include "path_pattern.hpp"

namespace dillen::parser {

namespace {

bool HasHigherPrecedence(
    const SourceLayer& first,
    const SourceLayer& second
)
{
    return first.priority > second.priority
        || (first.priority == second.priority
            && first.id > second.id);
}

bool PathIsInside(
    std::string_view path,
    std::string_view root
)
{
    return path == root
        || (path.size() > root.size()
            && path.compare(0, root.size(), root) == 0
            && path[root.size()] == '/');
}

std::uint64_t HashBytes(std::string_view bytes)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : bytes)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool ReadFileBytes(
    const std::filesystem::path& path,
    std::string& output
)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }
    output.assign(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    );
    return stream.good() || stream.eof();
}

bool IncludedByLayer(
    const SourceLayer& layer,
    std::string_view relativePath
)
{
    return layer.includePatterns.empty()
        || std::any_of(
            layer.includePatterns.begin(),
            layer.includePatterns.end(),
            [relativePath](const std::string& pattern)
            {
                return MatchPathPattern(pattern, relativePath);
            }
        );
}

std::string MountedVirtualPath(
    const SourceLayer& layer,
    std::string_view relativePath
)
{
    if (layer.virtualPrefix.empty())
    {
        return std::string(relativePath);
    }
    std::string mounted = layer.virtualPrefix;
    mounted.push_back('/');
    mounted.append(relativePath);
    return mounted;
}

const SourceLayer* FindLayer(
    const std::vector<SourceLayer>& layers,
    SourceLayerId id
)
{
    const auto iterator = std::find_if(
        layers.begin(),
        layers.end(),
        [id](const SourceLayer& layer)
        {
            return layer.id == id;
        }
    );
    return iterator == layers.end() ? nullptr : &(*iterator);
}

}

FileCatalog::FileCatalog(CatalogOptions options)
    : options_(options)
{
}

bool FileCatalog::AddLayer(SourceLayer layer)
{
    if (built_
        || layer.id == 0
        || layer.name.empty()
        || layer.root.empty()
        || std::any_of(
            layers_.begin(),
            layers_.end(),
            [&layer](const SourceLayer& item)
            {
                return item.id == layer.id;
            }))
    {
        return false;
    }
    for (std::string& replacePath : layer.replacePaths)
    {
        replacePath = NormalizeVirtualPath(
            replacePath,
            options_.caseInsensitivePaths
        );
    }
    layer.virtualPrefix = NormalizeVirtualPath(
        layer.virtualPrefix,
        options_.caseInsensitivePaths
    );
    for (std::string& includePattern : layer.includePatterns)
    {
        includePattern = NormalizeVirtualPath(
            includePattern,
            options_.caseInsensitivePaths
        );
    }
    layers_.push_back(std::move(layer));
    return true;
}

void FileCatalog::ClearLayers()
{
    layers_.clear();
    files_.clear();
    built_ = false;
}

bool FileCatalog::Build(
    const TemplateRegistry& templates,
    DiagnosticBag& diagnostics
)
{
    files_.clear();
    built_ = false;
    if (!templates.IsFrozen())
    {
        diagnostics.Fatal(
            "file_catalog.templates_not_frozen",
            "template registry must be frozen before catalog construction"
        );
        return false;
    }

    std::vector<SourceLayer> orderedLayers = layers_;
    std::sort(
        orderedLayers.begin(),
        orderedLayers.end(),
        [](const SourceLayer& first, const SourceLayer& second)
        {
            if (first.priority != second.priority)
            {
                return first.priority < second.priority;
            }
            return first.id < second.id;
        }
    );

    for (const SourceLayer& layer : orderedLayers)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(layer.root, error))
        {
            diagnostics.Error(
                "file_catalog.layer_missing",
                "source layer root is not a readable directory: "
                    + layer.root.u8string()
            );
            continue;
        }

        std::filesystem::recursive_directory_iterator iterator(
            layer.root,
            std::filesystem::directory_options::skip_permission_denied,
            error
        );
        const std::filesystem::recursive_directory_iterator end;
        for (; iterator != end; iterator.increment(error))
        {
            if (error)
            {
                diagnostics.Warning(
                    "file_catalog.enumeration_error",
                    "failed while enumerating source layer " + layer.name
                );
                error.clear();
                continue;
            }
            if (!iterator->is_regular_file(error))
            {
                error.clear();
                continue;
            }

            const std::filesystem::path relative =
                std::filesystem::relative(iterator->path(), layer.root, error);
            if (error)
            {
                diagnostics.Warning(
                    "file_catalog.relative_path_failed",
                    "could not create virtual path for "
                        + iterator->path().u8string()
                );
                error.clear();
                continue;
            }

            const std::string relativePath = NormalizeVirtualPath(
                relative.generic_u8string(),
                options_.caseInsensitivePaths
            );
            if (!IncludedByLayer(layer, relativePath))
            {
                continue;
            }

            CatalogFile file;
            file.virtualPath = MountedVirtualPath(layer, relativePath);
            file.physicalPath = iterator->path();
            file.sourceLayer = layer.id;
            file.sourceLayerName = layer.name;
            file.sourcePriority = layer.priority;
            file.size = iterator->file_size(error);
            if (error)
            {
                file.size = 0;
                error.clear();
            }

            std::string bytes;
            if (!ReadFileBytes(file.physicalPath, bytes))
            {
                file.disposition = CatalogDisposition::ReadError;
                diagnostics.Error(
                    "file_catalog.read_failed",
                    "could not read " + file.physicalPath.u8string()
                );
            }
            else
            {
                file.fingerprint = HashBytes(bytes);
                file.encoding = DetectSourceEncoding(bytes);
                const std::string_view probe(
                    bytes.data(),
                    std::min(options_.probeBytes, bytes.size())
                );
                file.match = templates.Match(file.virtualPath, probe);
            }
            files_.push_back(std::move(file));
        }
    }

    for (CatalogFile& file : files_)
    {
        if (file.disposition == CatalogDisposition::ReadError)
        {
            continue;
        }
        const SourceLayer* ownLayer = FindLayer(layers_, file.sourceLayer);
        if (ownLayer == nullptr)
        {
            continue;
        }
        for (const SourceLayer& layer : layers_)
        {
            if (!HasHigherPrecedence(layer, *ownLayer))
            {
                continue;
            }
            const auto replace = std::find_if(
                layer.replacePaths.begin(),
                layer.replacePaths.end(),
                [&file](const std::string& path)
                {
                    return PathIsInside(file.virtualPath, path);
                }
            );
            if (replace != layer.replacePaths.end())
            {
                file.disposition = CatalogDisposition::HiddenByReplacePath;
                file.displacedByLayer = layer.id;
                break;
            }
        }
    }

    std::sort(
        files_.begin(),
        files_.end(),
        [](const CatalogFile& first, const CatalogFile& second)
        {
            if (first.virtualPath != second.virtualPath)
            {
                return first.virtualPath < second.virtualPath;
            }
            if (first.sourcePriority != second.sourcePriority)
            {
                return first.sourcePriority > second.sourcePriority;
            }
            if (first.sourceLayer != second.sourceLayer)
            {
                return first.sourceLayer > second.sourceLayer;
            }
            return first.physicalPath.generic_u8string()
                < second.physicalPath.generic_u8string();
        }
    );

    std::size_t groupBegin = 0;
    while (groupBegin < files_.size())
    {
        std::size_t groupEnd = groupBegin + 1;
        while (groupEnd < files_.size()
            && files_[groupEnd].virtualPath
                == files_[groupBegin].virtualPath)
        {
            ++groupEnd;
        }

        CatalogFile* winner = nullptr;
        for (std::size_t index = groupBegin;
            index < groupEnd;
            ++index)
        {
            CatalogFile& candidate = files_[index];
            if (candidate.disposition
                    == CatalogDisposition::HiddenByReplacePath
                || candidate.disposition == CatalogDisposition::ReadError)
            {
                continue;
            }
            if (winner == nullptr)
            {
                winner = &candidate;
                candidate.disposition = candidate.match
                    ? CatalogDisposition::Active
                    : CatalogDisposition::Unclassified;
            }
            else
            {
                candidate.disposition = CatalogDisposition::ShadowedByFile;
                candidate.displacedByLayer = winner->sourceLayer;
            }
        }
        groupBegin = groupEnd;
    }

    built_ = true;
    return !diagnostics.HasErrors();
}

bool FileCatalog::IsBuilt() const noexcept
{
    return built_;
}

const std::vector<SourceLayer>& FileCatalog::Layers() const noexcept
{
    return layers_;
}

const std::vector<CatalogFile>& FileCatalog::Files() const noexcept
{
    return files_;
}

std::size_t FileCatalog::ActiveClassifiedFileCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        files_.begin(),
        files_.end(),
        [](const CatalogFile& file)
        {
            return file.disposition == CatalogDisposition::Active
                && file.match.has_value();
        }
    ));
}

std::string FileCatalog::NormalizeVirtualPath(
    std::string_view path,
    bool caseInsensitive
)
{
    std::string normalized;
    normalized.reserve(path.size());
    bool lastWasSlash = false;
    for (char character : path)
    {
        char output = character == '\\' ? '/' : character;
        if (output == '/')
        {
            if (lastWasSlash)
            {
                continue;
            }
            lastWasSlash = true;
        }
        else
        {
            lastWasSlash = false;
            if (caseInsensitive)
            {
                output = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(output)
                ));
            }
        }
        normalized.push_back(output);
    }
    while (normalized.compare(0, 2, "./") == 0)
    {
        normalized.erase(0, 2);
    }
    while (!normalized.empty() && normalized.back() == '/')
    {
        normalized.pop_back();
    }
    return normalized;
}

}

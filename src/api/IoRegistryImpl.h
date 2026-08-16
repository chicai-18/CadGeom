#pragma once

#include <cadgeom/IImportExport.h>

#include "core/ObjectTracker.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace cadgeom::api {

class EngineImpl;

/// The extension point itself is complete as of M0 — registration, ownership,
/// extension dispatch. What is missing is any handler to dispatch *to*: the OBJ
/// and glTF readers land in M5 (docs/architecture.md §7). A host that already
/// has its own format can register it today and it will work.
class IoRegistryImpl final : public IIoRegistry {
public:
    explicit IoRegistryImpl(EngineImpl& engine);
    ~IoRegistryImpl() override;

    CgResult Register(const char* extension, IImporter* importer,
                      IExporter* exporter) override;
    CgResult Unregister(const char* extension) override;

    uint32_t GetFormatCount() const override;
    const char* GetFormatExtension(uint32_t index) const override;
    bool CanImport(const char* extension) const override;
    bool CanExport(const char* extension) const override;

    CgResult Import(const char* utf8Path, const ImportOptions& options,
                    IoProgressCallback progress, void* userData) override;
    CgResult Export(const char* utf8Path, const ExportOptions& options,
                    IoProgressCallback progress, void* userData) override;

private:
    struct Handlers {
        IImporter* importer = nullptr;
        IExporter* exporter = nullptr;
    };

    /// Lowercased, dot-stripped. "OBJ", ".obj" and "obj" are the same format.
    static std::string Normalize(const char* extension);
    /// Extension of a path, normalized. Empty when the path has none.
    static std::string ExtensionOf(const char* path);
    /// Extension implied by an explicit FileFormat, empty for Auto.
    static std::string ExtensionOfFormat(FileFormat format);

    const Handlers* Lookup(const std::string& ext) const;

    core::ObjectTracker tracker_;
    EngineImpl& engine_;

    std::unordered_map<std::string, Handlers> formats_;
    /// Registration order, so GetFormatExtension() indexes something stable.
    std::vector<std::string> order_;
};

} // namespace cadgeom::api

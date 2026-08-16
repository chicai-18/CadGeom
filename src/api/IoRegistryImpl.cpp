#include "api/IoRegistryImpl.h"

#include "api/EngineImpl.h"
#include "core/Error.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>

namespace cadgeom::api {

IoRegistryImpl::IoRegistryImpl(EngineImpl& engine) : engine_(engine) {}

IoRegistryImpl::~IoRegistryImpl() {
    for (auto& [ext, handlers] : formats_) {
        if (handlers.importer) {
            handlers.importer->Release();
        }
        if (handlers.exporter) {
            handlers.exporter->Release();
        }
    }
    formats_.clear();
}

std::string IoRegistryImpl::Normalize(const char* extension) {
    if (!extension) {
        return {};
    }
    std::string s(extension);
    if (!s.empty() && s.front() == '.') {
        s.erase(s.begin());
    }
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string IoRegistryImpl::ExtensionOf(const char* path) {
    if (!path) {
        return {};
    }
    const std::string s(path);
    const size_t dot = s.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    // A dot in a directory name is not an extension ("../model" has none).
    const size_t sep = s.find_last_of("/\\");
    if (sep != std::string::npos && dot < sep) {
        return {};
    }
    return Normalize(s.c_str() + dot + 1);
}

std::string IoRegistryImpl::ExtensionOfFormat(FileFormat format) {
    switch (format) {
        case FileFormat::Obj: return "obj";
        case FileFormat::Gltf: return "gltf";
        case FileFormat::Glb: return "glb";
        case FileFormat::Auto: break;
    }
    return {};
}

const IoRegistryImpl::Handlers* IoRegistryImpl::Lookup(const std::string& ext) const {
    const auto it = formats_.find(ext);
    return it != formats_.end() ? &it->second : nullptr;
}

CgResult IoRegistryImpl::Register(const char* extension, IImporter* importer,
                                  IExporter* exporter) {
    const std::string ext = Normalize(extension);
    if (ext.empty()) {
        return core::SetError(CgResult::InvalidArgument, "Register: empty extension");
    }
    if (!importer && !exporter) {
        return core::SetError(CgResult::InvalidArgument,
                              "Register: '%s' has neither an importer nor an exporter",
                              ext.c_str());
    }

    auto [it, inserted] = formats_.try_emplace(ext);
    if (inserted) {
        order_.push_back(ext);
    }

    // Replacing one direction leaves the other in place; releasing only what is
    // actually superseded keeps a host from losing its exporter by re-registering
    // an importer.
    if (importer) {
        if (it->second.importer && it->second.importer != importer) {
            it->second.importer->Release();
        }
        it->second.importer = importer;
    }
    if (exporter) {
        if (it->second.exporter && it->second.exporter != exporter) {
            it->second.exporter->Release();
        }
        it->second.exporter = exporter;
    }

    CG_DEBUG("registered format '%s' (import=%d export=%d)", ext.c_str(),
             it->second.importer != nullptr, it->second.exporter != nullptr);
    return CgResult::Ok;
}

CgResult IoRegistryImpl::Unregister(const char* extension) {
    const std::string ext = Normalize(extension);
    const auto it = formats_.find(ext);
    if (it == formats_.end()) {
        return core::SetError(CgResult::NotFound, "Unregister: '%s' is not registered",
                              ext.c_str());
    }

    if (it->second.importer) {
        it->second.importer->Release();
    }
    if (it->second.exporter) {
        it->second.exporter->Release();
    }
    formats_.erase(it);
    order_.erase(std::remove(order_.begin(), order_.end(), ext), order_.end());
    return CgResult::Ok;
}

uint32_t IoRegistryImpl::GetFormatCount() const {
    return static_cast<uint32_t>(order_.size());
}

const char* IoRegistryImpl::GetFormatExtension(uint32_t index) const {
    return index < order_.size() ? order_[index].c_str() : nullptr;
}

bool IoRegistryImpl::CanImport(const char* extension) const {
    const Handlers* h = Lookup(Normalize(extension));
    return h && h->importer;
}

bool IoRegistryImpl::CanExport(const char* extension) const {
    const Handlers* h = Lookup(Normalize(extension));
    return h && h->exporter;
}

CgResult IoRegistryImpl::Import(const char* utf8Path, const ImportOptions& options,
                                IoProgressCallback progress, void* userData) {
    if (!utf8Path || !*utf8Path) {
        return core::SetError(CgResult::InvalidArgument, "Import: empty path");
    }

    std::string ext = ExtensionOfFormat(options.format);
    if (ext.empty()) {
        ext = ExtensionOf(utf8Path);
    }
    if (ext.empty()) {
        return core::SetError(CgResult::InvalidArgument,
                              "Import: cannot infer a format from '%s'; set ImportOptions::format",
                              utf8Path);
    }

    const Handlers* h = Lookup(ext);
    if (!h || !h->importer) {
        return core::SetError(CgResult::NotSupported,
                              "Import: no importer for '.%s'; the engine reads obj, gltf and glb, "
                              "and a host can register anything else through Register()",
                              ext.c_str());
    }

    return h->importer->Import(utf8Path, engine_.GetScene(), options, progress, userData);
}

CgResult IoRegistryImpl::Export(const char* utf8Path, const ExportOptions& options,
                                IoProgressCallback progress, void* userData) {
    if (!utf8Path || !*utf8Path) {
        return core::SetError(CgResult::InvalidArgument, "Export: empty path");
    }

    std::string ext = ExtensionOfFormat(options.format);
    if (ext.empty()) {
        ext = ExtensionOf(utf8Path);
    }
    if (ext.empty()) {
        return core::SetError(CgResult::InvalidArgument,
                              "Export: cannot infer a format from '%s'; set ExportOptions::format",
                              utf8Path);
    }

    const Handlers* h = Lookup(ext);
    if (!h || !h->exporter) {
        return core::SetError(CgResult::NotSupported,
                              "Export: no exporter for '.%s'; the engine writes obj, gltf and glb, "
                              "and a host can register anything else through Register()",
                              ext.c_str());
    }

    const IScene* scene = static_cast<const EngineImpl&>(engine_).GetScene();
    return h->exporter->Export(utf8Path, scene, options, progress, userData);
}

} // namespace cadgeom::api

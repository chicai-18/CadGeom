// CadGeom — file IO and its extension point (docs/architecture.md §7).
#ifndef CADGEOM_IIMPORTEXPORT_H
#define CADGEOM_IIMPORTEXPORT_H

#include <cadgeom/Export.h>
#include <cadgeom/Types.h>

namespace cadgeom {

class IScene;

/// Optional progress/diagnostic sink for a long import. Return false to abort.
typedef bool (*IoProgressCallback)(float fraction, const char* utf8Message, void* userData);

/// Implement to teach the engine a new input format. Registered handlers may be
/// host-allocated, which is why Release() exists.
class CADGEOM_API IImporter {
public:
    virtual void Release() = 0;

    /// UTF-8 display name, e.g. "Wavefront OBJ". Must outlive the importer.
    virtual const char* GetName() const = 0;

    /// Reads `utf8Path` into `scene`. Everything created is wrapped in one undo
    /// group, so a failed import leaves no half-loaded debris behind.
    virtual CgResult Import(const char* utf8Path, IScene* scene, const ImportOptions& options,
                            IoProgressCallback progress, void* userData) = 0;

protected:
    virtual ~IImporter() = default;
};

class CADGEOM_API IExporter {
public:
    virtual void Release() = 0;
    virtual const char* GetName() const = 0;

    virtual CgResult Export(const char* utf8Path, const IScene* scene,
                            const ExportOptions& options, IoProgressCallback progress,
                            void* userData) = 0;

protected:
    virtual ~IExporter() = default;
};

/// Extension registry mapping file extensions to handlers.
///
/// glTF is effectively CadGeom's native format: the parametric definitions ride
/// along in each node's `extras`, so our own round-trip keeps a circle editable
/// as a circle, while Blender and friends still see a plain mesh and ignore the
/// extra keys.
class CADGEOM_API IIoRegistry {
public:
    /// `extension` is written without a dot and matched case-insensitively
    /// ("obj", "gltf", "glb"). Passing null for either handler leaves that
    /// direction unregistered; registering over an existing entry replaces and
    /// releases the old one.
    virtual CgResult Register(const char* extension, IImporter* importer,
                              IExporter* exporter) = 0;
    virtual CgResult Unregister(const char* extension) = 0;

    virtual uint32_t GetFormatCount() const = 0;
    /// Extension of the registered format at `index`, or null when out of range.
    virtual const char* GetFormatExtension(uint32_t index) const = 0;
    virtual bool CanImport(const char* extension) const = 0;
    virtual bool CanExport(const char* extension) const = 0;

    /// Dispatches on ImportOptions::format, or on the path's extension when
    /// that is FileFormat::Auto.
    virtual CgResult Import(const char* utf8Path, const ImportOptions& options,
                            IoProgressCallback progress, void* userData) = 0;
    virtual CgResult Export(const char* utf8Path, const ExportOptions& options,
                            IoProgressCallback progress, void* userData) = 0;

protected:
    virtual ~IIoRegistry() = default;
};

} // namespace cadgeom

#endif // CADGEOM_IIMPORTEXPORT_H

#include "io/Registry.h"

#include "core/Log.h"

namespace cadgeom::io {

CgResult RegisterBuiltinFormats(IIoRegistry& registry, ISceneSource& source, ISceneSink& sink) {
    // 注册失败的那一对没有被接手，还是我们的，得自己放掉。
    const auto add = [&registry](const char* extension, IImporter* importer, IExporter* exporter) {
        const CgResult r = registry.Register(extension, importer, exporter);
        if (CgFailed(r)) {
            if (importer) {
                importer->Release();
            }
            if (exporter) {
                exporter->Release();
            }
        }
        return r;
    };

    CgResult r = add("obj", CreateObjImporter(sink), CreateObjExporter(source));
    if (CgFailed(r)) {
        return r;
    }
    // .gltf 和 .glb 是同一种格式的两个容器：读取器一份代码认两种（开头的魔数就能
    // 分出来），写出器却得知道自己在写哪一个，所以分别建。
    r = add("gltf", CreateGltfImporter(sink), CreateGltfExporter(source, /*binary=*/false));
    if (CgFailed(r)) {
        return r;
    }
    r = add("glb", CreateGltfImporter(sink), CreateGltfExporter(source, /*binary=*/true));
    if (CgFailed(r)) {
        return r;
    }

    CG_DEBUG("built-in IO formats registered: obj, gltf, glb");
    return CgResult::Ok;
}

} // namespace cadgeom::io

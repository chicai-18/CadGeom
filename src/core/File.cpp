#include "core/File.h"

#include "core/Error.h"

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cadgeom::core {
namespace {

#if defined(_WIN32)
/// UTF-8 → UTF-16。空串或转换失败时返回空 vector。
std::vector<wchar_t> Widen(const char* utf8) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::vector<wchar_t> wide(static_cast<size_t>(length));
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), length);
    return wide;
}
#endif

} // namespace

FILE* OpenFile(const char* utf8Path, const char* mode) {
    if (!utf8Path || !*utf8Path || !mode) {
        return nullptr;
    }
#if defined(_WIN32)
    const std::vector<wchar_t> widePath = Widen(utf8Path);
    const std::vector<wchar_t> wideMode = Widen(mode);
    if (widePath.empty() || wideMode.empty()) {
        return nullptr;
    }
    FILE* file = nullptr;
    if (_wfopen_s(&file, widePath.data(), wideMode.data()) != 0) {
        return nullptr;
    }
    return file;
#else
    return fopen(utf8Path, mode);
#endif
}

CgResult ReadFile(const char* utf8Path, std::vector<uint8_t>& out) {
    out.clear();
    FILE* file = OpenFile(utf8Path, "rb");
    if (!file) {
        return SetError(CgResult::IoError, "cannot open '%s' for reading",
                        utf8Path ? utf8Path : "");
    }

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return SetError(CgResult::IoError, "cannot measure '%s'", utf8Path);
    }

    out.resize(static_cast<size_t>(size));
    const size_t read = size > 0 ? fread(out.data(), 1, out.size(), file) : 0;
    fclose(file);
    if (read != out.size()) {
        out.clear();
        return SetError(CgResult::IoError, "short read from '%s'", utf8Path);
    }
    return CgResult::Ok;
}

CgResult ReadTextFile(const char* utf8Path, std::string& out) {
    std::vector<uint8_t> bytes;
    const CgResult r = ReadFile(utf8Path, bytes);
    if (CgFailed(r)) {
        out.clear();
        return r;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return CgResult::Ok;
}

CgResult WriteFile(const char* utf8Path, const void* data, size_t size) {
    FILE* file = OpenFile(utf8Path, "wb");
    if (!file) {
        return SetError(CgResult::IoError, "cannot open '%s' for writing",
                        utf8Path ? utf8Path : "");
    }
    const size_t written = size > 0 ? fwrite(data, 1, size, file) : 0;
    const bool flushed = fclose(file) == 0;
    if (written != size || !flushed) {
        return SetError(CgResult::IoError, "short write to '%s'", utf8Path);
    }
    return CgResult::Ok;
}

std::string DirectoryOf(const char* utf8Path) {
    if (!utf8Path) {
        return {};
    }
    const std::string path(utf8Path);
    const size_t sep = path.find_last_of("/\\");
    return sep == std::string::npos ? std::string{} : path.substr(0, sep);
}

std::string StemOf(const char* utf8Path) {
    if (!utf8Path) {
        return {};
    }
    std::string name(utf8Path);
    const size_t sep = name.find_last_of("/\\");
    if (sep != std::string::npos) {
        name.erase(0, sep + 1);
    }
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        name.erase(dot);
    }
    return name;
}

std::string ReplaceExtension(const char* utf8Path, const char* newExtension) {
    if (!utf8Path) {
        return {};
    }
    std::string path(utf8Path);
    const size_t sep = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    // 目录名里的点不是扩展名（"../model" 没有扩展名）。
    if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
        path.erase(dot);
    }
    path.push_back('.');
    path.append(newExtension ? newExtension : "");
    return path;
}

std::string JoinPath(const std::string& directory, const std::string& name) {
    if (directory.empty()) {
        return name;
    }
    std::string joined = directory;
    if (joined.back() != '/' && joined.back() != '\\') {
        joined.push_back('/');
    }
    joined.append(name);
    return joined;
}

} // namespace cadgeom::core

/**
 * @file Utf8.h
 * @brief UTF-8 与 UTF-16 之间的唯一一处换算。
 *
 * 引擎的字符串全是 UTF-8 `const char*`（ABI 规则第 1 条：接口里不出现 STL 类型，
 * 也就不出现 std::wstring）；这个宿主是 Unicode 构建的 MFC，`CString` 是
 * `CStringW`，控件收发的都是 UTF-16。两者之间必须换算，而换算只该有一处 —— 散落
 * 在各个调用点上迟早会漏掉一个，表现为菜单里的中文变成乱码，或者带中文路径的文件
 * 打不开。
 *
 * @note 反过来说：**不要**用 `CT2A`、`CW2A` 这类默认按 ANSI 代码页转换的宏。它们
 *       在简体中文机器上把 UTF-8 当 GBK 处理，看起来能用，换一台英文机器就全错。
 */
#ifndef CADGEOM_MFC_VIEWER_UTF8_H
#define CADGEOM_MFC_VIEWER_UTF8_H

#include <afxwin.h>

/// @brief UTF-8 → CString（UTF-16）。
/// @param utf8 可以为 null，返回空串。
inline CString FromUtf8(const char* utf8) {
    if (!utf8 || !*utf8) {
        return CString();
    }
    const int chars = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (chars <= 1) {
        return CString();
    }
    CString out;
    wchar_t* buffer = out.GetBuffer(chars);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buffer, chars);
    out.ReleaseBuffer(chars - 1);  // chars 含结尾的 '\0'
    return out;
}

/// @brief CString（UTF-16）→ UTF-8。
/// @return 一个 CStringA；传给引擎时用它的 `LPCSTR` 转换。**别把返回值的指针存起
///         来**：临时对象活到整条表达式结束为止，这在调用点上刚好够用。
inline CStringA ToUtf8(const CString& text) {
    if (text.IsEmpty()) {
        return CStringA();
    }
    const int bytes =
        ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return CStringA();
    }
    CStringA out;
    char* buffer = out.GetBuffer(bytes);
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, bytes, nullptr, nullptr);
    out.ReleaseBuffer(bytes - 1);
    return out;
}

#endif // CADGEOM_MFC_VIEWER_UTF8_H

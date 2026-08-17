/**
 * @file Dialogs.h
 * @brief 几个把引擎的 POD 结构摆成表单的小对话框。
 *
 * 它们存在的理由和 HUD 那段注释是同一件事：引擎自绘的 HUD 用的是笔画字体，只画
 * ASCII；真正的面板 —— 中文的、可输入的 —— 是宿主的活儿，引擎负责把数据交出来
 * （ICadEngine2::GetStatusText / FormatLength / GetMeasurement）。
 *
 * 和 qt_viewer 的同名文件比，最大的差别是**模板是死的**：Qt 那边按形状类型一行行
 * 拼出表单，MFC 的对话框模板在资源里写死，所以 CShapeParamsDialog 备齐了所有行，
 * 用不到的藏起来再把下面的往上收（见 .cpp 里的 LayoutRows）。
 *
 * 这里一律直接读写控件文本，没用 DDX/DDV：数值要过单位换算（模型单位 ↔ 显示
 * 单位），DDX 那套「成员变量 ↔ 控件」的直连中间插不进这一步。
 */
#ifndef CADGEOM_MFC_VIEWER_DIALOGS_H
#define CADGEOM_MFC_VIEWER_DIALOGS_H

#include <afxwin.h>

#include <cadgeom/CadGeom.h>

#include "resource.h"

/// @brief 长度单位的显示后缀（"mm"、"in" …），与 core/Units.h 的那张表一致。
CString LengthUnitName(cadgeom::LengthUnit unit);

// EntityStyle 和 ViewportDesc 里的 Color 是**线性光**里的值 —— 渲染器在半浮点目标
// 里合成，sRGB 编码发生在最后那次 blit（CLAUDE.md）。COLORREF 交换的是 sRGB，所以
// 每次进出都得换算一次；少了它，挑出来的灰会明显偏亮。

/// @brief sRGB 的 COLORREF → 线性的 Color。
cadgeom::Color ColorFromRgb(COLORREF rgb);
/// @brief 线性的 Color → sRGB 的 COLORREF。
COLORREF ColorToRgb(const cadgeom::Color& color);

/// @brief 一块显示当前颜色的自绘按钮；点开调色板的事归对话框做。
class CColorButton : public CButton {
public:
    void SetColor(COLORREF color);
    COLORREF Color() const { return color_; }

    void DrawItem(LPDRAWITEMSTRUCT item) override;

private:
    COLORREF color_{RGB(255, 255, 255)};
};

/// @brief 通用输入框 —— MFC 没有 QInputDialog，重命名 / 组名 / 捕捉半径 / 撤销栈
///        容量都用它。
class CInputDialog : public CDialog {
public:
    CInputDialog(const CString& title, const CString& prompt, const CString& initial,
                 CWnd* parent);

    const CString& Text() const { return text_; }

    /// @return 用户点了确定为 true。
    static bool AskText(CWnd* parent, const CString& title, const CString& prompt, CString& text);
    /// @brief 同上，但把输入当浮点数读，并夹到 [lo, hi]。
    static bool AskDouble(CWnd* parent, const CString& title, const CString& prompt, double& value,
                          double lo, double hi);
    /// @brief 同上，整数版。
    static bool AskInt(CWnd* parent, const CString& title, const CString& prompt, int& value,
                       int lo, int hi);

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;

private:
    CString title_;
    CString prompt_;
    CString text_;
};

/// @brief 导入 / 导出用的进度框，接 IoProgressCallback。
///
/// 模态框会自己开一圈消息循环，那样进度就永远刷不出来了 —— 所以它是**非模态**的：
/// 每次回调里自己抽干一次消息队列，取消按钮才点得动。
class CProgressDialog : public CDialog {
public:
    explicit CProgressDialog(CWnd* parent);
    ~CProgressDialog() override;

    /// @brief 建窗口并显示。
    BOOL Show();
    /// @brief 交给 IIoRegistry::Import/Export 的那个函数指针。
    static bool ProgressCallback(float fraction, const char* utf8Message, void* userData);

    bool Cancelled() const { return cancelled_; }

protected:
    BOOL OnInitDialog() override;
    void OnCancel() override;

private:
    /// @brief 刷新进度并让界面喘口气。
    void Update(float fraction, const CString& text);

    CWnd* parent_{nullptr};
    bool cancelled_{false};
};

/// @brief 显示单位设置。改它只影响读数，不动任何一个顶点。
class CUnitSettingsDialog : public CDialog {
public:
    CUnitSettingsDialog(const cadgeom::UnitSettings& settings, CWnd* parent);

    const cadgeom::UnitSettings& Settings() const { return settings_; }

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;

private:
    cadgeom::UnitSettings settings_{};
};

/// @brief 编辑一个形状的参数化定义 —— CAD 与「三角形编辑器」的分界线就在这里：
///        改的是半径，重新生成的是网格（docs/architecture.md §3.1）。
class CShapeParamsDialog : public CDialog {
public:
    /// @param params 初值，同时决定表单长什么样。
    /// @param units  用来把长度换算成显示单位；可以为 null（那就按模型单位显示）。
    CShapeParamsDialog(const cadgeom::ShapeParams& params, const cadgeom::ICadEngine2* units,
                       const CString& title, CWnd* parent);

    /// @return 这个类型能不能编辑。Polyline 不能 —— ShapeParams 是个冻结的 POD
    ///         union，装不下变长的点表（CLAUDE.md「两个后果」第一条）；Mesh 也不
    ///         能，它压根没有参数化定义。
    static bool Supports(cadgeom::ShapeType type);
    /// @brief 不能编辑时给用户看的那句话。
    static CString UnsupportedReason(cadgeom::ShapeType type);

    /// @return 按表单填好的参数；未在表单里出现的字段保持原值。
    const cadgeom::ShapeParams& Params() const { return params_; }

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;

private:
    void UseVec3(int index, const CString& label, const cadgeom::Vec3d& value, bool isLength);
    void UseScalar(int index, const CString& label, double value, bool isLength,
                   const CString& unitOverride);
    void UseCheck(int index, const CString& label, bool checked);
    cadgeom::Vec3d ReadVec3(int index, bool isLength) const;
    double ReadScalar(int index, bool isLength) const;
    bool ReadCheck(int index) const;

    double ToDisplay(double modelValue, bool isLength) const;
    double ToModel(double displayValue, bool isLength) const;
    /// @brief 藏掉没用到的行，把剩下的往上收，最后把对话框缩到刚好。
    void LayoutRows();

    cadgeom::ShapeParams params_{};
    const cadgeom::ICadEngine2* units_{nullptr};
    CString title_;
    CString note_;
    int vecCount_{0};
    int scalarCount_{0};
    int checkCount_{0};
};

/// @brief 整份 EntityStyle。颜色在对话框里是 sRGB，交给引擎前换成线性。
class CEntityStyleDialog : public CDialog {
public:
    CEntityStyleDialog(const cadgeom::EntityStyle& style, CWnd* parent);

    const cadgeom::EntityStyle& Style() const { return style_; }

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    afx_msg void OnPickColor();
    afx_msg void OnPickEdgeColor();
    DECLARE_MESSAGE_MAP()

private:
    cadgeom::EntityStyle style_{};
    CColorButton color_;
    CColorButton edgeColor_;
};

/// @brief 数值编辑一个实体的局部变换。旋转用欧拉角（ZYX）显示，四元数不适合手输。
class CTransformDialog : public CDialog {
public:
    CTransformDialog(const cadgeom::Transform& transform, const cadgeom::ICadEngine2* units,
                     CWnd* parent);

    const cadgeom::Transform& Transform() const { return transform_; }

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;

private:
    cadgeom::Transform transform_{};
    const cadgeom::ICadEngine2* units_{nullptr};
};

/// @brief 相机的那几个数：视场角、正交视高、近远裁剪面。
class CCameraDialog : public CDialog {
public:
    CCameraDialog(const cadgeom::ICamera& camera, const cadgeom::ICadEngine2* units, CWnd* parent);

    /// @brief 把表单里的值写回相机。
    void ApplyTo(cadgeom::ICamera& camera) const;

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;

private:
    const cadgeom::ICadEngine2* units_{nullptr};
    double fovDeg_{45.0};
    double orthoHeight_{100.0};
    double nearPlane_{0.1};
    double farPlane_{1000.0};
};

/// @brief 细分精度。改它会让所有网格缓存失效并重新生成。
class CTessParamsDialog : public CDialog {
public:
    CTessParamsDialog(const cadgeom::TessParams& params, const cadgeom::ICadEngine2* units,
                      CWnd* parent);

    const cadgeom::TessParams& Params() const { return params_; }

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;

private:
    cadgeom::TessParams params_{};
    const cadgeom::ICadEngine2* units_{nullptr};
};

/// @brief 一个只读文本框加一个按钮 —— 快捷键一览和「关于」都用它。
class CTextDialog : public CDialog {
public:
    CTextDialog(UINT templateId, UINT textControlId, const CString& title, const CString& text,
                CWnd* parent);

protected:
    BOOL OnInitDialog() override;

private:
    UINT textControlId_{0};
    CString title_;
    CString text_;
    CFont font_;
};

/// @brief 弹出快捷键一览。
void ShowShortcutsDialog(CWnd* parent);

/// @brief 弹出「关于」，把版本、设备、采样数和存活对象数摆出来。
void ShowAboutDialog(CWnd* parent, cadgeom::ICadEngine& engine, const cadgeom::ICadEngine2* ext,
                     const cadgeom::IViewport* viewport);

#endif // CADGEOM_MFC_VIEWER_DIALOGS_H

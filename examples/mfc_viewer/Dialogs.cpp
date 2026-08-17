/**
 * @file Dialogs.cpp
 * @brief Dialogs.h 的实现。
 */
#include "Dialogs.h"

#include "Utf8.h"

#include <afxcmn.h>   // CProgressCtrl
#include <afxdlgs.h>  // CColorDialog

#include <cadgeom/CadGeomRAII.h>

#include <cmath>

using namespace cadgeom;

namespace {

constexpr double kPi = 3.14159265358979323846;

double ToDegrees(double radians) { return radians * 180.0 / kPi; }
double ToRadians(double degrees) { return degrees * kPi / 180.0; }

double SrgbToLinear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double LinearToSrgb(double c) {
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

/// @brief 去掉小数点后多余的零：CAD 里的坐标要位数够，但 "45.0000" 读着累。
CString TrimZeros(const CString& text) {
    if (text.Find(_T('.')) < 0) {
        return text;
    }
    CString out = text;
    while (out.GetLength() > 1 && out[out.GetLength() - 1] == _T('0')) {
        out.Delete(out.GetLength() - 1);
    }
    if (out.GetLength() > 1 && out[out.GetLength() - 1] == _T('.')) {
        out.Delete(out.GetLength() - 1);
    }
    return out;
}

void SetDlgDouble(CWnd* dialog, int id, double value, int decimals = 4) {
    CString text;
    text.Format(_T("%.*f"), decimals, value);
    dialog->SetDlgItemText(id, TrimZeros(text));
}

double GetDlgDouble(const CWnd* dialog, int id, double fallback = 0.0) {
    CString text;
    dialog->GetDlgItemText(id, text);
    text.Trim();
    if (text.IsEmpty()) {
        return fallback;
    }
    return _tstof(text);
}

/// @brief 控件整体挪一段，只动 y。
void OffsetControl(CWnd* dialog, int id, int dy) {
    CWnd* control = dialog->GetDlgItem(id);
    if (!control || dy == 0) {
        return;
    }
    CRect rect;
    control->GetWindowRect(&rect);
    dialog->ScreenToClient(&rect);
    control->MoveWindow(rect.left, rect.top + dy, rect.Width(), rect.Height());
}

void HideControl(CWnd* dialog, int id) {
    if (CWnd* control = dialog->GetDlgItem(id)) {
        control->ShowWindow(SW_HIDE);
        control->EnableWindow(FALSE);
    }
}

/// @brief 控件在对话框客户区里的位置。
CRect ControlRect(CWnd* dialog, int id) {
    CRect rect(0, 0, 0, 0);
    if (CWnd* control = dialog->GetDlgItem(id)) {
        control->GetWindowRect(&rect);
        dialog->ScreenToClient(&rect);
    }
    return rect;
}

const int kVecLabel[3] = {IDC_SP_VLABEL0, IDC_SP_VLABEL1, IDC_SP_VLABEL2};
const int kVecEdit[3][3] = {{IDC_SP_VX0, IDC_SP_VY0, IDC_SP_VZ0},
                            {IDC_SP_VX1, IDC_SP_VY1, IDC_SP_VZ1},
                            {IDC_SP_VX2, IDC_SP_VY2, IDC_SP_VZ2}};
const int kScalarLabel[4] = {IDC_SP_SLABEL0, IDC_SP_SLABEL1, IDC_SP_SLABEL2, IDC_SP_SLABEL3};
const int kScalarEdit[4] = {IDC_SP_SEDIT0, IDC_SP_SEDIT1, IDC_SP_SEDIT2, IDC_SP_SEDIT3};
const int kCheckBox[2] = {IDC_SP_CHK0, IDC_SP_CHK1};

/// @brief 当前显示单位的后缀，拼进标签里（"半径 (mm)"）。
CString UnitSuffix(const ICadEngine2* units) {
    if (!units) {
        return CString();
    }
    UnitSettings settings{};
    units->GetUnitSettings(settings);
    CString out;
    out.Format(_T(" (%s)"), static_cast<LPCTSTR>(LengthUnitName(settings.displayUnit)));
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// 换算
// ---------------------------------------------------------------------------

Color ColorFromRgb(COLORREF rgb) {
    return Color{static_cast<float>(SrgbToLinear(GetRValue(rgb) / 255.0)),
                 static_cast<float>(SrgbToLinear(GetGValue(rgb) / 255.0)),
                 static_cast<float>(SrgbToLinear(GetBValue(rgb) / 255.0)), 1.0f};
}

COLORREF ColorToRgb(const Color& color) {
    const int r = static_cast<int>(Clamp01(LinearToSrgb(color.r)) * 255.0 + 0.5);
    const int g = static_cast<int>(Clamp01(LinearToSrgb(color.g)) * 255.0 + 0.5);
    const int b = static_cast<int>(Clamp01(LinearToSrgb(color.b)) * 255.0 + 0.5);
    return RGB(r, g, b);
}

CString LengthUnitName(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimetre: return _T("mm");
        case LengthUnit::Centimetre: return _T("cm");
        case LengthUnit::Metre: return _T("m");
        case LengthUnit::Kilometre: return _T("km");
        case LengthUnit::Inch: return _T("in");
        case LengthUnit::Foot: return _T("ft");
        case LengthUnit::Yard: return _T("yd");
        case LengthUnit::Mile: return _T("mi");
    }
    return CString();
}

// ---------------------------------------------------------------------------
// CColorButton
// ---------------------------------------------------------------------------

void CColorButton::SetColor(COLORREF color) {
    color_ = color;
    if (GetSafeHwnd()) {
        Invalidate();
    }
}

void CColorButton::DrawItem(LPDRAWITEMSTRUCT item) {
    CDC* dc = CDC::FromHandle(item->hDC);
    CRect rect(item->rcItem);

    dc->FillSolidRect(rect, color_);
    dc->Draw3dRect(rect, ::GetSysColor(COLOR_3DSHADOW), ::GetSysColor(COLOR_3DHILIGHT));
    if ((item->itemState & ODS_FOCUS) != 0) {
        CRect focus = rect;
        focus.DeflateRect(2, 2);
        dc->DrawFocusRect(focus);
    }

    // 深色底上写白字，浅色底上写黑字 —— 亮度用的是 sRGB 空间里的粗略权重，这里
    // 只是为了让十六进制值看得清，不必较真。
    const int luma = (GetRValue(color_) * 30 + GetGValue(color_) * 59 + GetBValue(color_) * 11) / 100;
    CString text;
    text.Format(_T("#%02X%02X%02X"), GetRValue(color_), GetGValue(color_), GetBValue(color_));
    dc->SetBkMode(TRANSPARENT);
    dc->SetTextColor(luma > 128 ? RGB(0, 0, 0) : RGB(255, 255, 255));
    dc->DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// CInputDialog
// ---------------------------------------------------------------------------

CInputDialog::CInputDialog(const CString& title, const CString& prompt, const CString& initial,
                           CWnd* parent)
    : CDialog(IDD_INPUT, parent), title_(title), prompt_(prompt), text_(initial) {}

BOOL CInputDialog::OnInitDialog() {
    CDialog::OnInitDialog();
    SetWindowText(title_);
    SetDlgItemText(IDC_INPUT_PROMPT, prompt_);
    SetDlgItemText(IDC_INPUT_EDIT, text_);
    if (CWnd* edit = GetDlgItem(IDC_INPUT_EDIT)) {
        edit->SetFocus();
        static_cast<CEdit*>(edit)->SetSel(0, -1);
    }
    return FALSE;  // 焦点已经自己安排好了
}

void CInputDialog::OnOK() {
    GetDlgItemText(IDC_INPUT_EDIT, text_);
    CDialog::OnOK();
}

bool CInputDialog::AskText(CWnd* parent, const CString& title, const CString& prompt,
                           CString& text) {
    CInputDialog dialog(title, prompt, text, parent);
    if (dialog.DoModal() != IDOK) {
        return false;
    }
    text = dialog.Text();
    return true;
}

bool CInputDialog::AskDouble(CWnd* parent, const CString& title, const CString& prompt,
                             double& value, double lo, double hi) {
    CString text;
    text.Format(_T("%g"), value);
    if (!AskText(parent, title, prompt, text)) {
        return false;
    }
    double parsed = _tstof(text);
    parsed = parsed < lo ? lo : (parsed > hi ? hi : parsed);
    value = parsed;
    return true;
}

bool CInputDialog::AskInt(CWnd* parent, const CString& title, const CString& prompt, int& value,
                          int lo, int hi) {
    double asDouble = value;
    if (!AskDouble(parent, title, prompt, asDouble, lo, hi)) {
        return false;
    }
    value = static_cast<int>(asDouble);
    return true;
}

// ---------------------------------------------------------------------------
// CProgressDialog
// ---------------------------------------------------------------------------

CProgressDialog::CProgressDialog(CWnd* parent) : CDialog(IDD_PROGRESS, parent), parent_(parent) {}

CProgressDialog::~CProgressDialog() {
    // 先把主窗口放开再销毁自己：反过来的话，主窗口那一瞬间是禁用的，Windows 会把
    // 激活焦点扔给别的进程的窗口。
    if (parent_) {
        parent_->EnableWindow(TRUE);
        parent_->SetActiveWindow();
    }
    if (GetSafeHwnd()) {
        DestroyWindow();
    }
}

BOOL CProgressDialog::Show() {
    if (!Create(IDD_PROGRESS, parent_)) {
        return FALSE;
    }
    // 非模态窗口 + 禁用主窗口 = 自己做的模态。真做成模态的话，DoModal 会开一圈
    // 自己的消息循环，而导入是在**调用方**的栈上跑的，进度根本刷不出来。
    if (parent_) {
        parent_->EnableWindow(FALSE);
    }
    CenterWindow(parent_);
    ShowWindow(SW_SHOW);
    UpdateWindow();
    return TRUE;
}

BOOL CProgressDialog::OnInitDialog() {
    CDialog::OnInitDialog();
    if (auto* bar = static_cast<CProgressCtrl*>(GetDlgItem(IDC_PROGRESS_BAR))) {
        bar->SetRange(0, 100);
        bar->SetPos(0);
    }
    return TRUE;
}

void CProgressDialog::OnCancel() {
    // 不能真的关掉：IO 还在跑，回调随时会回来。立个旗子，让下一次回调返回 false。
    cancelled_ = true;
    SetDlgItemText(IDC_PROGRESS_TEXT, _T("正在取消…"));
    if (CWnd* button = GetDlgItem(IDCANCEL)) {
        button->EnableWindow(FALSE);
    }
}

void CProgressDialog::Update(float fraction, const CString& text) {
    if (!text.IsEmpty()) {
        SetDlgItemText(IDC_PROGRESS_TEXT, text);
    }
    if (auto* bar = static_cast<CProgressCtrl*>(GetDlgItem(IDC_PROGRESS_BAR))) {
        int pos = static_cast<int>(fraction * 100.0f);
        pos = pos < 0 ? 0 : (pos > 100 ? 100 : pos);
        bar->SetPos(pos);
    }

    // 抽干一次消息队列，界面才刷得出来、取消按钮才点得动 —— MFC 版的 DoEvents。
    // 主窗口的帧定时器在导入期间是停掉的（CMainFrame::CFrameLoopPause），所以这
    // 里不会在读一半文件的时候插进去画一帧。
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE)) {
        if (!AfxGetApp()->PumpMessage()) {
            break;
        }
    }
}

bool CProgressDialog::ProgressCallback(float fraction, const char* utf8Message, void* userData) {
    auto* dialog = static_cast<CProgressDialog*>(userData);
    if (!dialog || !dialog->GetSafeHwnd()) {
        return true;
    }
    dialog->Update(fraction, FromUtf8(utf8Message));
    return !dialog->Cancelled();
}

// ---------------------------------------------------------------------------
// CUnitSettingsDialog
// ---------------------------------------------------------------------------

namespace {

void FillLengthUnits(CComboBox* box) {
    // 顺序即 LengthUnit 的枚举值，所以选中项的下标就是枚举值。
    static const TCHAR* const kNames[] = {_T("毫米 (mm)"), _T("厘米 (cm)"), _T("米 (m)"),
                                          _T("千米 (km)"), _T("英寸 (in)"), _T("英尺 (ft)"),
                                          _T("码 (yd)"),   _T("英里 (mi)")};
    for (const TCHAR* name : kNames) {
        box->AddString(name);
    }
}

} // namespace

CUnitSettingsDialog::CUnitSettingsDialog(const UnitSettings& settings, CWnd* parent)
    : CDialog(IDD_UNITS, parent), settings_(settings) {}

BOOL CUnitSettingsDialog::OnInitDialog() {
    CDialog::OnInitDialog();

    auto* model = static_cast<CComboBox*>(GetDlgItem(IDC_UNIT_MODEL));
    auto* display = static_cast<CComboBox*>(GetDlgItem(IDC_UNIT_DISPLAY));
    auto* angle = static_cast<CComboBox*>(GetDlgItem(IDC_UNIT_ANGLE));
    FillLengthUnits(model);
    FillLengthUnits(display);
    angle->AddString(_T("度 (°)"));
    angle->AddString(_T("弧度 (rad)"));

    model->SetCurSel(static_cast<int>(settings_.modelUnit));
    display->SetCurSel(static_cast<int>(settings_.displayUnit));
    angle->SetCurSel(static_cast<int>(settings_.angleUnit));

    SetDlgItemInt(IDC_UNIT_LINPREC, settings_.linearPrecision < 0 ? 0 : settings_.linearPrecision);
    SetDlgItemInt(IDC_UNIT_ANGPREC,
                  settings_.angularPrecision < 0 ? 0 : settings_.angularPrecision);
    CheckDlgButton(IDC_UNIT_SUFFIX, settings_.showUnitSuffix ? BST_CHECKED : BST_UNCHECKED);
    return TRUE;
}

void CUnitSettingsDialog::OnOK() {
    const int model = static_cast<CComboBox*>(GetDlgItem(IDC_UNIT_MODEL))->GetCurSel();
    const int display = static_cast<CComboBox*>(GetDlgItem(IDC_UNIT_DISPLAY))->GetCurSel();
    const int angle = static_cast<CComboBox*>(GetDlgItem(IDC_UNIT_ANGLE))->GetCurSel();

    settings_.modelUnit = static_cast<LengthUnit>(model < 0 ? 0 : model);
    settings_.displayUnit = static_cast<LengthUnit>(display < 0 ? 0 : display);
    settings_.angleUnit = static_cast<AngleUnit>(angle < 0 ? 0 : angle);

    int precision = static_cast<int>(GetDlgItemInt(IDC_UNIT_LINPREC));
    settings_.linearPrecision = precision < 0 ? 0 : (precision > 9 ? 9 : precision);
    precision = static_cast<int>(GetDlgItemInt(IDC_UNIT_ANGPREC));
    settings_.angularPrecision = precision < 0 ? 0 : (precision > 9 ? 9 : precision);

    settings_.showUnitSuffix = IsDlgButtonChecked(IDC_UNIT_SUFFIX) == BST_CHECKED;
    CDialog::OnOK();
}

// ---------------------------------------------------------------------------
// CShapeParamsDialog
// ---------------------------------------------------------------------------

CShapeParamsDialog::CShapeParamsDialog(const ShapeParams& params, const ICadEngine2* units,
                                       const CString& title, CWnd* parent)
    : CDialog(IDD_SHAPE_PARAMS, parent), params_(params), units_(units), title_(title) {}

bool CShapeParamsDialog::Supports(ShapeType type) {
    switch (type) {
        case ShapeType::Point:
        case ShapeType::Line:
        case ShapeType::Circle:
        case ShapeType::Arc:
        case ShapeType::Rectangle:
        case ShapeType::Solid: return true;
        default: return false;
    }
}

CString CShapeParamsDialog::UnsupportedReason(ShapeType type) {
    switch (type) {
        case ShapeType::Polyline:
            return _T("多段线的点表带不过 ABI 边界：ShapeParams 是一个冻结的 POD union，")
                   _T("没有地方放变长数组，所以 GetParams 读不回点、SetParams 也拒绝写。\n\n")
                   _T("文件没有这个限制 —— glTF 的 extras 里带着点表，多段线能原样往返。");
        case ShapeType::Mesh:
            return _T("导入的网格没有参数化定义可谈：它的三角形就是定义本身，改不出一个")
                   _T("「半径」来。");
        default: return _T("这个对象没有可编辑的参数。");
    }
}

double CShapeParamsDialog::ToDisplay(double modelValue, bool isLength) const {
    return (isLength && units_) ? units_->ToDisplayLength(modelValue) : modelValue;
}

double CShapeParamsDialog::ToModel(double displayValue, bool isLength) const {
    return (isLength && units_) ? units_->ToModelLength(displayValue) : displayValue;
}

void CShapeParamsDialog::UseVec3(int index, const CString& label, const Vec3d& value,
                                 bool isLength) {
    CString text = label;
    if (isLength) {
        text += UnitSuffix(units_);
    }
    SetDlgItemText(kVecLabel[index], text);
    SetDlgDouble(this, kVecEdit[index][0], ToDisplay(value.x, isLength));
    SetDlgDouble(this, kVecEdit[index][1], ToDisplay(value.y, isLength));
    SetDlgDouble(this, kVecEdit[index][2], ToDisplay(value.z, isLength));
    vecCount_ = index + 1;
}

void CShapeParamsDialog::UseScalar(int index, const CString& label, double value, bool isLength,
                                   const CString& unitOverride) {
    CString text = label;
    text += unitOverride.IsEmpty() ? (isLength ? UnitSuffix(units_) : CString()) : unitOverride;
    SetDlgItemText(kScalarLabel[index], text);
    SetDlgDouble(this, kScalarEdit[index], ToDisplay(value, isLength));
    scalarCount_ = index + 1;
}

void CShapeParamsDialog::UseCheck(int index, const CString& label, bool checked) {
    SetDlgItemText(kCheckBox[index], label);
    CheckDlgButton(kCheckBox[index], checked ? BST_CHECKED : BST_UNCHECKED);
    checkCount_ = index + 1;
}

Vec3d CShapeParamsDialog::ReadVec3(int index, bool isLength) const {
    return Vec3d{ToModel(GetDlgDouble(this, kVecEdit[index][0]), isLength),
                 ToModel(GetDlgDouble(this, kVecEdit[index][1]), isLength),
                 ToModel(GetDlgDouble(this, kVecEdit[index][2]), isLength)};
}

double CShapeParamsDialog::ReadScalar(int index, bool isLength) const {
    return ToModel(GetDlgDouble(this, kScalarEdit[index]), isLength);
}

bool CShapeParamsDialog::ReadCheck(int index) const {
    return IsDlgButtonChecked(kCheckBox[index]) == BST_CHECKED;
}

BOOL CShapeParamsDialog::OnInitDialog() {
    CDialog::OnInitDialog();
    SetWindowText(title_);

    const CString kDeg = _T(" (°)");

    switch (params_.type) {
        case ShapeType::Point:
            UseVec3(0, _T("位置 X / Y / Z"), params_.point.position, true);
            break;

        case ShapeType::Line:
            UseVec3(0, _T("起点 X / Y / Z"), params_.line.start, true);
            UseVec3(1, _T("终点 X / Y / Z"), params_.line.end, true);
            break;

        case ShapeType::Circle:
            UseVec3(0, _T("圆心 X / Y / Z"), params_.circle.plane.origin, true);
            UseVec3(1, _T("法线 X / Y / Z"), params_.circle.plane.normal, false);
            UseScalar(0, _T("半径"), params_.circle.radius, true, CString());
            break;

        case ShapeType::Arc:
            UseVec3(0, _T("圆心 X / Y / Z"), params_.arc.plane.origin, true);
            UseVec3(1, _T("法线 X / Y / Z"), params_.arc.plane.normal, false);
            UseScalar(0, _T("半径"), params_.arc.radius, true, CString());
            UseScalar(1, _T("起始角"), ToDegrees(params_.arc.startAngle), false, kDeg);
            UseScalar(2, _T("扫掠角"), ToDegrees(params_.arc.sweepAngle), false, kDeg);
            break;

        case ShapeType::Rectangle:
            UseVec3(0, _T("中心 X / Y / Z"), params_.rectangle.plane.origin, true);
            UseVec3(1, _T("法线 X / Y / Z"), params_.rectangle.plane.normal, false);
            UseVec3(2, _T("宽度方向 X / Y / Z"), params_.rectangle.uAxis, false);
            UseScalar(0, _T("宽"), params_.rectangle.width, true, CString());
            UseScalar(1, _T("高"), params_.rectangle.height, true, CString());
            break;

        case ShapeType::Solid:
            UseVec3(0, _T("扫掠方向 X / Y / Z"), params_.extrude.direction, false);
            UseScalar(0, _T("距离"), params_.extrude.distance, true, CString());
            UseScalar(1, _T("拔模角"), ToDegrees(params_.extrude.options.draftAngle), false, kDeg);
            UseCheck(0, _T("双向扫掠"), params_.extrude.options.bothDirections);
            UseCheck(1, _T("封端（否则得到一张壳）"), params_.extrude.options.capEnds);
            note_ = _T("轮廓不能换：实体带的是轮廓的一份拷贝，换掉那个 id 只会让它和自己的")
                    _T("几何对不上号。想换轮廓，就重新拉伸一次。");
            break;

        default:
            break;
    }

    SetDlgItemText(IDC_SP_NOTE, note_);
    LayoutRows();
    return TRUE;
}

void CShapeParamsDialog::LayoutRows() {
    // 行距从模板自己量：DLU 换成像素跟字体走，写死一个数在高 DPI 下会散架。
    const CRect vec0 = ControlRect(this, kVecLabel[0]);
    const CRect vec1 = ControlRect(this, kVecLabel[1]);
    const CRect chk0 = ControlRect(this, kCheckBox[0]);
    const CRect chk1 = ControlRect(this, kCheckBox[1]);
    const CRect note = ControlRect(this, IDC_SP_NOTE);
    const int rowStep = vec1.top - vec0.top;
    const int checkStep = chk1.top - chk0.top;

    for (int i = vecCount_; i < 3; ++i) {
        HideControl(this, kVecLabel[i]);
        for (int axis = 0; axis < 3; ++axis) {
            HideControl(this, kVecEdit[i][axis]);
        }
    }
    for (int i = scalarCount_; i < 4; ++i) {
        HideControl(this, kScalarLabel[i]);
        HideControl(this, kScalarEdit[i]);
    }
    for (int i = checkCount_; i < 2; ++i) {
        HideControl(this, kCheckBox[i]);
    }

    // 用到的行依次往上收：标量行让开没用到的向量行，复选框再让开没用到的标量行。
    const int scalarShift = (vecCount_ - 3) * rowStep;
    for (int i = 0; i < scalarCount_; ++i) {
        OffsetControl(this, kScalarLabel[i], scalarShift);
        OffsetControl(this, kScalarEdit[i], scalarShift);
    }
    const int checkShift = scalarShift + (scalarCount_ - 4) * rowStep;
    for (int i = 0; i < checkCount_; ++i) {
        OffsetControl(this, kCheckBox[i], checkShift);
    }

    int tailShift = checkShift + (checkCount_ - 2) * checkStep;
    if (note_.IsEmpty()) {
        HideControl(this, IDC_SP_NOTE);
        tailShift -= note.Height() + (note.top - chk1.bottom);
    } else {
        OffsetControl(this, IDC_SP_NOTE, tailShift);
    }
    OffsetControl(this, IDOK, tailShift);
    OffsetControl(this, IDCANCEL, tailShift);

    CRect window;
    GetWindowRect(&window);
    SetWindowPos(nullptr, 0, 0, window.Width(), window.Height() + tailShift,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    CenterWindow();
}

void CShapeParamsDialog::OnOK() {
    switch (params_.type) {
        case ShapeType::Point:
            params_.point.position = ReadVec3(0, true);
            break;

        case ShapeType::Line:
            params_.line.start = ReadVec3(0, true);
            params_.line.end = ReadVec3(1, true);
            break;

        case ShapeType::Circle:
            params_.circle.plane.origin = ReadVec3(0, true);
            params_.circle.plane.normal = ReadVec3(1, false);
            params_.circle.radius = ReadScalar(0, true);
            break;

        case ShapeType::Arc:
            params_.arc.plane.origin = ReadVec3(0, true);
            params_.arc.plane.normal = ReadVec3(1, false);
            params_.arc.radius = ReadScalar(0, true);
            params_.arc.startAngle = ToRadians(ReadScalar(1, false));
            params_.arc.sweepAngle = ToRadians(ReadScalar(2, false));
            break;

        case ShapeType::Rectangle:
            params_.rectangle.plane.origin = ReadVec3(0, true);
            params_.rectangle.plane.normal = ReadVec3(1, false);
            params_.rectangle.uAxis = ReadVec3(2, false);
            params_.rectangle.width = ReadScalar(0, true);
            params_.rectangle.height = ReadScalar(1, true);
            break;

        case ShapeType::Solid:
            params_.extrude.direction = ReadVec3(0, false);
            params_.extrude.distance = ReadScalar(0, true);
            params_.extrude.options.draftAngle = ToRadians(ReadScalar(1, false));
            params_.extrude.options.bothDirections = ReadCheck(0);
            params_.extrude.options.capEnds = ReadCheck(1);
            break;

        default:
            break;
    }
    CDialog::OnOK();
}

// ---------------------------------------------------------------------------
// CEntityStyleDialog
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CEntityStyleDialog, CDialog)
    ON_BN_CLICKED(IDC_STYLE_COLOR, &CEntityStyleDialog::OnPickColor)
    ON_BN_CLICKED(IDC_STYLE_EDGECOLOR, &CEntityStyleDialog::OnPickEdgeColor)
END_MESSAGE_MAP()

CEntityStyleDialog::CEntityStyleDialog(const EntityStyle& style, CWnd* parent)
    : CDialog(IDD_STYLE, parent), style_(style) {}

BOOL CEntityStyleDialog::OnInitDialog() {
    CDialog::OnInitDialog();

    color_.SubclassDlgItem(IDC_STYLE_COLOR, this);
    color_.SetColor(ColorToRgb(style_.color));
    edgeColor_.SubclassDlgItem(IDC_STYLE_EDGECOLOR, this);
    edgeColor_.SetColor(ColorToRgb(style_.edgeColor));

    SetDlgDouble(this, IDC_STYLE_LINEWIDTH, style_.lineWidth, 2);
    SetDlgDouble(this, IDC_STYLE_POINTSIZE, style_.pointSize, 2);

    auto* lineStyle = static_cast<CComboBox*>(GetDlgItem(IDC_STYLE_LINESTYLE));
    static const TCHAR* const kNames[] = {_T("实线"),   _T("虚线"),   _T("点线"),
                                          _T("点画线"), _T("中心线"), _T("隐藏线")};
    for (const TCHAR* name : kNames) {
        lineStyle->AddString(name);
    }
    lineStyle->SetCurSel(static_cast<int>(style_.lineStyle));

    CheckDlgButton(IDC_STYLE_VISIBLE, style_.visible ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_STYLE_SELECTABLE, style_.selectable ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_STYLE_SHADOW, style_.castsShadow ? BST_CHECKED : BST_UNCHECKED);
    return TRUE;
}

void CEntityStyleDialog::OnPickColor() {
    CColorDialog dialog(color_.Color(), CC_FULLOPEN | CC_ANYCOLOR, this);
    if (dialog.DoModal() == IDOK) {
        color_.SetColor(dialog.GetColor());
    }
}

void CEntityStyleDialog::OnPickEdgeColor() {
    CColorDialog dialog(edgeColor_.Color(), CC_FULLOPEN | CC_ANYCOLOR, this);
    if (dialog.DoModal() == IDOK) {
        edgeColor_.SetColor(dialog.GetColor());
    }
}

void CEntityStyleDialog::OnOK() {
    style_.color = ColorFromRgb(color_.Color());
    style_.edgeColor = ColorFromRgb(edgeColor_.Color());
    style_.lineWidth = static_cast<float>(GetDlgDouble(this, IDC_STYLE_LINEWIDTH, 1.5));
    style_.pointSize = static_cast<float>(GetDlgDouble(this, IDC_STYLE_POINTSIZE, 6.0));

    const int lineStyle = static_cast<CComboBox*>(GetDlgItem(IDC_STYLE_LINESTYLE))->GetCurSel();
    style_.lineStyle = static_cast<LineStyle>(lineStyle < 0 ? 0 : lineStyle);

    style_.visible = IsDlgButtonChecked(IDC_STYLE_VISIBLE) == BST_CHECKED;
    style_.selectable = IsDlgButtonChecked(IDC_STYLE_SELECTABLE) == BST_CHECKED;
    style_.castsShadow = IsDlgButtonChecked(IDC_STYLE_SHADOW) == BST_CHECKED;
    CDialog::OnOK();
}

// ---------------------------------------------------------------------------
// CTransformDialog
// ---------------------------------------------------------------------------

namespace {

/// @brief 四元数 → 欧拉角（ZYX：先绕 Z 偏航，再绕 Y 俯仰，最后绕 X 滚转），弧度。
Vec3d QuatToEuler(const Quatd& q) {
    const double sinRoll = 2.0 * (q.w * q.x + q.y * q.z);
    const double cosRoll = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);
    const double roll = std::atan2(sinRoll, cosRoll);

    double sinPitch = 2.0 * (q.w * q.y - q.z * q.x);
    sinPitch = sinPitch > 1.0 ? 1.0 : (sinPitch < -1.0 ? -1.0 : sinPitch);
    const double pitch = std::asin(sinPitch);

    const double sinYaw = 2.0 * (q.w * q.z + q.x * q.y);
    const double cosYaw = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    const double yaw = std::atan2(sinYaw, cosYaw);

    return Vec3d{roll, pitch, yaw};
}

/// @brief 欧拉角（弧度，ZYX）→ 单位四元数。
Quatd EulerToQuat(const Vec3d& euler) {
    const double cr = std::cos(euler.x * 0.5);
    const double sr = std::sin(euler.x * 0.5);
    const double cp = std::cos(euler.y * 0.5);
    const double sp = std::sin(euler.y * 0.5);
    const double cy = std::cos(euler.z * 0.5);
    const double sy = std::sin(euler.z * 0.5);

    Quatd q{};
    q.w = cr * cp * cy + sr * sp * sy;
    q.x = sr * cp * cy - cr * sp * sy;
    q.y = cr * sp * cy + sr * cp * sy;
    q.z = cr * cp * sy - sr * sp * cy;
    return q;
}

} // namespace

CTransformDialog::CTransformDialog(const cadgeom::Transform& transform, const ICadEngine2* units,
                                   CWnd* parent)
    : CDialog(IDD_TRANSFORM, parent), transform_(transform), units_(units) {}

BOOL CTransformDialog::OnInitDialog() {
    CDialog::OnInitDialog();

    const auto toDisplay = [this](double value) {
        return units_ ? units_->ToDisplayLength(value) : value;
    };
    SetDlgDouble(this, IDC_TR_TX, toDisplay(transform_.translation.x));
    SetDlgDouble(this, IDC_TR_TY, toDisplay(transform_.translation.y));
    SetDlgDouble(this, IDC_TR_TZ, toDisplay(transform_.translation.z));

    const Vec3d euler = QuatToEuler(transform_.rotation);
    SetDlgDouble(this, IDC_TR_RX, ToDegrees(euler.x));
    SetDlgDouble(this, IDC_TR_RY, ToDegrees(euler.y));
    SetDlgDouble(this, IDC_TR_RZ, ToDegrees(euler.z));

    SetDlgDouble(this, IDC_TR_SX, transform_.scale.x);
    SetDlgDouble(this, IDC_TR_SY, transform_.scale.y);
    SetDlgDouble(this, IDC_TR_SZ, transform_.scale.z);

    // 平移那一行的数值是过了单位换算的，标签上就得写清楚是什么单位 —— 这也是模板
    // 里唯一一个需要在运行时改文字的静态文本，所以只有它有自己的 id。
    if (units_) {
        UnitSettings settings{};
        units_->GetUnitSettings(settings);
        CString label;
        label.Format(_T("平移 X / Y / Z (%s)"),
                     static_cast<LPCTSTR>(LengthUnitName(settings.displayUnit)));
        SetDlgItemText(IDC_TR_TLABEL, label);
    }
    return TRUE;
}

void CTransformDialog::OnOK() {
    const auto toModel = [this](double value) {
        return units_ ? units_->ToModelLength(value) : value;
    };
    transform_.translation = Vec3d{toModel(GetDlgDouble(this, IDC_TR_TX)),
                                   toModel(GetDlgDouble(this, IDC_TR_TY)),
                                   toModel(GetDlgDouble(this, IDC_TR_TZ))};
    transform_.rotation = EulerToQuat(Vec3d{ToRadians(GetDlgDouble(this, IDC_TR_RX)),
                                            ToRadians(GetDlgDouble(this, IDC_TR_RY)),
                                            ToRadians(GetDlgDouble(this, IDC_TR_RZ))});
    transform_.scale = Vec3d{GetDlgDouble(this, IDC_TR_SX, 1.0), GetDlgDouble(this, IDC_TR_SY, 1.0),
                             GetDlgDouble(this, IDC_TR_SZ, 1.0)};
    CDialog::OnOK();
}

// ---------------------------------------------------------------------------
// CCameraDialog
// ---------------------------------------------------------------------------

CCameraDialog::CCameraDialog(const ICamera& camera, const ICadEngine2* units, CWnd* parent)
    : CDialog(IDD_CAMERA, parent), units_(units) {
    camera.GetNearFar(nearPlane_, farPlane_);
    fovDeg_ = ToDegrees(camera.GetFieldOfView());
    orthoHeight_ = camera.GetOrthoHeight();
}

BOOL CCameraDialog::OnInitDialog() {
    CDialog::OnInitDialog();
    const auto toDisplay = [this](double value) {
        return units_ ? units_->ToDisplayLength(value) : value;
    };
    SetDlgDouble(this, IDC_CAM_FOV, fovDeg_, 2);
    SetDlgDouble(this, IDC_CAM_ORTHO, toDisplay(orthoHeight_));
    SetDlgDouble(this, IDC_CAM_NEAR, toDisplay(nearPlane_), 6);
    SetDlgDouble(this, IDC_CAM_FAR, toDisplay(farPlane_), 6);
    return TRUE;
}

void CCameraDialog::OnOK() {
    const auto toModel = [this](double value) {
        return units_ ? units_->ToModelLength(value) : value;
    };
    fovDeg_ = GetDlgDouble(this, IDC_CAM_FOV, fovDeg_);
    fovDeg_ = fovDeg_ < 1.0 ? 1.0 : (fovDeg_ > 179.0 ? 179.0 : fovDeg_);
    orthoHeight_ = toModel(GetDlgDouble(this, IDC_CAM_ORTHO, orthoHeight_));
    nearPlane_ = toModel(GetDlgDouble(this, IDC_CAM_NEAR, nearPlane_));
    farPlane_ = toModel(GetDlgDouble(this, IDC_CAM_FAR, farPlane_));
    CDialog::OnOK();
}

void CCameraDialog::ApplyTo(ICamera& camera) const {
    camera.SetFieldOfView(ToRadians(fovDeg_));
    camera.SetOrthoHeight(orthoHeight_);
    camera.SetNearFar(nearPlane_, farPlane_);
}

// ---------------------------------------------------------------------------
// CTessParamsDialog
// ---------------------------------------------------------------------------

CTessParamsDialog::CTessParamsDialog(const TessParams& params, const ICadEngine2* units,
                                     CWnd* parent)
    : CDialog(IDD_TESS, parent), params_(params), units_(units) {}

BOOL CTessParamsDialog::OnInitDialog() {
    CDialog::OnInitDialog();
    const double chord =
        units_ ? units_->ToDisplayLength(params_.chordTolerance) : params_.chordTolerance;
    SetDlgDouble(this, IDC_TESS_CHORD, chord, 5);
    SetDlgDouble(this, IDC_TESS_ANGULAR, ToDegrees(params_.angularTolerance), 3);
    return TRUE;
}

void CTessParamsDialog::OnOK() {
    const double chord = GetDlgDouble(this, IDC_TESS_CHORD);
    params_.chordTolerance = units_ ? units_->ToModelLength(chord) : chord;
    params_.angularTolerance = ToRadians(GetDlgDouble(this, IDC_TESS_ANGULAR));
    CDialog::OnOK();
}

// ---------------------------------------------------------------------------
// CTextDialog
// ---------------------------------------------------------------------------

CTextDialog::CTextDialog(UINT templateId, UINT textControlId, const CString& title,
                         const CString& text, CWnd* parent)
    : CDialog(templateId, parent), textControlId_(textControlId), title_(title), text_(text) {}

BOOL CTextDialog::OnInitDialog() {
    CDialog::OnInitDialog();
    SetWindowText(title_);
    // 等宽字体：那张快捷键表是靠空格对齐的。
    font_.CreatePointFont(95, _T("Consolas"));
    if (CWnd* control = GetDlgItem(static_cast<int>(textControlId_))) {
        control->SetFont(&font_);
        control->SetWindowText(text_);
    }
    return TRUE;
}

void ShowShortcutsDialog(CWnd* parent) {
    const CString text =
        _T("【文件】\r\n")
        _T("  Ctrl+N          新建（清空场景）\r\n")
        _T("  Ctrl+Shift+N    载入示例图纸\r\n")
        _T("  Ctrl+O          打开（替换场景）\r\n")
        _T("  Ctrl+I          导入（并入当前场景）\r\n")
        _T("  Ctrl+E          导出\r\n")
        _T("  Ctrl+Shift+E    导出选中\r\n")
        _T("  Ctrl+P          保存截图\r\n")
        _T("  Ctrl+Q          退出\r\n")
        _T("\r\n")
        _T("【编辑】\r\n")
        _T("  Ctrl+Z          撤销\r\n")
        _T("  Ctrl+Y  Ctrl+Shift+Z   重做\r\n")
        _T("  Del             删除选中\r\n")
        _T("  Ctrl+A          全选\r\n")
        _T("  Ctrl+Shift+A    取消选择\r\n")
        _T("  F2              重命名\r\n")
        _T("  F4              编辑参数（改半径就重新生成几何）\r\n")
        _T("  Ctrl+Shift+C    设置颜色\r\n")
        _T("  Ctrl+Shift+S    整份样式：颜色、边色、线宽、点大小、线型\r\n")
        _T("  Ctrl+T          数值变换（Gizmo 之外的那条路）\r\n")
        _T("  Ctrl+H          显示 / 隐藏选中\r\n")
        _T("  Ctrl+G          把选中的对象编成一组\r\n")
        _T("\r\n")
        _T("【创建（工具）】\r\n")
        _T("  V               选择\r\n")
        _T("  X               点\r\n")
        _T("  L               直线\r\n")
        _T("  C               圆\r\n")
        _T("  R               矩形\r\n")
        _T("  Y               多段线（Enter 结束）\r\n")
        _T("  E               拉伸\r\n")
        _T("  M / T / S       移动 / 旋转（turn）/ 缩放\r\n")
        _T("  D               测量（dimension）\r\n")
        _T("  Esc             取消当前操作，回到选择工具\r\n")
        _T("  Ctrl+W          把光标下的面设为工作平面\r\n")
        _T("\r\n")
        _T("【视图】\r\n")
        _T("  1 … 7           前 / 后 / 右 / 左 / 顶 / 底 / 等轴测\r\n")
        _T("  F               缩放至全部（有选中时缩放至选中）\r\n")
        _T("  Shift+F         缩放至选中\r\n")
        _T("  P               正交 / 透视\r\n")
        _T("  W               轮换渲染模式\r\n")
        _T("  G               网格\r\n")
        _T("  H               引擎自绘的 HUD\r\n")
        _T("  Ctrl+Shift+V    新建一个视口\r\n")
        _T("  Ctrl+Shift+W    只留一个视口\r\n")
        _T("\r\n")
        _T("【鼠标（引擎内建）】\r\n")
        _T("  左键            当前工具\r\n")
        _T("  中键拖          环绕\r\n")
        _T("  右键拖          平移\r\n")
        _T("  滚轮            以光标为中心缩放\r\n")
        _T("  中键双击        缩放至全部\r\n")
        _T("\r\n")
        _T("【其他】\r\n")
        _T("  Ctrl+U          显示单位设置\r\n")
        _T("  F1              这张表\r\n");

    CTextDialog dialog(IDD_SHORTCUTS, IDC_SHORTCUT_TEXT, _T("快捷键"), text, parent);
    dialog.DoModal();
}

void ShowAboutDialog(CWnd* parent, ICadEngine& engine, const ICadEngine2* ext,
                     const IViewport* viewport) {
    const uint32_t runtime = CadGeom_GetApiVersion();
    CString samples = _T("—");
    if (ext && viewport) {
        samples.Format(_T("%u×"), ext->GetSampleCount(viewport));
    }

    CString text;
    text.Format(
        _T("CadGeom — MFC 宿主示例\r\n")
        _T("\r\n")
        _T("宿主头文件 API 版本：%d.%d.%d\r\n")
        _T("运行时库 API 版本：  %u.%u.%u\r\n")
        _T("构建：%s\r\n")
        _T("设备：%s\r\n")
        _T("MSAA：%s\r\n")
        _T("存活接口对象：%llu\r\n")
        _T("\r\n")
        _T("整个宿主只链接一个 cadgeom 共享库，全部通过纯虚接口通信 —— 没有一个 STL ")
        _T("类型、没有一个异常跨过那道边界。\r\n")
        _T("\r\n")
        _T("同一个引擎的另外两个示例：glfw_viewer（引擎自带窗口）、qt_viewer（同样是")
        _T("宿主拥有窗口，换成 Qt 说一遍）。"),
        CADGEOM_API_VERSION_MAJOR, CADGEOM_API_VERSION_MINOR, CADGEOM_API_VERSION_PATCH,
        CADGEOM_VERSION_MAJOR(runtime), CADGEOM_VERSION_MINOR(runtime),
        CADGEOM_VERSION_PATCH(runtime), static_cast<LPCTSTR>(FromUtf8(CadGeom_GetBuildInfo())),
        static_cast<LPCTSTR>(FromUtf8(engine.GetDeviceName())), static_cast<LPCTSTR>(samples),
        static_cast<unsigned long long>(CadGeom_GetLiveObjectCount()));

    CTextDialog dialog(IDD_ABOUTBOX, IDC_ABOUT_TEXT, _T("关于 CadGeom"), text, parent);
    dialog.DoModal();
}

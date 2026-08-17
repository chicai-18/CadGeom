/**
 * @file Dialogs.cpp
 * @brief Dialogs.h 的实现。
 */
#include "Dialogs.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <cmath>

using namespace cadgeom;

namespace {

constexpr double kPi = 3.14159265358979323846;

double ToDegrees(double radians) { return radians * 180.0 / kPi; }
double ToRadians(double degrees) { return degrees * kPi / 180.0; }

/// @brief 一个数字框的通用配置：CAD 里的坐标可正可负，位数要够。
QDoubleSpinBox* MakeSpin(double value, const QString& suffix, int decimals = 4) {
    auto* spin = new QDoubleSpinBox;
    spin->setRange(-1.0e9, 1.0e9);
    spin->setDecimals(decimals);
    spin->setSingleStep(1.0);
    spin->setValue(value);
    spin->setAlignment(Qt::AlignRight);
    if (!suffix.isEmpty()) {
        spin->setSuffix(QString(" %1").arg(suffix));
    }
    spin->setKeyboardTracking(false);
    return spin;
}

double SrgbToLinear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double LinearToSrgb(double c) {
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

} // namespace

// ---------------------------------------------------------------------------

Color ColorFromQColor(const QColor& color) {
    return Color{static_cast<float>(SrgbToLinear(color.redF())),
                 static_cast<float>(SrgbToLinear(color.greenF())),
                 static_cast<float>(SrgbToLinear(color.blueF())),
                 static_cast<float>(color.alphaF())};
}

QColor ColorToQColor(const Color& color) {
    QColor out;
    out.setRgbF(LinearToSrgb(color.r), LinearToSrgb(color.g), LinearToSrgb(color.b), color.a);
    return out;
}

QString LengthUnitName(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimetre: return QStringLiteral("mm");
        case LengthUnit::Centimetre: return QStringLiteral("cm");
        case LengthUnit::Metre: return QStringLiteral("m");
        case LengthUnit::Kilometre: return QStringLiteral("km");
        case LengthUnit::Inch: return QStringLiteral("in");
        case LengthUnit::Foot: return QStringLiteral("ft");
        case LengthUnit::Yard: return QStringLiteral("yd");
        case LengthUnit::Mile: return QStringLiteral("mi");
    }
    return QString();
}

// ---------------------------------------------------------------------------
// UnitSettingsDialog
// ---------------------------------------------------------------------------

namespace {

void FillLengthUnits(QComboBox* box) {
    const LengthUnit units[] = {LengthUnit::Millimetre, LengthUnit::Centimetre,
                                LengthUnit::Metre,      LengthUnit::Kilometre,
                                LengthUnit::Inch,       LengthUnit::Foot,
                                LengthUnit::Yard,       LengthUnit::Mile};
    const char* names[] = {"毫米 (mm)", "厘米 (cm)", "米 (m)",  "千米 (km)",
                           "英寸 (in)", "英尺 (ft)", "码 (yd)", "英里 (mi)"};
    for (int i = 0; i < 8; ++i) {
        box->addItem(QString::fromUtf8(names[i]), static_cast<int>(units[i]));
    }
}

void SelectByData(QComboBox* box, int value) {
    const int index = box->findData(value);
    box->setCurrentIndex(index >= 0 ? index : 0);
}

} // namespace

UnitSettingsDialog::UnitSettingsDialog(const UnitSettings& settings, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("显示单位"));

    auto* form = new QFormLayout;

    modelUnit_ = new QComboBox;
    FillLengthUnits(modelUnit_);
    SelectByData(modelUnit_, static_cast<int>(settings.modelUnit));
    form->addRow(QStringLiteral("模型单位（1.0 代表多长）"), modelUnit_);

    displayUnit_ = new QComboBox;
    FillLengthUnits(displayUnit_);
    SelectByData(displayUnit_, static_cast<int>(settings.displayUnit));
    form->addRow(QStringLiteral("显示单位（读数用什么）"), displayUnit_);

    angleUnit_ = new QComboBox;
    angleUnit_->addItem(QStringLiteral("度 (°)"), static_cast<int>(AngleUnit::Degrees));
    angleUnit_->addItem(QStringLiteral("弧度 (rad)"), static_cast<int>(AngleUnit::Radians));
    SelectByData(angleUnit_, static_cast<int>(settings.angleUnit));
    form->addRow(QStringLiteral("角度单位"), angleUnit_);

    linearPrecision_ = new QSpinBox;
    linearPrecision_->setRange(0, 9);
    linearPrecision_->setValue(settings.linearPrecision < 0 ? 0 : settings.linearPrecision);
    form->addRow(QStringLiteral("长度小数位"), linearPrecision_);

    angularPrecision_ = new QSpinBox;
    angularPrecision_->setRange(0, 9);
    angularPrecision_->setValue(settings.angularPrecision < 0 ? 0 : settings.angularPrecision);
    form->addRow(QStringLiteral("角度小数位"), angularPrecision_);

    showSuffix_ = new QCheckBox(QStringLiteral("读数带单位后缀"));
    showSuffix_->setChecked(settings.showUnitSuffix);
    form->addRow(QString(), showSuffix_);

    auto* note = new QLabel(QStringLiteral(
        "单位只是显示层的事：换一个单位不会移动任何一个顶点，也不会让场景变脏。"));
    note->setWordWrap(true);
    note->setEnabled(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
}

UnitSettings UnitSettingsDialog::settings() const {
    UnitSettings out{};
    out.modelUnit = static_cast<LengthUnit>(modelUnit_->currentData().toInt());
    out.displayUnit = static_cast<LengthUnit>(displayUnit_->currentData().toInt());
    out.angleUnit = static_cast<AngleUnit>(angleUnit_->currentData().toInt());
    out.linearPrecision = linearPrecision_->value();
    out.angularPrecision = angularPrecision_->value();
    out.showUnitSuffix = showSuffix_->isChecked();
    return out;
}

// ---------------------------------------------------------------------------
// ShapeParamsDialog
// ---------------------------------------------------------------------------

bool ShapeParamsDialog::Supports(ShapeType type) {
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

QString ShapeParamsDialog::UnsupportedReason(ShapeType type) {
    switch (type) {
        case ShapeType::Polyline:
            return QStringLiteral(
                "多段线的点表带不过 ABI 边界：ShapeParams 是一个冻结的 POD union，"
                "没有地方放变长数组，所以 GetParams 读不回点、SetParams 也拒绝写。\n\n"
                "文件没有这个限制 —— glTF 的 extras 里带着点表，多段线能原样往返。");
        case ShapeType::Mesh:
            return QStringLiteral(
                "导入的网格没有参数化定义可谈：它的三角形就是定义本身，改不出一个"
                "「半径」来。");
        default:
            return QStringLiteral("这个对象没有可编辑的参数。");
    }
}

double ShapeParamsDialog::toDisplay(double modelValue, bool isLength) const {
    return (isLength && units_) ? units_->ToDisplayLength(modelValue) : modelValue;
}

double ShapeParamsDialog::toModel(double displayValue, bool isLength) const {
    return (isLength && units_) ? units_->ToModelLength(displayValue) : displayValue;
}

QDoubleSpinBox* ShapeParamsDialog::addScalar(QFormLayout* form, const QString& label,
                                             double modelValue, bool isLength,
                                             const QString& suffix) {
    QString effective = suffix;
    if (isLength && effective.isEmpty() && units_) {
        UnitSettings settings{};
        units_->GetUnitSettings(settings);
        effective = LengthUnitName(settings.displayUnit);
    }
    QDoubleSpinBox* spin = MakeSpin(toDisplay(modelValue, isLength), effective);
    form->addRow(label, spin);
    return spin;
}

ShapeParamsDialog::Vec3Field ShapeParamsDialog::addVec3(QFormLayout* form, const QString& label,
                                                        const Vec3d& value, bool isLength) {
    QString suffix;
    if (isLength && units_) {
        UnitSettings settings{};
        units_->GetUnitSettings(settings);
        suffix = LengthUnitName(settings.displayUnit);
    }

    Vec3Field field{};
    field.x = MakeSpin(toDisplay(value.x, isLength), suffix);
    field.y = MakeSpin(toDisplay(value.y, isLength), suffix);
    field.z = MakeSpin(toDisplay(value.z, isLength), suffix);

    auto* row = new QWidget;
    auto* box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->addWidget(field.x);
    box->addWidget(field.y);
    box->addWidget(field.z);
    form->addRow(label, row);
    return field;
}

Vec3d ShapeParamsDialog::readVec3(const Vec3Field& field, bool isLength) const {
    if (!field.x) {
        return Vec3d{0.0, 0.0, 0.0};
    }
    return Vec3d{toModel(field.x->value(), isLength), toModel(field.y->value(), isLength),
                 toModel(field.z->value(), isLength)};
}

ShapeParamsDialog::ShapeParamsDialog(const ShapeParams& params, const ICadEngine2* units,
                                     const QString& title, QWidget* parent)
    : QDialog(parent), params_(params), units_(units) {
    setWindowTitle(title);

    auto* form = new QFormLayout;
    const QString kDeg = QStringLiteral("°");

    switch (params_.type) {
        case ShapeType::Point:
            origin_ = addVec3(form, QStringLiteral("位置 X / Y / Z"), params_.point.position, true);
            break;

        case ShapeType::Line:
            origin_ = addVec3(form, QStringLiteral("起点 X / Y / Z"), params_.line.start, true);
            second_ = addVec3(form, QStringLiteral("终点 X / Y / Z"), params_.line.end, true);
            break;

        case ShapeType::Circle:
            origin_ = addVec3(form, QStringLiteral("圆心 X / Y / Z"), params_.circle.plane.origin,
                              true);
            normal_ = addVec3(form, QStringLiteral("法线 X / Y / Z"), params_.circle.plane.normal,
                              false);
            radius_ = addScalar(form, QStringLiteral("半径"), params_.circle.radius, true);
            break;

        case ShapeType::Arc:
            origin_ =
                addVec3(form, QStringLiteral("圆心 X / Y / Z"), params_.arc.plane.origin, true);
            normal_ =
                addVec3(form, QStringLiteral("法线 X / Y / Z"), params_.arc.plane.normal, false);
            radius_ = addScalar(form, QStringLiteral("半径"), params_.arc.radius, true);
            startAngle_ = addScalar(form, QStringLiteral("起始角"),
                                    ToDegrees(params_.arc.startAngle), false, kDeg);
            sweepAngle_ = addScalar(form, QStringLiteral("扫掠角"),
                                    ToDegrees(params_.arc.sweepAngle), false, kDeg);
            break;

        case ShapeType::Rectangle:
            origin_ = addVec3(form, QStringLiteral("中心 X / Y / Z"),
                              params_.rectangle.plane.origin, true);
            normal_ = addVec3(form, QStringLiteral("法线 X / Y / Z"),
                              params_.rectangle.plane.normal, false);
            second_ =
                addVec3(form, QStringLiteral("宽度方向 X / Y / Z"), params_.rectangle.uAxis, false);
            width_ = addScalar(form, QStringLiteral("宽"), params_.rectangle.width, true);
            height_ = addScalar(form, QStringLiteral("高"), params_.rectangle.height, true);
            break;

        case ShapeType::Solid: {
            normal_ = addVec3(form, QStringLiteral("扫掠方向 X / Y / Z"),
                              params_.extrude.direction, false);
            distance_ = addScalar(form, QStringLiteral("距离"), params_.extrude.distance, true);
            draftAngle_ = addScalar(form, QStringLiteral("拔模角"),
                                    ToDegrees(params_.extrude.options.draftAngle), false, kDeg);
            bothDirections_ = new QCheckBox(QStringLiteral("双向扫掠"));
            bothDirections_->setChecked(params_.extrude.options.bothDirections);
            form->addRow(QString(), bothDirections_);
            capEnds_ = new QCheckBox(QStringLiteral("封端（否则得到一张壳）"));
            capEnds_->setChecked(params_.extrude.options.capEnds);
            form->addRow(QString(), capEnds_);

            auto* note = new QLabel(QStringLiteral(
                "轮廓不能换：实体带的是轮廓的一份拷贝，换掉那个 id 只会让它和自己的"
                "几何对不上号。想换轮廓，就重新拉伸一次。"));
            note->setWordWrap(true);
            note->setEnabled(false);
            form->addRow(note);
            break;
        }

        default:
            break;
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

ShapeParams ShapeParamsDialog::params() const {
    ShapeParams out = params_;
    switch (out.type) {
        case ShapeType::Point:
            out.point.position = readVec3(origin_, true);
            break;

        case ShapeType::Line:
            out.line.start = readVec3(origin_, true);
            out.line.end = readVec3(second_, true);
            break;

        case ShapeType::Circle:
            out.circle.plane.origin = readVec3(origin_, true);
            out.circle.plane.normal = readVec3(normal_, false);
            out.circle.radius = toModel(radius_->value(), true);
            break;

        case ShapeType::Arc:
            out.arc.plane.origin = readVec3(origin_, true);
            out.arc.plane.normal = readVec3(normal_, false);
            out.arc.radius = toModel(radius_->value(), true);
            out.arc.startAngle = ToRadians(startAngle_->value());
            out.arc.sweepAngle = ToRadians(sweepAngle_->value());
            break;

        case ShapeType::Rectangle:
            out.rectangle.plane.origin = readVec3(origin_, true);
            out.rectangle.plane.normal = readVec3(normal_, false);
            out.rectangle.uAxis = readVec3(second_, false);
            out.rectangle.width = toModel(width_->value(), true);
            out.rectangle.height = toModel(height_->value(), true);
            break;

        case ShapeType::Solid:
            out.extrude.direction = readVec3(normal_, false);
            out.extrude.distance = toModel(distance_->value(), true);
            out.extrude.options.draftAngle = ToRadians(draftAngle_->value());
            out.extrude.options.bothDirections = bothDirections_->isChecked();
            out.extrude.options.capEnds = capEnds_->isChecked();
            break;

        default:
            break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// EntityStyleDialog
// ---------------------------------------------------------------------------

/// @brief 一块显示当前颜色、点开就是调色板的按钮。
class EntityStyleDialog::ColorButton : public QPushButton {
public:
    explicit ColorButton(const QColor& initial, QWidget* parent = nullptr)
        : QPushButton(parent), color_(initial) {
        setAutoFillBackground(true);
        setFlat(true);
        refresh();
        connect(this, &QPushButton::clicked, this, [this]() {
            const QColor picked = QColorDialog::getColor(color_, this, QStringLiteral("颜色"));
            if (picked.isValid()) {
                color_ = picked;
                refresh();
            }
        });
    }

    QColor color() const { return color_; }

private:
    void refresh() {
        setText(color_.name(QColor::HexRgb).toUpper());
        setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                          .arg(color_.name(),
                               color_.lightness() > 128 ? QStringLiteral("black")
                                                        : QStringLiteral("white")));
    }

    QColor color_;
};

EntityStyleDialog::EntityStyleDialog(const EntityStyle& style, QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("对象样式"));

    auto* form = new QFormLayout;

    color_ = new ColorButton(ColorToQColor(style.color));
    form->addRow(QStringLiteral("颜色"), color_);
    edgeColor_ = new ColorButton(ColorToQColor(style.edgeColor));
    form->addRow(QStringLiteral("特征边颜色"), edgeColor_);

    lineWidth_ = MakeSpin(style.lineWidth, QStringLiteral("px"), 2);
    lineWidth_->setRange(0.1, 32.0);
    lineWidth_->setSingleStep(0.25);
    form->addRow(QStringLiteral("线宽"), lineWidth_);

    pointSize_ = MakeSpin(style.pointSize, QStringLiteral("px"), 2);
    pointSize_->setRange(0.5, 64.0);
    pointSize_->setSingleStep(0.5);
    form->addRow(QStringLiteral("点大小"), pointSize_);

    lineStyle_ = new QComboBox;
    const char* const kNames[] = {"实线", "虚线", "点线", "点画线", "中心线", "隐藏线"};
    for (int i = 0; i < 6; ++i) {
        lineStyle_->addItem(QString::fromUtf8(kNames[i]), i);
    }
    SelectByData(lineStyle_, static_cast<int>(style.lineStyle));
    form->addRow(QStringLiteral("线型"), lineStyle_);

    visible_ = new QCheckBox(QStringLiteral("可见"));
    visible_->setChecked(style.visible);
    form->addRow(QString(), visible_);

    selectable_ = new QCheckBox(QStringLiteral("可拾取"));
    selectable_->setChecked(style.selectable);
    form->addRow(QString(), selectable_);

    castsShadow_ = new QCheckBox(QStringLiteral("投射阴影"));
    castsShadow_->setChecked(style.castsShadow);
    form->addRow(QString(), castsShadow_);

    auto* note = new QLabel(QStringLiteral(
        "线宽和点大小是**屏幕像素**：线和点在顶点着色器里被展开成屏幕空间的四边形，"
        "所以它们不随缩放变粗变细。"));
    note->setWordWrap(true);
    note->setEnabled(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
}

EntityStyle EntityStyleDialog::style() const {
    EntityStyle out{};
    out.color = ColorFromQColor(color_->color());
    out.edgeColor = ColorFromQColor(edgeColor_->color());
    out.lineWidth = static_cast<float>(lineWidth_->value());
    out.pointSize = static_cast<float>(pointSize_->value());
    out.lineStyle = static_cast<LineStyle>(lineStyle_->currentData().toInt());
    out.visible = visible_->isChecked();
    out.selectable = selectable_->isChecked();
    out.castsShadow = castsShadow_->isChecked();
    return out;
}

// ---------------------------------------------------------------------------
// TransformDialog
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

TransformDialog::TransformDialog(const Transform& transform, const ICadEngine2* units,
                                 QWidget* parent)
    : QDialog(parent), units_(units) {
    setWindowTitle(QStringLiteral("变换"));

    QString suffix;
    if (units_) {
        UnitSettings settings{};
        units_->GetUnitSettings(settings);
        suffix = LengthUnitName(settings.displayUnit);
    }

    const auto addRow = [](QFormLayout* form, const QString& label, QDoubleSpinBox* (&boxes)[3],
                           const Vec3d& value, const QString& fieldSuffix) {
        auto* row = new QWidget;
        auto* box = new QHBoxLayout(row);
        box->setContentsMargins(0, 0, 0, 0);
        const double values[3] = {value.x, value.y, value.z};
        for (int i = 0; i < 3; ++i) {
            boxes[i] = MakeSpin(values[i], fieldSuffix);
            box->addWidget(boxes[i]);
        }
        form->addRow(label, row);
    };

    auto* form = new QFormLayout;

    const Vec3d translation{units_ ? units_->ToDisplayLength(transform.translation.x)
                                   : transform.translation.x,
                            units_ ? units_->ToDisplayLength(transform.translation.y)
                                   : transform.translation.y,
                            units_ ? units_->ToDisplayLength(transform.translation.z)
                                   : transform.translation.z};
    addRow(form, QStringLiteral("平移 X / Y / Z"), translation_, translation, suffix);

    const Vec3d euler = QuatToEuler(transform.rotation);
    const Vec3d eulerDeg{ToDegrees(euler.x), ToDegrees(euler.y), ToDegrees(euler.z)};
    addRow(form, QStringLiteral("旋转 X / Y / Z"), rotation_, eulerDeg, QStringLiteral("°"));

    addRow(form, QStringLiteral("缩放 X / Y / Z"), scale_, transform.scale, QString());

    auto* note = new QLabel(QStringLiteral(
        "旋转按 ZYX 欧拉角显示 —— 引擎存的是四元数，但四元数不适合手输。"));
    note->setWordWrap(true);
    note->setEnabled(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
}

Transform TransformDialog::transform() const {
    Transform out{};
    const auto toModel = [this](double value) {
        return units_ ? units_->ToModelLength(value) : value;
    };
    out.translation = Vec3d{toModel(translation_[0]->value()), toModel(translation_[1]->value()),
                            toModel(translation_[2]->value())};
    out.rotation = EulerToQuat(Vec3d{ToRadians(rotation_[0]->value()),
                                     ToRadians(rotation_[1]->value()),
                                     ToRadians(rotation_[2]->value())});
    out.scale = Vec3d{scale_[0]->value(), scale_[1]->value(), scale_[2]->value()};
    return out;
}

// ---------------------------------------------------------------------------
// CameraDialog
// ---------------------------------------------------------------------------

CameraDialog::CameraDialog(const ICamera& camera, const ICadEngine2* units, QWidget* parent)
    : QDialog(parent), units_(units) {
    setWindowTitle(QStringLiteral("相机参数"));

    QString suffix;
    if (units_) {
        UnitSettings settings{};
        units_->GetUnitSettings(settings);
        suffix = LengthUnitName(settings.displayUnit);
    }
    const auto toDisplay = [this](double value) {
        return units_ ? units_->ToDisplayLength(value) : value;
    };

    double nearPlane = 0.0;
    double farPlane = 0.0;
    camera.GetNearFar(nearPlane, farPlane);

    auto* form = new QFormLayout;

    fov_ = MakeSpin(ToDegrees(camera.GetFieldOfView()), QStringLiteral("°"), 2);
    fov_->setRange(1.0, 179.0);
    form->addRow(QStringLiteral("视场角（透视）"), fov_);

    orthoHeight_ = MakeSpin(toDisplay(camera.GetOrthoHeight()), suffix);
    orthoHeight_->setRange(1.0e-6, 1.0e9);
    form->addRow(QStringLiteral("视高（正交）"), orthoHeight_);

    nearPlane_ = MakeSpin(toDisplay(nearPlane), suffix, 6);
    nearPlane_->setRange(1.0e-9, 1.0e9);
    form->addRow(QStringLiteral("近裁剪面"), nearPlane_);

    farPlane_ = MakeSpin(toDisplay(farPlane), suffix, 6);
    farPlane_->setRange(1.0e-6, 1.0e12);
    form->addRow(QStringLiteral("远裁剪面"), farPlane_);

    auto* note = new QLabel(QStringLiteral(
        "视场角只在透视下起作用，视高只在正交下起作用 —— 两个都留着，切投影时不会"
        "丢掉另一边的设置。"));
    note->setWordWrap(true);
    note->setEnabled(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
}

void CameraDialog::applyTo(ICamera& camera) const {
    const auto toModel = [this](double value) {
        return units_ ? units_->ToModelLength(value) : value;
    };
    camera.SetFieldOfView(ToRadians(fov_->value()));
    camera.SetOrthoHeight(toModel(orthoHeight_->value()));
    camera.SetNearFar(toModel(nearPlane_->value()), toModel(farPlane_->value()));
}

// ---------------------------------------------------------------------------
// TessParamsDialog
// ---------------------------------------------------------------------------

TessParamsDialog::TessParamsDialog(const TessParams& params, const ICadEngine2* units,
                                   QWidget* parent)
    : QDialog(parent), units_(units) {
    setWindowTitle(QStringLiteral("细分精度"));

    QString suffix;
    double chordDisplay = params.chordTolerance;
    if (units_) {
        UnitSettings settings{};
        units_->GetUnitSettings(settings);
        suffix = LengthUnitName(settings.displayUnit);
        chordDisplay = units_->ToDisplayLength(params.chordTolerance);
    }

    auto* form = new QFormLayout;
    chord_ = MakeSpin(chordDisplay, suffix, 5);
    chord_->setRange(0.0, 1.0e6);
    form->addRow(QStringLiteral("弦高容差"), chord_);

    angular_ = MakeSpin(ToDegrees(params.angularTolerance), QStringLiteral("°"), 3);
    angular_->setRange(0.0, 180.0);
    form->addRow(QStringLiteral("角度容差"), angular_);

    auto* note = new QLabel(QStringLiteral(
        "填 0 表示「用引擎默认值」。改动会让所有网格缓存失效并重新细分 —— 参数是"
        "真相，网格只是缓存。"));
    note->setWordWrap(true);
    note->setEnabled(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
}

TessParams TessParamsDialog::params() const {
    TessParams out{};
    out.chordTolerance = units_ ? units_->ToModelLength(chord_->value()) : chord_->value();
    out.angularTolerance = ToRadians(angular_->value());
    return out;
}

// ---------------------------------------------------------------------------
// 快捷键一览
// ---------------------------------------------------------------------------

void ShowShortcutsDialog(QWidget* parent) {
    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("快捷键"));
    dialog->resize(620, 640);

    auto* view = new QTextBrowser;
    view->setHtml(QStringLiteral(R"(
<style>
  h3 { margin: 12px 0 4px 0; }
  td { padding: 2px 14px 2px 0; }
  code { background: rgba(128,128,128,0.18); padding: 1px 5px; border-radius: 3px; }
</style>

<h3>文件</h3>
<table>
<tr><td><code>Ctrl+N</code></td><td>新建（清空场景）</td></tr>
<tr><td><code>Ctrl+Shift+N</code></td><td>载入示例图纸</td></tr>
<tr><td><code>Ctrl+O</code></td><td>打开（替换场景）</td></tr>
<tr><td><code>Ctrl+I</code></td><td>导入（并入当前场景）</td></tr>
<tr><td><code>Ctrl+E</code></td><td>导出</td></tr>
<tr><td><code>Ctrl+Shift+E</code></td><td>导出选中</td></tr>
<tr><td><code>Ctrl+P</code></td><td>保存截图</td></tr>
<tr><td><code>Ctrl+Q</code></td><td>退出</td></tr>
</table>

<h3>编辑</h3>
<table>
<tr><td><code>Ctrl+Z</code></td><td>撤销</td></tr>
<tr><td><code>Ctrl+Y</code> / <code>Ctrl+Shift+Z</code></td><td>重做</td></tr>
<tr><td><code>Del</code></td><td>删除选中</td></tr>
<tr><td><code>Ctrl+A</code></td><td>全选</td></tr>
<tr><td><code>Ctrl+Shift+A</code></td><td>取消选择</td></tr>
<tr><td><code>F2</code></td><td>重命名</td></tr>
<tr><td><code>F4</code></td><td>编辑参数（改半径就重新生成几何）</td></tr>
<tr><td><code>Ctrl+Shift+C</code></td><td>设置颜色</td></tr>
<tr><td><code>Ctrl+Shift+S</code></td><td>整份样式：颜色、边色、线宽、点大小、线型</td></tr>
<tr><td><code>Ctrl+T</code></td><td>数值变换（Gizmo 之外的那条路）</td></tr>
<tr><td><code>Ctrl+H</code></td><td>显示 / 隐藏选中</td></tr>
<tr><td><code>Ctrl+G</code></td><td>把选中的对象编成一组</td></tr>
</table>

<h3>创建（工具）</h3>
<table>
<tr><td><code>V</code></td><td>选择</td></tr>
<tr><td><code>X</code></td><td>点</td></tr>
<tr><td><code>L</code></td><td>直线</td></tr>
<tr><td><code>C</code></td><td>圆</td></tr>
<tr><td><code>R</code></td><td>矩形</td></tr>
<tr><td><code>Y</code></td><td>多段线（<code>Enter</code> 结束）</td></tr>
<tr><td><code>E</code></td><td>拉伸</td></tr>
<tr><td><code>M</code> / <code>T</code> / <code>S</code></td><td>移动 / 旋转（turn）/ 缩放</td></tr>
<tr><td><code>D</code></td><td>测量（dimension）</td></tr>
<tr><td><code>Esc</code></td><td>取消当前操作，回到选择工具</td></tr>
<tr><td><code>Ctrl+W</code></td><td>把光标下的面设为工作平面</td></tr>
</table>

<h3>视图</h3>
<table>
<tr><td><code>1</code>…<code>7</code></td><td>前 / 后 / 右 / 左 / 顶 / 底 / 等轴测</td></tr>
<tr><td><code>F</code></td><td>缩放至全部（有选中时缩放至选中）</td></tr>
<tr><td><code>Shift+F</code></td><td>缩放至选中</td></tr>
<tr><td><code>P</code></td><td>正交 / 透视</td></tr>
<tr><td><code>W</code></td><td>轮换渲染模式</td></tr>
<tr><td><code>G</code></td><td>网格</td></tr>
<tr><td><code>H</code></td><td>引擎自绘的 HUD</td></tr>
<tr><td><code>Ctrl+Shift+V</code></td><td>新建一个视口</td></tr>
<tr><td><code>Ctrl+Shift+W</code></td><td>只留一个视口</td></tr>
</table>

<h3>鼠标（引擎内建）</h3>
<table>
<tr><td>左键</td><td>当前工具</td></tr>
<tr><td>中键拖</td><td>环绕</td></tr>
<tr><td>右键拖 / Shift+中键拖</td><td>平移</td></tr>
<tr><td>滚轮</td><td>以光标为中心缩放</td></tr>
<tr><td>中键双击</td><td>缩放至全部</td></tr>
</table>

<h3>其他</h3>
<table>
<tr><td><code>Ctrl+U</code></td><td>显示单位设置</td></tr>
<tr><td><code>F1</code></td><td>这张表</td></tr>
</table>
)"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);

    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(view);
    layout->addWidget(buttons);
    dialog->show();
}

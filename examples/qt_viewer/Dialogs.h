/**
 * @file Dialogs.h
 * @brief 几个把引擎的 POD 结构摆成表单的小对话框。
 *
 * 它们存在的理由和 HUD 那段注释是同一件事：引擎自绘的 HUD 用的是笔画字体，只画
 * ASCII；真正的面板 —— 中文的、可输入的 —— 是宿主的活儿，引擎负责把数据交出来
 * （ICadEngine2::GetStatusText / FormatLength / GetMeasurement）。
 */
#ifndef CADGEOM_QT_VIEWER_DIALOGS_H
#define CADGEOM_QT_VIEWER_DIALOGS_H

#include <cadgeom/CadGeom.h>

#include <QColor>
#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QFormLayout;

/// @brief 长度单位的显示后缀（"mm"、"in" …），与 core/Units.h 的那张表一致。
QString LengthUnitName(cadgeom::LengthUnit unit);

// EntityStyle 和 ViewportDesc 里的 Color 是**线性光**里的值 —— 渲染器在半浮点目标
// 里合成，sRGB 编码发生在最后那次 blit（CLAUDE.md）。QColor 交换的是 sRGB，所以每
// 次进出都得换算一次；少了它，挑出来的灰会明显偏亮。

/// @brief sRGB 的 QColor → 线性的 Color。
cadgeom::Color ColorFromQColor(const QColor& color);
/// @brief 线性的 Color → sRGB 的 QColor。
QColor ColorToQColor(const cadgeom::Color& color);

/// @brief 显示单位设置。改它只影响读数，不动任何一个顶点。
class UnitSettingsDialog : public QDialog {
    Q_OBJECT

public:
    UnitSettingsDialog(const cadgeom::UnitSettings& settings, QWidget* parent = nullptr);

    /// @return 表单里的当前值。
    cadgeom::UnitSettings settings() const;

private:
    QComboBox* modelUnit_{nullptr};
    QComboBox* displayUnit_{nullptr};
    QComboBox* angleUnit_{nullptr};
    QSpinBox* linearPrecision_{nullptr};
    QSpinBox* angularPrecision_{nullptr};
    QCheckBox* showSuffix_{nullptr};
};

/// @brief 编辑一个形状的参数化定义 —— CAD 与「三角形编辑器」的分界线就在这里：
///        改的是半径，重新生成的是网格（docs/architecture.md §3.1）。
class ShapeParamsDialog : public QDialog {
    Q_OBJECT

public:
    /// @param params 初值，同时决定表单长什么样。
    /// @param units  用来把长度换算成显示单位；可以为 null（那就按模型单位显示）。
    /// @param title  窗口标题。
    ShapeParamsDialog(const cadgeom::ShapeParams& params, const cadgeom::ICadEngine2* units,
                      const QString& title, QWidget* parent = nullptr);

    /// @return 这个类型能不能编辑。Polyline 不能 —— ShapeParams 是个冻结的 POD
    ///         union，装不下变长的点表（CLAUDE.md「两个后果」第一条）；Mesh 也不
    ///         能，它压根没有参数化定义。
    static bool Supports(cadgeom::ShapeType type);
    /// @brief 不能编辑时给用户看的那句话。
    static QString UnsupportedReason(cadgeom::ShapeType type);

    /// @return 按表单填好的参数；未在表单里出现的字段保持原值。
    cadgeom::ShapeParams params() const;

private:
    /// @brief 一行三个数字框。`isLength` 决定要不要过单位换算。
    struct Vec3Field {
        QDoubleSpinBox* x{nullptr};
        QDoubleSpinBox* y{nullptr};
        QDoubleSpinBox* z{nullptr};
    };

    QDoubleSpinBox* addScalar(QFormLayout* form, const QString& label, double modelValue,
                              bool isLength, const QString& suffix = QString());
    Vec3Field addVec3(QFormLayout* form, const QString& label, const cadgeom::Vec3d& value,
                      bool isLength);
    cadgeom::Vec3d readVec3(const Vec3Field& field, bool isLength) const;
    double toDisplay(double modelValue, bool isLength) const;
    double toModel(double displayValue, bool isLength) const;

    cadgeom::ShapeParams params_{};
    const cadgeom::ICadEngine2* units_{nullptr};

    Vec3Field origin_{};   ///< 点的位置 / 线的起点 / 平面原点
    Vec3Field second_{};   ///< 线的终点 / 矩形的 uAxis
    Vec3Field normal_{};   ///< 平面法线 / 拉伸方向
    QDoubleSpinBox* radius_{nullptr};
    QDoubleSpinBox* width_{nullptr};
    QDoubleSpinBox* height_{nullptr};
    QDoubleSpinBox* startAngle_{nullptr};
    QDoubleSpinBox* sweepAngle_{nullptr};
    QDoubleSpinBox* distance_{nullptr};
    QDoubleSpinBox* draftAngle_{nullptr};
    QCheckBox* bothDirections_{nullptr};
    QCheckBox* capEnds_{nullptr};
};

/// @brief 整份 EntityStyle。颜色在对话框里是 sRGB，交给引擎前换成线性。
class EntityStyleDialog : public QDialog {
    Q_OBJECT

public:
    EntityStyleDialog(const cadgeom::EntityStyle& style, QWidget* parent = nullptr);

    cadgeom::EntityStyle style() const;

private:
    /// @brief 一个点开调色板的色块按钮。
    class ColorButton;

    ColorButton* color_{nullptr};
    ColorButton* edgeColor_{nullptr};
    QDoubleSpinBox* lineWidth_{nullptr};
    QDoubleSpinBox* pointSize_{nullptr};
    QComboBox* lineStyle_{nullptr};
    QCheckBox* visible_{nullptr};
    QCheckBox* selectable_{nullptr};
    QCheckBox* castsShadow_{nullptr};
};

/// @brief 数值编辑一个实体的局部变换。旋转用欧拉角（ZYX）显示，四元数不适合手输。
class TransformDialog : public QDialog {
    Q_OBJECT

public:
    TransformDialog(const cadgeom::Transform& transform, const cadgeom::ICadEngine2* units,
                    QWidget* parent = nullptr);

    cadgeom::Transform transform() const;

private:
    QDoubleSpinBox* translation_[3]{};
    QDoubleSpinBox* rotation_[3]{};
    QDoubleSpinBox* scale_[3]{};
    const cadgeom::ICadEngine2* units_{nullptr};
};

/// @brief 相机的那几个数：视场角、正交视高、近远裁剪面。
class CameraDialog : public QDialog {
    Q_OBJECT

public:
    CameraDialog(const cadgeom::ICamera& camera, const cadgeom::ICadEngine2* units,
                 QWidget* parent = nullptr);

    /// @brief 把表单里的值写回相机。
    void applyTo(cadgeom::ICamera& camera) const;

private:
    QDoubleSpinBox* fov_{nullptr};
    QDoubleSpinBox* orthoHeight_{nullptr};
    QDoubleSpinBox* nearPlane_{nullptr};
    QDoubleSpinBox* farPlane_{nullptr};
    const cadgeom::ICadEngine2* units_{nullptr};
};

/// @brief 细分精度。改它会让所有网格缓存失效并重新生成。
class TessParamsDialog : public QDialog {
    Q_OBJECT

public:
    TessParamsDialog(const cadgeom::TessParams& params, const cadgeom::ICadEngine2* units,
                     QWidget* parent = nullptr);

    cadgeom::TessParams params() const;

private:
    QDoubleSpinBox* chord_{nullptr};
    QDoubleSpinBox* angular_{nullptr};
    const cadgeom::ICadEngine2* units_{nullptr};
};

/// @brief 弹出快捷键一览。
void ShowShortcutsDialog(QWidget* parent);

#endif // CADGEOM_QT_VIEWER_DIALOGS_H

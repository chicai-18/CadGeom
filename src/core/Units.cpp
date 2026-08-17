#include "core/Units.h"

#include <stdarg.h>
#include <stdio.h>

#include <algorithm>

namespace cadgeom::core {
namespace {

/// 小数位数的合理区间。负数按 0 算，上限挡住 "%.*f" 拿到一个荒唐的宽度。
int ClampPrecision(int32_t precision) {
    return static_cast<int>(std::clamp<int32_t>(precision, 0, 9));
}

/// @brief 往 `buffer` 里写一个必定以 '\0' 结尾的串。
/// @return 实际写入的字节数，不含结尾的 '\0'。
uint32_t Emit(char* buffer, uint32_t capacity, const char* format, ...) {
    if (!buffer || capacity == 0) {
        return 0;
    }
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer, capacity, format, args);
    va_end(args);

    if (written < 0) {
        buffer[0] = '\0';
        return 0;
    }
    // vsnprintf 返回的是「本来要写多少」，截断时会大于容量。
    const uint32_t length = static_cast<uint32_t>(written);
    return length < capacity ? length : capacity - 1;
}

} // namespace

double MetresPerUnit(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimetre: return 0.001;
        case LengthUnit::Centimetre: return 0.01;
        case LengthUnit::Metre:      return 1.0;
        case LengthUnit::Kilometre:  return 1000.0;
        case LengthUnit::Inch:       return 0.0254;
        case LengthUnit::Foot:       return 0.3048;
        case LengthUnit::Yard:       return 0.9144;
        case LengthUnit::Mile:       return 1609.344;
    }
    return 1.0;
}

const char* UnitSuffix(LengthUnit unit) {
    switch (unit) {
        case LengthUnit::Millimetre: return "mm";
        case LengthUnit::Centimetre: return "cm";
        case LengthUnit::Metre:      return "m";
        case LengthUnit::Kilometre:  return "km";
        case LengthUnit::Inch:       return "in";
        case LengthUnit::Foot:       return "ft";
        case LengthUnit::Yard:       return "yd";
        case LengthUnit::Mile:       return "mi";
    }
    return "";
}

double ToDisplay(const UnitSettings& settings, double modelUnits) {
    const double display = MetresPerUnit(settings.displayUnit);
    if (!(display > 0.0)) {
        return modelUnits;
    }
    return modelUnits * MetresPerUnit(settings.modelUnit) / display;
}

double ToModel(const UnitSettings& settings, double displayValue) {
    const double model = MetresPerUnit(settings.modelUnit);
    if (!(model > 0.0)) {
        return displayValue;
    }
    return displayValue * MetresPerUnit(settings.displayUnit) / model;
}

uint32_t FormatLength(const UnitSettings& settings, double modelUnits, char* buffer,
                      uint32_t capacity) {
    double value = ToDisplay(settings, modelUnits);
    // -0.00 是格式化出来的假象，不是一个长度。
    if (value == 0.0) {
        value = 0.0;
    }
    const int precision = ClampPrecision(settings.linearPrecision);
    if (settings.showUnitSuffix) {
        return Emit(buffer, capacity, "%.*f %s", precision, value,
                    UnitSuffix(settings.displayUnit));
    }
    return Emit(buffer, capacity, "%.*f", precision, value);
}

uint32_t FormatAngle(const UnitSettings& settings, double radians, char* buffer,
                     uint32_t capacity) {
    const int precision = ClampPrecision(settings.angularPrecision);
    if (settings.angleUnit == AngleUnit::Radians) {
        return settings.showUnitSuffix
                   ? Emit(buffer, capacity, "%.*f rad", precision, radians)
                   : Emit(buffer, capacity, "%.*f", precision, radians);
    }
    // 度符号写成 ASCII 的 "deg"：HUD 用的是笔画字体，只认 ASCII，而这个串两边
    // 都要用（状态栏和宿主面板）—— 与其两套，不如一套到底。
    const double degrees = radians * 180.0 / 3.14159265358979323846;
    return settings.showUnitSuffix ? Emit(buffer, capacity, "%.*f deg", precision, degrees)
                                   : Emit(buffer, capacity, "%.*f", precision, degrees);
}

} // namespace cadgeom::core

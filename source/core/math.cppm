/// @file math.cppm
/// @brief gfx.core.math: vectors, quaternions, matrices and rigid transforms for 3D work (the physics and camera
///        libraries are built on them). `import gfx.core.math;` (not part of the gfx.core umbrella).
///
/// **Compile time first, SIMD otherwise.** Every function is `constexpr`. In a constant expression it computes in
/// plain C++ (square roots and trigonometry included, with their own implementations, as C++23 does not make
/// `<cmath>` constexpr); at run time it computes through SIMD where that pays: cglm (MIT) for 4-component vectors,
/// quaternions and 3×3 and 4×4 matrices (SSE/SSE2/AVX/NEON/WASM, scalar code elsewhere), and SIMDe (MIT) for the
/// batch functions over arrays (math_kernels.cpp). 3-component vectors compute in scalar code (cglm's do too):
/// their SIMD is the batch functions'. The instruction set is the build's (CMake GFX_SIMD_INSTRUCTION_SET).
/// Results computed at compile time and at run time agree to a few units in the last place, not bit for bit.
///
/// **Conventions.** Right-handed coordinates; column vectors, so `matrix * vector` and `first * second` applies
/// `second` first; matrices are column-major (`columns[c]` is column c, the layout graphics APIs and cglm use);
/// quaternions are Hamilton quaternions stored x, y, z, w (w last, as cglm and glm), unit length for rotations,
/// and rotate vectors as `q v q*`; angles are radians.
module;
#include <cglm/affine-mat.h>
#include <cglm/mat3.h>
#include <cglm/mat4.h>
#include <cglm/quat.h>
#include <cglm/vec4.h>

#include "math_kernels.hpp"
export module gfx.core.math;

import std;
import gfx.core.errors;

export namespace gfx
{

// ===================================================================================================== scalars

/// @brief The square root of @p value (NaN for negative values), in constant expressions too.
[[nodiscard]] constexpr auto
square_root(float value) noexcept -> float
{
    if consteval
    {
        // Special values first, then Newton's method in double from an estimate of the right exponent.
        if (value != value || value < 0.0F)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        if (value == 0.0F || value == std::numeric_limits<float>::infinity())
        {
            return value;
        }
        const int exponent = static_cast<int>((std::bit_cast<std::uint32_t>(value) >> 23U) & 0xFFU) - 127;
        double estimate = static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(exponent / 2 + 127) << 23U));
        for (int iteration = 0; iteration < 8; ++iteration)
        {
            estimate = 0.5 * (estimate + static_cast<double>(value) / estimate);
        }
        return static_cast<float>(estimate);
    }
    else
    {
        return std::sqrt(value);
    }
}

namespace math_detail
{

inline constexpr double PI = std::numbers::pi_v<double>;

/// The sine of @p angle (radians) in double: reduced to [-π/2, π/2], then its Taylor series.
constexpr auto
sine_of(double angle) noexcept -> double
{
    // Reduce to [-π, π], then to [-π/2, π/2] (sin(π - a) = sin(a)).
    double reduced = angle - 2.0 * PI * static_cast<double>(static_cast<long long>(angle / (2.0 * PI)));
    reduced = reduced > PI ? reduced - 2.0 * PI : reduced < -PI ? reduced + 2.0 * PI : reduced;
    reduced = reduced > PI / 2 ? PI - reduced : reduced < -PI / 2 ? -PI - reduced : reduced;

    // x - x³/3! + x⁵/5! - ...: terms until they no longer change the sum.
    double term = reduced;
    double sum = reduced;
    for (int power = 3; power < 40; power += 2)
    {
        term = -term * reduced * reduced / static_cast<double>((power - 1) * power);
        sum += term;
    }
    return sum;
}

/// The arc tangent of @p value in double: reduced to [0, tan(π/12)], then its Taylor series.
constexpr auto
arc_tangent_of(double value) noexcept -> double
{
    // Odd function: work on |value|; above 1, atan(x) = π/2 - atan(1/x).
    const bool negative = value < 0;
    double reduced = negative ? -value : value;
    const bool inverted = reduced > 1.0;
    reduced = inverted ? 1.0 / reduced : reduced;

    // Above tan(π/12): atan(x) = π/6 + atan((x√3 - 1) / (√3 + x)).
    constexpr double square_root_of_3 = 1.7320508075688772935;
    const bool shifted = reduced > 0.26794919243112270647;
    reduced = shifted ? (reduced * square_root_of_3 - 1.0) / (square_root_of_3 + reduced) : reduced;

    // x - x³/3 + x⁵/5 - ...
    double power = reduced;
    double sum = reduced;
    for (int degree = 3; degree < 60; degree += 2)
    {
        power = -power * reduced * reduced;
        sum += power / static_cast<double>(degree);
    }
    sum = shifted ? sum + PI / 6 : sum;
    sum = inverted ? PI / 2 - sum : sum;
    return negative ? -sum : sum;
}

} // namespace math_detail

/// @brief The sine of @p angle (radians), in constant expressions too.
[[nodiscard]] constexpr auto
sine(float angle) noexcept -> float
{
    if consteval
    {
        return static_cast<float>(math_detail::sine_of(angle));
    }
    else
    {
        return std::sin(angle);
    }
}

/// @brief The cosine of @p angle (radians), in constant expressions too.
[[nodiscard]] constexpr auto
cosine(float angle) noexcept -> float
{
    if consteval
    {
        return static_cast<float>(math_detail::sine_of(static_cast<double>(angle) + math_detail::PI / 2));
    }
    else
    {
        return std::cos(angle);
    }
}

/// @brief The tangent of @p angle (radians), in constant expressions too.
[[nodiscard]] constexpr auto
tangent(float angle) noexcept -> float
{
    if consteval
    {
        return static_cast<float>(math_detail::sine_of(angle) / math_detail::sine_of(static_cast<double>(angle) + math_detail::PI / 2));
    }
    else
    {
        return std::tan(angle);
    }
}

/// @brief The arc tangent of @p value (radians, in [-π/2, π/2]), in constant expressions too.
[[nodiscard]] constexpr auto
arc_tangent(float value) noexcept -> float
{
    if consteval
    {
        return static_cast<float>(math_detail::arc_tangent_of(value));
    }
    else
    {
        return std::atan(value);
    }
}

/// @brief The angle of the point (@p x, @p y) from the x axis (radians, in [-π, π]; atan2), in constant
///        expressions too.
[[nodiscard]] constexpr auto
arc_tangent(float y,
            float x) noexcept -> float
{
    if consteval
    {
        // The quadrant from the signs; the axes and the origin explicitly.
        constexpr double pi = math_detail::PI;
        if (x > 0)
        {
            return static_cast<float>(math_detail::arc_tangent_of(static_cast<double>(y) / x));
        }
        if (x < 0)
        {
            const double base = math_detail::arc_tangent_of(static_cast<double>(y) / x);
            return static_cast<float>(y >= 0 ? base + pi : base - pi);
        }
        return y > 0 ? static_cast<float>(pi / 2) : y < 0 ? static_cast<float>(-pi / 2) : 0.0F;
    }
    else
    {
        return std::atan2(y, x);
    }
}

/// @brief The arc sine of @p value (radians, in [-π/2, π/2]; @p value clamped to [-1, 1]), in constant
///        expressions too.
[[nodiscard]] constexpr auto
arc_sine(float value) noexcept -> float
{
    const float clamped = std::clamp(value, -1.0F, 1.0F);
    if consteval
    {
        return arc_tangent(clamped, square_root(1.0F - clamped * clamped));
    }
    else
    {
        return std::asin(clamped);
    }
}

/// @brief The arc cosine of @p value (radians, in [0, π]; @p value clamped to [-1, 1]), in constant expressions
///        too.
[[nodiscard]] constexpr auto
arc_cosine(float value) noexcept -> float
{
    const float clamped = std::clamp(value, -1.0F, 1.0F);
    if consteval
    {
        return arc_tangent(square_root(1.0F - clamped * clamped), clamped);
    }
    else
    {
        return std::acos(clamped);
    }
}

// ===================================================================================================== vectors

/// @brief A 3D vector or point: three packed floats (arrays of them are plain float arrays, x, y, z after x, y, z).
struct Vector3
{
    float x = 0; ///< First component.
    float y = 0; ///< Second component.
    float z = 0; ///< Third component.

    /// @brief Equal components (exactly).
    [[nodiscard]] constexpr auto operator==(const Vector3&) const noexcept -> bool = default;

    constexpr auto operator+=(Vector3 other) noexcept -> Vector3&
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    constexpr auto operator-=(Vector3 other) noexcept -> Vector3&
    {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    constexpr auto operator*=(float factor) noexcept -> Vector3&
    {
        x *= factor;
        y *= factor;
        z *= factor;
        return *this;
    }

    constexpr auto operator/=(float divisor) noexcept -> Vector3&
    {
        x /= divisor;
        y /= divisor;
        z /= divisor;
        return *this;
    }
};

/// @brief Component-wise sum.
[[nodiscard]] constexpr auto
operator+(Vector3 first,
          Vector3 second) noexcept -> Vector3
{
    return first += second;
}

/// @brief Component-wise difference.
[[nodiscard]] constexpr auto
operator-(Vector3 first,
          Vector3 second) noexcept -> Vector3
{
    return first -= second;
}

/// @brief The opposite vector.
[[nodiscard]] constexpr auto
operator-(Vector3 vector) noexcept -> Vector3
{
    return {.x = -vector.x, .y = -vector.y, .z = -vector.z};
}

/// @brief @p vector scaled by @p factor.
[[nodiscard]] constexpr auto
operator*(Vector3 vector,
          float factor) noexcept -> Vector3
{
    return vector *= factor;
}

/// @brief @p vector scaled by @p factor.
[[nodiscard]] constexpr auto
operator*(float factor,
          Vector3 vector) noexcept -> Vector3
{
    return vector *= factor;
}

/// @brief @p vector divided by @p divisor.
[[nodiscard]] constexpr auto
operator/(Vector3 vector,
          float divisor) noexcept -> Vector3
{
    return vector /= divisor;
}

/// @brief The dot product.
[[nodiscard]] constexpr auto
dot(Vector3 first,
    Vector3 second) noexcept -> float
{
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

/// @brief The cross product (right-handed: cross(x, y) = z).
[[nodiscard]] constexpr auto
cross(Vector3 first,
      Vector3 second) noexcept -> Vector3
{
    return {.x = first.y * second.z - first.z * second.y, .y = first.z * second.x - first.x * second.z, .z = first.x * second.y - first.y * second.x};
}

/// @brief The squared length.
[[nodiscard]] constexpr auto
length_squared(Vector3 vector) noexcept -> float
{
    return dot(vector, vector);
}

/// @brief The length.
[[nodiscard]] constexpr auto
length(Vector3 vector) noexcept -> float
{
    return square_root(length_squared(vector));
}

/// @brief @p vector at unit length; the zero vector stays zero.
[[nodiscard]] constexpr auto
normalized(Vector3 vector) noexcept -> Vector3
{
    const float size = length(vector);
    return size > 0 ? vector / size : Vector3{};
}

/// @brief The distance between two points.
[[nodiscard]] constexpr auto
distance(Vector3 first,
         Vector3 second) noexcept -> float
{
    return length(first - second);
}

/// @brief The squared distance between two points.
[[nodiscard]] constexpr auto
distance_squared(Vector3 first,
                 Vector3 second) noexcept -> float
{
    return length_squared(first - second);
}

/// @brief The smaller of each component.
[[nodiscard]] constexpr auto
component_minimum(Vector3 first,
                  Vector3 second) noexcept -> Vector3
{
    return {.x = std::min(first.x, second.x), .y = std::min(first.y, second.y), .z = std::min(first.z, second.z)};
}

/// @brief The larger of each component.
[[nodiscard]] constexpr auto
component_maximum(Vector3 first,
                  Vector3 second) noexcept -> Vector3
{
    return {.x = std::max(first.x, second.x), .y = std::max(first.y, second.y), .z = std::max(first.z, second.z)};
}

/// @brief The product of each component.
[[nodiscard]] constexpr auto
component_multiply(Vector3 first,
                   Vector3 second) noexcept -> Vector3
{
    return {.x = first.x * second.x, .y = first.y * second.y, .z = first.z * second.z};
}

/// @brief The absolute value of each component.
[[nodiscard]] constexpr auto
component_absolute(Vector3 vector) noexcept -> Vector3
{
    return {.x = std::abs(vector.x), .y = std::abs(vector.y), .z = std::abs(vector.z)};
}

/// @brief The point at @p fraction from @p first (0) to @p second (1).
[[nodiscard]] constexpr auto
interpolate(Vector3 first,
            Vector3 second,
            float fraction) noexcept -> Vector3
{
    return first + (second - first) * fraction;
}

/// @brief Every component within @p tolerance of the other's.
[[nodiscard]] constexpr auto
approximately_equal(Vector3 first,
                    Vector3 second,
                    float tolerance) noexcept -> bool
{
    return std::abs(first.x - second.x) <= tolerance && std::abs(first.y - second.y) <= tolerance && std::abs(first.z - second.z) <= tolerance;
}

/// @brief A unit vector perpendicular to @p vector (which must not be zero).
[[nodiscard]] constexpr auto
any_perpendicular(Vector3 vector) noexcept -> Vector3
{
    // Cross with the axis least aligned with the vector.
    const Vector3 size = component_absolute(vector);
    const Vector3 axis = size.x <= size.y && size.x <= size.z ? Vector3{.x = 1} : size.y <= size.z ? Vector3{.y = 1} : Vector3{.z = 1};
    return normalized(cross(vector, axis));
}

/// @brief A 4D vector (a homogeneous point, a plane, a row or column of a matrix); 16-byte aligned.
struct alignas(16) Vector4
{
    float x = 0; ///< First component.
    float y = 0; ///< Second component.
    float z = 0; ///< Third component.
    float w = 0; ///< Fourth component.

    /// @brief Equal components (exactly).
    [[nodiscard]] constexpr auto operator==(const Vector4&) const noexcept -> bool = default;
};

namespace math_detail
{

// cglm's types with gfx's layout: gfx values are copied in and out (std::bit_cast) rather than reinterpreted.
struct NativeVector4
{
    vec4 values;
};

struct NativeMatrix3
{
    mat3 values;
};

struct NativeMatrix4
{
    mat4 values;
};

static_assert(sizeof(NativeVector4) == sizeof(Vector4));

/// @p operation of cglm on two 4-component values, through its SIMD code.
template<class Value, class Operation>
auto
native_binary(const Value& first,
              const Value& second,
              Operation operation) noexcept -> Value
{
    NativeVector4 left = std::bit_cast<NativeVector4>(first);
    NativeVector4 right = std::bit_cast<NativeVector4>(second);
    NativeVector4 result{};
    operation(left.values, right.values, result.values);
    return std::bit_cast<Value>(result);
}

} // namespace math_detail

/// @brief Component-wise sum (SIMD at run time).
[[nodiscard]] constexpr auto
operator+(Vector4 first,
          Vector4 second) noexcept -> Vector4
{
    if consteval
    {
        return {.x = first.x + second.x, .y = first.y + second.y, .z = first.z + second.z, .w = first.w + second.w};
    }
    else
    {
        return math_detail::native_binary(first, second, [](vec4 left, vec4 right, vec4 result) { glm_vec4_add(left, right, result); });
    }
}

/// @brief Component-wise difference (SIMD at run time).
[[nodiscard]] constexpr auto
operator-(Vector4 first,
          Vector4 second) noexcept -> Vector4
{
    if consteval
    {
        return {.x = first.x - second.x, .y = first.y - second.y, .z = first.z - second.z, .w = first.w - second.w};
    }
    else
    {
        return math_detail::native_binary(first, second, [](vec4 left, vec4 right, vec4 result) { glm_vec4_sub(left, right, result); });
    }
}

/// @brief The opposite vector.
[[nodiscard]] constexpr auto
operator-(Vector4 vector) noexcept -> Vector4
{
    return {.x = -vector.x, .y = -vector.y, .z = -vector.z, .w = -vector.w};
}

/// @brief @p vector scaled by @p factor (SIMD at run time).
[[nodiscard]] constexpr auto
operator*(Vector4 vector,
          float factor) noexcept -> Vector4
{
    if consteval
    {
        return {.x = vector.x * factor, .y = vector.y * factor, .z = vector.z * factor, .w = vector.w * factor};
    }
    else
    {
        math_detail::NativeVector4 native = std::bit_cast<math_detail::NativeVector4>(vector);
        math_detail::NativeVector4 result{};
        glm_vec4_scale(native.values, factor, result.values);
        return std::bit_cast<Vector4>(result);
    }
}

/// @brief @p vector scaled by @p factor.
[[nodiscard]] constexpr auto
operator*(float factor,
          Vector4 vector) noexcept -> Vector4
{
    return vector * factor;
}

/// @brief The dot product (SIMD at run time).
[[nodiscard]] constexpr auto
dot(Vector4 first,
    Vector4 second) noexcept -> float
{
    if consteval
    {
        return first.x * second.x + first.y * second.y + first.z * second.z + first.w * second.w;
    }
    else
    {
        math_detail::NativeVector4 left = std::bit_cast<math_detail::NativeVector4>(first);
        math_detail::NativeVector4 right = std::bit_cast<math_detail::NativeVector4>(second);
        return glm_vec4_dot(left.values, right.values);
    }
}

/// @brief The length.
[[nodiscard]] constexpr auto
length(Vector4 vector) noexcept -> float
{
    return square_root(dot(vector, vector));
}

/// @brief @p vector at unit length; the zero vector stays zero.
[[nodiscard]] constexpr auto
normalized(Vector4 vector) noexcept -> Vector4
{
    const float size = length(vector);
    return size > 0 ? vector * (1.0F / size) : Vector4{};
}

/// @brief The smaller of each component (SIMD at run time).
[[nodiscard]] constexpr auto
component_minimum(Vector4 first,
                  Vector4 second) noexcept -> Vector4
{
    if consteval
    {
        return {.x = std::min(first.x, second.x), .y = std::min(first.y, second.y), .z = std::min(first.z, second.z), .w = std::min(first.w, second.w)};
    }
    else
    {
        return math_detail::native_binary(first, second, [](vec4 left, vec4 right, vec4 result) { glm_vec4_minv(left, right, result); });
    }
}

/// @brief The larger of each component (SIMD at run time).
[[nodiscard]] constexpr auto
component_maximum(Vector4 first,
                  Vector4 second) noexcept -> Vector4
{
    if consteval
    {
        return {.x = std::max(first.x, second.x), .y = std::max(first.y, second.y), .z = std::max(first.z, second.z), .w = std::max(first.w, second.w)};
    }
    else
    {
        return math_detail::native_binary(first, second, [](vec4 left, vec4 right, vec4 result) { glm_vec4_maxv(left, right, result); });
    }
}

/// @brief The product of each component (SIMD at run time).
[[nodiscard]] constexpr auto
component_multiply(Vector4 first,
                   Vector4 second) noexcept -> Vector4
{
    if consteval
    {
        return {.x = first.x * second.x, .y = first.y * second.y, .z = first.z * second.z, .w = first.w * second.w};
    }
    else
    {
        return math_detail::native_binary(first, second, [](vec4 left, vec4 right, vec4 result) { glm_vec4_mul(left, right, result); });
    }
}

/// @brief Every component within @p tolerance of the other's.
[[nodiscard]] constexpr auto
approximately_equal(Vector4 first,
                    Vector4 second,
                    float tolerance) noexcept -> bool
{
    return std::abs(first.x - second.x) <= tolerance && std::abs(first.y - second.y) <= tolerance && std::abs(first.z - second.z) <= tolerance
           && std::abs(first.w - second.w) <= tolerance;
}

/// @brief @p vector with a fourth component @p w (1 for a point, 0 for a direction).
[[nodiscard]] constexpr auto
to_vector4(Vector3 vector,
           float w) noexcept -> Vector4
{
    return {.x = vector.x, .y = vector.y, .z = vector.z, .w = w};
}

/// @brief The first three components of @p vector.
[[nodiscard]] constexpr auto
to_vector3(Vector4 vector) noexcept -> Vector3
{
    return {.x = vector.x, .y = vector.y, .z = vector.z};
}

// ===================================================================================================== quaternions

/// @brief Yaw, pitch and roll (radians): rotations about +Y, +X and +Z, applied roll first, then pitch, then yaw
///        (intrinsic Y-X-Z: Quaternion::from_yaw_pitch_roll()).
struct YawPitchRoll
{
    float yaw = 0;   ///< About +Y.
    float pitch = 0; ///< About +X.
    float roll = 0;  ///< About +Z.
};

struct Matrix3;

/// @brief A quaternion x i + y j + z k + w (Hamilton), stored x, y, z, w; 16-byte aligned. Rotations are unit
///        quaternions; the default is the identity.
struct alignas(16) Quaternion
{
    float x = 0; ///< i component.
    float y = 0; ///< j component.
    float z = 0; ///< k component.
    float w = 1; ///< Real component.

    /// @brief Equal components (exactly; q and -q are the same rotation but compare unequal: see same_rotation()).
    [[nodiscard]] constexpr auto operator==(const Quaternion&) const noexcept -> bool = default;

    /// @brief The rotation by @p angle (radians, counter-clockwise looking against the axis) about @p axis (unit
    ///        length).
    [[nodiscard]] static constexpr auto from_axis_angle(Vector3 axis,
                                                        float angle) noexcept -> Quaternion
    {
        const float half_sine = sine(angle * 0.5F);
        return {.x = axis.x * half_sine, .y = axis.y * half_sine, .z = axis.z * half_sine, .w = cosine(angle * 0.5F)};
    }

    /// @brief The shortest rotation taking the unit vector @p from to the unit vector @p to (a half turn about
    ///        any perpendicular axis for opposite vectors).
    [[nodiscard]] static constexpr auto from_rotation_between(Vector3 from,
                                                              Vector3 to) noexcept -> Quaternion;

    /// @brief The rotation of @p angles: `yaw * pitch * roll` (roll first).
    [[nodiscard]] static constexpr auto from_yaw_pitch_roll(YawPitchRoll angles) noexcept -> Quaternion;

    /// @brief The rotation of the rotation matrix @p rotation (orthonormal, determinant 1).
    [[nodiscard]] static constexpr auto from_rotation_matrix(const Matrix3& rotation) noexcept -> Quaternion;
};

/// @brief The Hamilton product: the rotation @p second, then @p first (SIMD at run time).
[[nodiscard]] constexpr auto
operator*(const Quaternion& first,
          const Quaternion& second) noexcept -> Quaternion
{
    if consteval
    {
        return {.x = first.w * second.x + first.x * second.w + first.y * second.z - first.z * second.y,
                .y = first.w * second.y - first.x * second.z + first.y * second.w + first.z * second.x,
                .z = first.w * second.z + first.x * second.y - first.y * second.x + first.z * second.w,
                .w = first.w * second.w - first.x * second.x - first.y * second.y - first.z * second.z};
    }
    else
    {
        return math_detail::native_binary(first, second, [](versor left, versor right, versor result) { glm_quat_mul(left, right, result); });
    }
}

/// @brief The conjugate (for a unit quaternion, the inverse rotation).
[[nodiscard]] constexpr auto
conjugate(const Quaternion& quaternion) noexcept -> Quaternion
{
    return {.x = -quaternion.x, .y = -quaternion.y, .z = -quaternion.z, .w = quaternion.w};
}

/// @brief The dot product of the components.
[[nodiscard]] constexpr auto
dot(const Quaternion& first,
    const Quaternion& second) noexcept -> float
{
    return first.x * second.x + first.y * second.y + first.z * second.z + first.w * second.w;
}

/// @brief The norm.
[[nodiscard]] constexpr auto
length(const Quaternion& quaternion) noexcept -> float
{
    return square_root(dot(quaternion, quaternion));
}

/// @brief @p quaternion at unit length (the identity for the zero quaternion; SIMD at run time).
[[nodiscard]] constexpr auto
normalized(const Quaternion& quaternion) noexcept -> Quaternion
{
    if consteval
    {
        const float size = length(quaternion);
        return size > 0 ? Quaternion{.x = quaternion.x / size, .y = quaternion.y / size, .z = quaternion.z / size, .w = quaternion.w / size}
                        : Quaternion{};
    }
    else
    {
        if (dot(quaternion, quaternion) <= 0.0F)
        {
            return {};
        }
        math_detail::NativeVector4 native = std::bit_cast<math_detail::NativeVector4>(quaternion);
        math_detail::NativeVector4 result{};
        glm_quat_normalize_to(native.values, result.values);
        return std::bit_cast<Quaternion>(result);
    }
}

/// @brief The inverse (the conjugate divided by the squared norm).
[[nodiscard]] constexpr auto
inverse(const Quaternion& quaternion) noexcept -> Quaternion
{
    const float squared = dot(quaternion, quaternion);
    const Quaternion conjugated = conjugate(quaternion);
    return {.x = conjugated.x / squared, .y = conjugated.y / squared, .z = conjugated.z / squared, .w = conjugated.w / squared};
}

/// @brief @p vector rotated by the unit quaternion @p rotation (q v q*).
[[nodiscard]] constexpr auto
rotate(const Quaternion& rotation,
       Vector3 vector) noexcept -> Vector3
{
    // v + 2w (u × v) + 2u × (u × v), with u the vector part.
    const Vector3 axis{.x = rotation.x, .y = rotation.y, .z = rotation.z};
    const Vector3 twice_cross = cross(axis, vector) * 2.0F;
    return vector + twice_cross * rotation.w + cross(axis, twice_cross);
}

/// @brief The same rotation within @p tolerance per component (q and -q are the same rotation).
[[nodiscard]] constexpr auto
same_rotation(const Quaternion& first,
              const Quaternion& second,
              float tolerance) noexcept -> bool
{
    const float sign = dot(first, second) < 0 ? -1.0F : 1.0F;
    return std::abs(first.x - sign * second.x) <= tolerance && std::abs(first.y - sign * second.y) <= tolerance
           && std::abs(first.z - sign * second.z) <= tolerance && std::abs(first.w - sign * second.w) <= tolerance;
}

/// @brief The rotation at @p fraction from @p first (0) to @p second (1), at constant angular speed along the
///        shorter way (spherical linear interpolation; normalized linear interpolation for nearly equal
///        rotations).
[[nodiscard]] constexpr auto
interpolate(const Quaternion& first,
            const Quaternion& second,
            float fraction) noexcept -> Quaternion
{
    // The shorter way: flip the second if the rotations are more than a half turn apart.
    float cosine_of_angle = dot(first, second);
    const float sign = cosine_of_angle < 0 ? -1.0F : 1.0F;
    cosine_of_angle *= sign;
    const Quaternion target{.x = second.x * sign, .y = second.y * sign, .z = second.z * sign, .w = second.w * sign};
    float first_weight = 1.0F - fraction;
    float second_weight = fraction;
    if (cosine_of_angle < 0.9995F)
    {
        const float angle = arc_cosine(cosine_of_angle);
        const float sine_of_angle = sine(angle);
        first_weight = sine(first_weight * angle) / sine_of_angle;
        second_weight = sine(second_weight * angle) / sine_of_angle;
    }
    return normalized(Quaternion{.x = first.x * first_weight + target.x * second_weight,
                                 .y = first.y * first_weight + target.y * second_weight,
                                 .z = first.z * first_weight + target.z * second_weight,
                                 .w = first.w * first_weight + target.w * second_weight});
}

/// @brief The angle (radians, in [0, π]) of the rotation of the unit quaternion @p rotation.
[[nodiscard]] constexpr auto
rotation_angle(const Quaternion& rotation) noexcept -> float
{
    return 2.0F * arc_cosine(std::abs(rotation.w));
}

constexpr auto
Quaternion::from_rotation_between(Vector3 from,
                                  Vector3 to) noexcept -> Quaternion
{
    // Opposite vectors: a half turn about any perpendicular axis; otherwise (from × to, 1 + from · to), normalized.
    const float cosine_of_angle = dot(from, to);
    if (cosine_of_angle < -0.999999F)
    {
        const Vector3 axis = any_perpendicular(from);
        return {.x = axis.x, .y = axis.y, .z = axis.z, .w = 0};
    }
    const Vector3 axis = cross(from, to);
    return normalized(Quaternion{.x = axis.x, .y = axis.y, .z = axis.z, .w = 1.0F + cosine_of_angle});
}

constexpr auto
Quaternion::from_yaw_pitch_roll(YawPitchRoll angles) noexcept -> Quaternion
{
    return from_axis_angle({.y = 1}, angles.yaw) * from_axis_angle({.x = 1}, angles.pitch) * from_axis_angle({.z = 1}, angles.roll);
}

// ===================================================================================================== matrices

/// @brief A 3×3 matrix, column-major: three packed columns (36 bytes, as cglm's mat3). Rotations, inertia tensors,
///        bases.
struct Matrix3
{
    std::array<Vector3, 3> columns{Vector3{.x = 1}, Vector3{.y = 1}, Vector3{.z = 1}}; ///< The columns; the identity by default.

    /// @brief Equal elements (exactly).
    [[nodiscard]] constexpr auto operator==(const Matrix3&) const noexcept -> bool = default;

    /// @brief The identity.
    [[nodiscard]] static constexpr auto identity() noexcept -> Matrix3
    {
        return {};
    }

    /// @brief The diagonal matrix of @p diagonal.
    [[nodiscard]] static constexpr auto from_diagonal(Vector3 diagonal) noexcept -> Matrix3
    {
        return {.columns = {Vector3{.x = diagonal.x}, Vector3{.y = diagonal.y}, Vector3{.z = diagonal.z}}};
    }

    /// @brief The rotation matrix of the unit quaternion @p rotation.
    [[nodiscard]] static constexpr auto from_rotation(const Quaternion& rotation) noexcept -> Matrix3
    {
        const float x = rotation.x;
        const float y = rotation.y;
        const float z = rotation.z;
        const float w = rotation.w;
        return {.columns = {Vector3{.x = 1 - 2 * (y * y + z * z), .y = 2 * (x * y + w * z), .z = 2 * (x * z - w * y)},
                            Vector3{.x = 2 * (x * y - w * z), .y = 1 - 2 * (x * x + z * z), .z = 2 * (y * z + w * x)},
                            Vector3{.x = 2 * (x * z + w * y), .y = 2 * (y * z - w * x), .z = 1 - 2 * (x * x + y * y)}}};
    }

    /// @brief The matrix of the cross product with @p vector: `cross_product(v) * u == cross(v, u)`.
    [[nodiscard]] static constexpr auto cross_product(Vector3 vector) noexcept -> Matrix3
    {
        return {.columns = {Vector3{.y = vector.z, .z = -vector.y}, Vector3{.x = -vector.z, .z = vector.x}, Vector3{.x = vector.y, .y = -vector.x}}};
    }

    /// @brief The outer product `first secondᵀ`.
    [[nodiscard]] static constexpr auto outer_product(Vector3 first,
                                                      Vector3 second) noexcept -> Matrix3
    {
        return {.columns = {first * second.x, first * second.y, first * second.z}};
    }

    /// @brief The element at @p row and @p column.
    [[nodiscard]] constexpr auto element(std::size_t row,
                                         std::size_t column) const noexcept -> float
    {
        const Vector3& vector = columns[column];
        return row == 0 ? vector.x : row == 1 ? vector.y : vector.z;
    }
};

/// @brief The matrix product: @p second applied first (SIMD at run time).
[[nodiscard]] constexpr auto
operator*(const Matrix3& first,
          const Matrix3& second) noexcept -> Matrix3
{
    if consteval
    {
        Matrix3 product{};
        for (std::size_t column = 0; column < 3; ++column)
        {
            const Vector3& right = second.columns[column];
            product.columns[column] = first.columns[0] * right.x + first.columns[1] * right.y + first.columns[2] * right.z;
        }
        return product;
    }
    else
    {
        math_detail::NativeMatrix3 left = std::bit_cast<math_detail::NativeMatrix3>(first);
        math_detail::NativeMatrix3 right = std::bit_cast<math_detail::NativeMatrix3>(second);
        math_detail::NativeMatrix3 result{};
        glm_mat3_mul(left.values, right.values, result.values);
        return std::bit_cast<Matrix3>(result);
    }
}

/// @brief @p matrix applied to @p vector.
[[nodiscard]] constexpr auto
operator*(const Matrix3& matrix,
          Vector3 vector) noexcept -> Vector3
{
    return matrix.columns[0] * vector.x + matrix.columns[1] * vector.y + matrix.columns[2] * vector.z;
}

/// @brief Element-wise sum.
[[nodiscard]] constexpr auto
operator+(const Matrix3& first,
          const Matrix3& second) noexcept -> Matrix3
{
    return {.columns = {first.columns[0] + second.columns[0], first.columns[1] + second.columns[1], first.columns[2] + second.columns[2]}};
}

/// @brief Element-wise difference.
[[nodiscard]] constexpr auto
operator-(const Matrix3& first,
          const Matrix3& second) noexcept -> Matrix3
{
    return {.columns = {first.columns[0] - second.columns[0], first.columns[1] - second.columns[1], first.columns[2] - second.columns[2]}};
}

/// @brief @p matrix scaled by @p factor.
[[nodiscard]] constexpr auto
operator*(const Matrix3& matrix,
          float factor) noexcept -> Matrix3
{
    return {.columns = {matrix.columns[0] * factor, matrix.columns[1] * factor, matrix.columns[2] * factor}};
}

/// @brief The transpose.
[[nodiscard]] constexpr auto
transpose(const Matrix3& matrix) noexcept -> Matrix3
{
    const auto& [first, second, third] = matrix.columns;
    return {.columns = {Vector3{.x = first.x, .y = second.x, .z = third.x}, Vector3{.x = first.y, .y = second.y, .z = third.y},
                        Vector3{.x = first.z, .y = second.z, .z = third.z}}};
}

/// @brief The determinant.
[[nodiscard]] constexpr auto
determinant(const Matrix3& matrix) noexcept -> float
{
    return dot(matrix.columns[0], cross(matrix.columns[1], matrix.columns[2]));
}

/// @brief The inverse (the matrix must be invertible).
[[nodiscard]] constexpr auto
inverse(const Matrix3& matrix) noexcept -> Matrix3
{
    // The rows of the inverse are the cross products of the columns, divided by the determinant.
    const auto& [first, second, third] = matrix.columns;
    const float factor = 1.0F / dot(first, cross(second, third));
    return transpose(Matrix3{.columns = {cross(second, third) * factor, cross(third, first) * factor, cross(first, second) * factor}});
}

/// @brief Every element within @p tolerance of the other's.
[[nodiscard]] constexpr auto
approximately_equal(const Matrix3& first,
                    const Matrix3& second,
                    float tolerance) noexcept -> bool
{
    return approximately_equal(first.columns[0], second.columns[0], tolerance) && approximately_equal(first.columns[1], second.columns[1], tolerance)
           && approximately_equal(first.columns[2], second.columns[2], tolerance);
}

constexpr auto
Quaternion::from_rotation_matrix(const Matrix3& rotation) noexcept -> Quaternion
{
    // Shepperd's method: from the largest of the trace and the diagonal elements, for precision.
    const float m00 = rotation.element(0, 0);
    const float m11 = rotation.element(1, 1);
    const float m22 = rotation.element(2, 2);
    const float trace = m00 + m11 + m22;
    if (trace > 0)
    {
        const float scale = square_root(trace + 1.0F) * 2.0F;
        return normalized(Quaternion{.x = (rotation.element(2, 1) - rotation.element(1, 2)) / scale,
                                     .y = (rotation.element(0, 2) - rotation.element(2, 0)) / scale,
                                     .z = (rotation.element(1, 0) - rotation.element(0, 1)) / scale,
                                     .w = 0.25F * scale});
    }
    if (m00 > m11 && m00 > m22)
    {
        const float scale = square_root(1.0F + m00 - m11 - m22) * 2.0F;
        return normalized(Quaternion{.x = 0.25F * scale,
                                     .y = (rotation.element(0, 1) + rotation.element(1, 0)) / scale,
                                     .z = (rotation.element(0, 2) + rotation.element(2, 0)) / scale,
                                     .w = (rotation.element(2, 1) - rotation.element(1, 2)) / scale});
    }
    if (m11 > m22)
    {
        const float scale = square_root(1.0F + m11 - m00 - m22) * 2.0F;
        return normalized(Quaternion{.x = (rotation.element(0, 1) + rotation.element(1, 0)) / scale,
                                     .y = 0.25F * scale,
                                     .z = (rotation.element(1, 2) + rotation.element(2, 1)) / scale,
                                     .w = (rotation.element(0, 2) - rotation.element(2, 0)) / scale});
    }
    const float scale = square_root(1.0F + m22 - m00 - m11) * 2.0F;
    return normalized(Quaternion{.x = (rotation.element(0, 2) + rotation.element(2, 0)) / scale,
                                 .y = (rotation.element(1, 2) + rotation.element(2, 1)) / scale,
                                 .z = 0.25F * scale,
                                 .w = (rotation.element(1, 0) - rotation.element(0, 1)) / scale});
}

/// @brief The yaw, pitch and roll of the unit quaternion @p rotation (Quaternion::from_yaw_pitch_roll()'s
///        convention); at a pitch of ±π/2 (gimbal lock), the roll is 0 and the yaw holds the whole turn.
[[nodiscard]] constexpr auto
to_yaw_pitch_roll(const Quaternion& rotation) noexcept -> YawPitchRoll
{
    // From the rotation matrix Ry Rx Rz: m12 = -sin(pitch), (m10, m11) = cos(pitch) (sin(roll), cos(roll)), m02 / m22
    // the yaw. The pitch from both its sine and cosine: an arc sine alone loses precision near a quarter turn.
    const Matrix3 matrix = Matrix3::from_rotation(rotation);
    const float cosine_of_pitch = square_root(matrix.element(1, 0) * matrix.element(1, 0) + matrix.element(1, 1) * matrix.element(1, 1));
    const float pitch = arc_tangent(-matrix.element(1, 2), cosine_of_pitch);
    if (cosine_of_pitch < 1e-5F)
    {
        return {.yaw = arc_tangent(-matrix.element(2, 0), matrix.element(0, 0)), .pitch = pitch, .roll = 0};
    }
    return {.yaw = arc_tangent(matrix.element(0, 2), matrix.element(2, 2)), .pitch = pitch, .roll = arc_tangent(matrix.element(1, 0), matrix.element(1, 1))};
}

struct Transform;

/// @brief A 4×4 matrix, column-major (as graphics APIs and cglm's mat4); 16-byte aligned. Projections, views,
///        affine transforms.
struct alignas(16) Matrix4
{
    std::array<Vector4, 4> columns{Vector4{.x = 1}, Vector4{.y = 1}, Vector4{.z = 1}, Vector4{.w = 1}}; ///< The columns; the identity by default.

    /// @brief Equal elements (exactly).
    [[nodiscard]] constexpr auto operator==(const Matrix4&) const noexcept -> bool = default;

    /// @brief The identity.
    [[nodiscard]] static constexpr auto identity() noexcept -> Matrix4
    {
        return {};
    }

    /// @brief The translation by @p offset.
    [[nodiscard]] static constexpr auto translation(Vector3 offset) noexcept -> Matrix4
    {
        Matrix4 matrix{};
        matrix.columns[3] = to_vector4(offset, 1);
        return matrix;
    }

    /// @brief The scaling by @p factors along the axes.
    [[nodiscard]] static constexpr auto scaling(Vector3 factors) noexcept -> Matrix4
    {
        return {.columns = {Vector4{.x = factors.x}, Vector4{.y = factors.y}, Vector4{.z = factors.z}, Vector4{.w = 1}}};
    }

    /// @brief @p linear (rotation, scale, shear) followed by the translation by @p offset.
    [[nodiscard]] static constexpr auto from_linear(const Matrix3& block,
                                                    Vector3 offset = {}) noexcept -> Matrix4
    {
        return {.columns = {to_vector4(block.columns[0], 0), to_vector4(block.columns[1], 0), to_vector4(block.columns[2], 0), to_vector4(offset, 1)}};
    }

    /// @brief The rotation by the unit quaternion @p orientation.
    [[nodiscard]] static constexpr auto rotation(const Quaternion& orientation) noexcept -> Matrix4
    {
        return from_linear(Matrix3::from_rotation(orientation));
    }

    /// @brief The matrix of the rigid transform @p transform.
    [[nodiscard]] static constexpr auto from_transform(const Transform& transform) noexcept -> Matrix4;

    /// @brief The element at @p row and @p column.
    [[nodiscard]] constexpr auto element(std::size_t row,
                                         std::size_t column) const noexcept -> float
    {
        const Vector4& vector = columns[column];
        return row == 0 ? vector.x : row == 1 ? vector.y : row == 2 ? vector.z : vector.w;
    }

    /// @brief The upper-left 3×3 block (rotation, scale, shear).
    [[nodiscard]] constexpr auto linear() const noexcept -> Matrix3
    {
        return {.columns = {to_vector3(columns[0]), to_vector3(columns[1]), to_vector3(columns[2])}};
    }
};

static_assert(sizeof(math_detail::NativeMatrix3) == sizeof(Matrix3) && sizeof(math_detail::NativeMatrix4) == sizeof(Matrix4)
                  && sizeof(Vector3) == 3 * sizeof(float) && sizeof(Matrix4) == 16 * sizeof(float),
              "gfx.core.math's layouts are cglm's and the batch kernels'");

/// @brief The matrix product: @p second applied first (SIMD at run time).
[[nodiscard]] constexpr auto
operator*(const Matrix4& first,
          const Matrix4& second) noexcept -> Matrix4
{
    if consteval
    {
        Matrix4 product{};
        for (std::size_t column = 0; column < 4; ++column)
        {
            const Vector4& right = second.columns[column];
            const Vector4 sum = first.columns[0] * right.x + first.columns[1] * right.y;
            product.columns[column] = sum + first.columns[2] * right.z + first.columns[3] * right.w;
        }
        return product;
    }
    else
    {
        math_detail::NativeMatrix4 left = std::bit_cast<math_detail::NativeMatrix4>(first);
        math_detail::NativeMatrix4 right = std::bit_cast<math_detail::NativeMatrix4>(second);
        math_detail::NativeMatrix4 result{};
        glm_mat4_mul(left.values, right.values, result.values);
        return std::bit_cast<Matrix4>(result);
    }
}

/// @brief @p matrix applied to @p vector (SIMD at run time).
[[nodiscard]] constexpr auto
operator*(const Matrix4& matrix,
          Vector4 vector) noexcept -> Vector4
{
    if consteval
    {
        return matrix.columns[0] * vector.x + matrix.columns[1] * vector.y + matrix.columns[2] * vector.z + matrix.columns[3] * vector.w;
    }
    else
    {
        math_detail::NativeMatrix4 native = std::bit_cast<math_detail::NativeMatrix4>(matrix);
        math_detail::NativeVector4 input = std::bit_cast<math_detail::NativeVector4>(vector);
        math_detail::NativeVector4 result{};
        glm_mat4_mulv(native.values, input.values, result.values);
        return std::bit_cast<Vector4>(result);
    }
}

/// @brief The point @p point transformed by @p matrix (w = 1, no perspective division).
[[nodiscard]] constexpr auto
transform_point(const Matrix4& matrix,
                Vector3 point) noexcept -> Vector3
{
    return to_vector3(matrix * to_vector4(point, 1));
}

/// @brief The direction @p vector transformed by @p matrix (w = 0: no translation).
[[nodiscard]] constexpr auto
transform_vector(const Matrix4& matrix,
                 Vector3 vector) noexcept -> Vector3
{
    return to_vector3(matrix * to_vector4(vector, 0));
}

/// @brief The point @p point transformed by the projective @p matrix, divided by its w.
[[nodiscard]] constexpr auto
project_point(const Matrix4& matrix,
              Vector3 point) noexcept -> Vector3
{
    const Vector4 projected = matrix * to_vector4(point, 1);
    return to_vector3(projected) / projected.w;
}

/// @brief The transpose (SIMD at run time).
[[nodiscard]] constexpr auto
transpose(const Matrix4& matrix) noexcept -> Matrix4
{
    if consteval
    {
        Matrix4 transposed{};
        for (std::size_t column = 0; column < 4; ++column)
        {
            transposed.columns[column] = {.x = matrix.element(column, 0), .y = matrix.element(column, 1), .z = matrix.element(column, 2), .w = matrix.element(column, 3)};
        }
        return transposed;
    }
    else
    {
        math_detail::NativeMatrix4 native = std::bit_cast<math_detail::NativeMatrix4>(matrix);
        math_detail::NativeMatrix4 result{};
        glm_mat4_transpose_to(native.values, result.values);
        return std::bit_cast<Matrix4>(result);
    }
}

namespace math_detail
{

/// The 2×2 sub-determinants a general 4×4 inverse and determinant are built from (rows 0-1 and 2-3).
struct Minors
{
    std::array<float, 6> upper{};
    std::array<float, 6> lower{};
};

constexpr auto
minors_of(const Matrix4& matrix) noexcept -> Minors
{
    const auto a = [&matrix](std::size_t row, std::size_t column) { return matrix.element(row, column); };
    return {.upper = {a(0, 0) * a(1, 1) - a(1, 0) * a(0, 1), a(0, 0) * a(1, 2) - a(1, 0) * a(0, 2), a(0, 0) * a(1, 3) - a(1, 0) * a(0, 3),
                      a(0, 1) * a(1, 2) - a(1, 1) * a(0, 2), a(0, 1) * a(1, 3) - a(1, 1) * a(0, 3), a(0, 2) * a(1, 3) - a(1, 2) * a(0, 3)},
            .lower = {a(2, 0) * a(3, 1) - a(3, 0) * a(2, 1), a(2, 0) * a(3, 2) - a(3, 0) * a(2, 2), a(2, 0) * a(3, 3) - a(3, 0) * a(2, 3),
                      a(2, 1) * a(3, 2) - a(3, 1) * a(2, 2), a(2, 1) * a(3, 3) - a(3, 1) * a(2, 3), a(2, 2) * a(3, 3) - a(3, 2) * a(2, 3)}};
}

constexpr auto
determinant_of(const Minors& minors) noexcept -> float
{
    const auto& [s0, s1, s2, s3, s4, s5] = minors.upper;
    const auto& [c0, c1, c2, c3, c4, c5] = minors.lower;
    return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
}

} // namespace math_detail

/// @brief The determinant (SIMD at run time).
[[nodiscard]] constexpr auto
determinant(const Matrix4& matrix) noexcept -> float
{
    if consteval
    {
        return math_detail::determinant_of(math_detail::minors_of(matrix));
    }
    else
    {
        math_detail::NativeMatrix4 native = std::bit_cast<math_detail::NativeMatrix4>(matrix);
        return glm_mat4_det(native.values);
    }
}

/// @brief The inverse (the matrix must be invertible; SIMD at run time).
[[nodiscard]] constexpr auto
inverse(const Matrix4& matrix) noexcept -> Matrix4
{
    if consteval
    {
        // The adjugate from the 2×2 minors of the upper and lower row pairs, divided by the determinant.
        const math_detail::Minors minors = math_detail::minors_of(matrix);
        const auto& [s0, s1, s2, s3, s4, s5] = minors.upper;
        const auto& [c0, c1, c2, c3, c4, c5] = minors.lower;
        const float factor = 1.0F / math_detail::determinant_of(minors);
        const auto a = [&matrix](std::size_t row, std::size_t column) { return matrix.element(row, column); };
        const std::array<std::array<float, 4>, 4> rows{{
            {a(1, 1) * c5 - a(1, 2) * c4 + a(1, 3) * c3, -a(0, 1) * c5 + a(0, 2) * c4 - a(0, 3) * c3, a(3, 1) * s5 - a(3, 2) * s4 + a(3, 3) * s3,
             -a(2, 1) * s5 + a(2, 2) * s4 - a(2, 3) * s3},
            {-a(1, 0) * c5 + a(1, 2) * c2 - a(1, 3) * c1, a(0, 0) * c5 - a(0, 2) * c2 + a(0, 3) * c1, -a(3, 0) * s5 + a(3, 2) * s2 - a(3, 3) * s1,
             a(2, 0) * s5 - a(2, 2) * s2 + a(2, 3) * s1},
            {a(1, 0) * c4 - a(1, 1) * c2 + a(1, 3) * c0, -a(0, 0) * c4 + a(0, 1) * c2 - a(0, 3) * c0, a(3, 0) * s4 - a(3, 1) * s2 + a(3, 3) * s0,
             -a(2, 0) * s4 + a(2, 1) * s2 - a(2, 3) * s0},
            {-a(1, 0) * c3 + a(1, 1) * c1 - a(1, 2) * c0, a(0, 0) * c3 - a(0, 1) * c1 + a(0, 2) * c0, -a(3, 0) * s3 + a(3, 1) * s1 - a(3, 2) * s0,
             a(2, 0) * s3 - a(2, 1) * s1 + a(2, 2) * s0},
        }};
        Matrix4 result{};
        for (std::size_t column = 0; column < 4; ++column)
        {
            result.columns[column] = {.x = rows[0][column] * factor, .y = rows[1][column] * factor, .z = rows[2][column] * factor, .w = rows[3][column] * factor};
        }
        return result;
    }
    else
    {
        math_detail::NativeMatrix4 native = std::bit_cast<math_detail::NativeMatrix4>(matrix);
        math_detail::NativeMatrix4 result{};
        glm_mat4_inv(native.values, result.values);
        return std::bit_cast<Matrix4>(result);
    }
}

/// @brief The inverse of a rigid transform (a rotation and a translation only; SIMD at run time): the transposed
///        rotation and the rotated, opposite translation.
[[nodiscard]] constexpr auto
rigid_inverse(const Matrix4& matrix) noexcept -> Matrix4
{
    if consteval
    {
        const Matrix3 rotation = transpose(matrix.linear());
        return Matrix4::from_linear(rotation, -(rotation * to_vector3(matrix.columns[3])));
    }
    else
    {
        math_detail::NativeMatrix4 native = std::bit_cast<math_detail::NativeMatrix4>(matrix);
        glm_inv_tr(native.values);
        return std::bit_cast<Matrix4>(native);
    }
}

/// @brief Every element within @p tolerance of the other's.
[[nodiscard]] constexpr auto
approximately_equal(const Matrix4& first,
                    const Matrix4& second,
                    float tolerance) noexcept -> bool
{
    for (std::size_t column = 0; column < 4; ++column)
    {
        if (!approximately_equal(first.columns[column], second.columns[column], tolerance))
        {
            return false;
        }
    }
    return true;
}

// ===================================================================================================== transforms

/// @brief A rigid transform: a rotation (unit quaternion) then a translation. Poses of bodies, cameras, shapes.
struct Transform
{
    Vector3 position{};       ///< The translation (where the origin goes).
    Quaternion orientation{}; ///< The rotation (applied first).

    /// @brief Equal components (exactly).
    [[nodiscard]] constexpr auto operator==(const Transform&) const noexcept -> bool = default;
};

/// @brief @p point transformed by @p transform.
[[nodiscard]] constexpr auto
transform_point(const Transform& transform,
                Vector3 point) noexcept -> Vector3
{
    return rotate(transform.orientation, point) + transform.position;
}

/// @brief The direction @p vector rotated by @p transform (no translation).
[[nodiscard]] constexpr auto
transform_vector(const Transform& transform,
                 Vector3 vector) noexcept -> Vector3
{
    return rotate(transform.orientation, vector);
}

/// @brief The composition: @p second applied first, then @p first.
[[nodiscard]] constexpr auto
operator*(const Transform& first,
          const Transform& second) noexcept -> Transform
{
    return {.position = transform_point(first, second.position), .orientation = first.orientation * second.orientation};
}

/// @brief The inverse transform.
[[nodiscard]] constexpr auto
inverse(const Transform& transform) noexcept -> Transform
{
    const Quaternion rotation = conjugate(transform.orientation);
    return {.position = -rotate(rotation, transform.position), .orientation = rotation};
}

/// @brief The transform at @p fraction from @p first (0) to @p second (1): positions linearly, orientations
///        spherically.
[[nodiscard]] constexpr auto
interpolate(const Transform& first,
            const Transform& second,
            float fraction) noexcept -> Transform
{
    return {.position = interpolate(first.position, second.position, fraction), .orientation = interpolate(first.orientation, second.orientation, fraction)};
}

constexpr auto
Matrix4::from_transform(const Transform& transform) noexcept -> Matrix4
{
    return from_linear(Matrix3::from_rotation(transform.orientation), transform.position);
}

// ===================================================================================================== batches

namespace math_detail
{

/// Throws when @p output is shorter than @p input.
constexpr void
require_room(std::size_t input,
             std::size_t output,
             std::string_view operation)
{
    if (output < input)
    {
        throw InvalidUsageError({.backend = std::nullopt,
                                 .operation = std::string{operation},
                                 .problem = std::format("the output holds {} elements for {} inputs", output, input),
                                 .suggestion = "give an output span at least as long as the input (it may be the input itself)"});
    }
}

/// Throws when @p first and @p second differ in length.
constexpr void
require_same_length(std::size_t first,
                    std::size_t second,
                    std::string_view operation)
{
    if (first != second)
    {
        throw InvalidUsageError({.backend = std::nullopt,
                                 .operation = std::string{operation},
                                 .problem = std::format("the destination holds {} elements and the source {}", first, second),
                                 .suggestion = "give spans of the same length (the destination may be the source)"});
    }
}

/// The batch transform of @p points by @p matrix (@p translate: points, else directions).
constexpr void
transform_batch(const Matrix4& matrix,
                std::span<const Vector3> points,
                std::span<Vector3> output,
                bool translate)
{
    if consteval
    {
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            output[index] = translate ? transform_point(matrix, points[index]) : transform_vector(matrix, points[index]);
        }
    }
    else
    {
        if (!points.empty())
        {
            math_kernels::transform_points(&matrix.columns[0].x, &points.data()->x, &output.data()->x, points.size(), translate);
        }
    }
}

} // namespace math_detail

/// @brief Every point of @p points transformed by @p matrix (w = 1, no perspective division) into @p output, which
///        may be @p points itself (SIMD at run time: four points at a time).
/// @throws InvalidUsageError when @p output is shorter than @p points.
constexpr void
transform_points(const Matrix4& matrix,
                 std::span<const Vector3> points,
                 std::span<Vector3> output)
{
    math_detail::require_room(points.size(), output.size(), "transform_points");
    math_detail::transform_batch(matrix, points, output, true);
}

/// @brief Every direction of @p vectors transformed by @p matrix (w = 0) into @p output, which may be @p vectors
///        itself (SIMD at run time).
/// @throws InvalidUsageError when @p output is shorter than @p vectors.
constexpr void
transform_vectors(const Matrix4& matrix,
                  std::span<const Vector3> vectors,
                  std::span<Vector3> output)
{
    math_detail::require_room(vectors.size(), output.size(), "transform_vectors");
    math_detail::transform_batch(matrix, vectors, output, false);
}

/// @brief Every point of @p points transformed by @p transform into @p output, which may be @p points itself
///        (SIMD at run time).
/// @throws InvalidUsageError when @p output is shorter than @p points.
constexpr void
transform_points(const Transform& transform,
                 std::span<const Vector3> points,
                 std::span<Vector3> output)
{
    math_detail::require_room(points.size(), output.size(), "transform_points");
    math_detail::transform_batch(Matrix4::from_transform(transform), points, output, true);
}

/// @brief Every direction of @p vectors rotated by @p transform into @p output, which may be @p vectors itself
///        (SIMD at run time).
/// @throws InvalidUsageError when @p output is shorter than @p vectors.
constexpr void
transform_vectors(const Transform& transform,
                  std::span<const Vector3> vectors,
                  std::span<Vector3> output)
{
    math_detail::require_room(vectors.size(), output.size(), "transform_vectors");
    math_detail::transform_batch(Matrix4::from_transform(transform), vectors, output, false);
}

/// @brief `destination[i] += factor * source[i]` (SIMD at run time: four floats at a time); @p destination may be
///        @p source.
/// @throws InvalidUsageError when the spans differ in length.
constexpr void
multiply_add(std::span<float> destination,
             std::span<const float> source,
             float factor)
{
    math_detail::require_same_length(destination.size(), source.size(), "multiply_add");
    if consteval
    {
        for (std::size_t index = 0; index < source.size(); ++index)
        {
            destination[index] = destination[index] + factor * source[index];
        }
    }
    else
    {
        if (!source.empty())
        {
            math_kernels::multiply_add(destination.data(), source.data(), factor, source.size());
        }
    }
}

/// @brief `destination[i] += factor * source[i]` for vectors (SIMD at run time); @p destination may be @p source.
/// @throws InvalidUsageError when the spans differ in length.
constexpr void
multiply_add(std::span<Vector3> destination,
             std::span<const Vector3> source,
             float factor)
{
    math_detail::require_same_length(destination.size(), source.size(), "multiply_add");
    if consteval
    {
        for (std::size_t index = 0; index < source.size(); ++index)
        {
            destination[index] = destination[index] + source[index] * factor;
        }
    }
    else
    {
        if (!source.empty())
        {
            math_kernels::multiply_add(&destination.data()->x, &source.data()->x, factor, source.size() * 3);
        }
    }
}

} // namespace gfx

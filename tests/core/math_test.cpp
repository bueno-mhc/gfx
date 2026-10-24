// gfx.core.math: results at compile time (static_assert), conventions (rotations, Euler angles, inverses), and the
// run-time SIMD paths (cglm, the SIMDe batch kernels) against the compile-time ones: inputs and expected results are
// computed in constant expressions, then recomputed at run time and compared.
#include <gtest/gtest.h>

import std;
import gfx.core.math;
import gfx.core.errors;

namespace
{

using namespace gfx;

constexpr float PI = std::numbers::pi_v<float>;

// ===================================================================================================== compile time

static_assert(square_root(4.0F) == 2.0F);
static_assert(square_root(0.0F) == 0.0F);
static_assert(std::abs(square_root(2.0F) - 1.41421356F) < 1e-6F);
static_assert(std::abs(sine(PI / 6) - 0.5F) < 1e-6F);
static_assert(std::abs(cosine(PI / 3) - 0.5F) < 1e-6F);
static_assert(std::abs(arc_tangent(1.0F, 1.0F) - PI / 4) < 1e-6F);
static_assert(cross(Vector3{.x = 1}, Vector3{.y = 1}) == Vector3{.z = 1}, "right-handed: x × y = z");
static_assert(dot(Vector3{.x = 1, .y = 2, .z = 3}, Vector3{.x = 4, .y = 5, .z = 6}) == 32);
static_assert(std::abs(length(normalized(Vector3{.x = 3, .y = 4, .z = 12})) - 1) < 1e-6F);
static_assert(normalized(Vector3{}) == Vector3{}, "the zero vector stays zero");

// A quarter turn about +Z takes +X to +Y (counter-clockwise looking down the axis).
static_assert(approximately_equal(rotate(Quaternion::from_axis_angle({.z = 1}, PI / 2), {.x = 1}), {.y = 1}, 1e-6F));

// A matrix times its inverse, at compile time.
constexpr Matrix4 SAMPLE_MATRIX = Matrix4::translation({.x = 1, .y = -2, .z = 3}) * Matrix4::rotation(Quaternion::from_axis_angle(normalized(Vector3{.x = 1, .y = 1}), 0.7F))
                                  * Matrix4::scaling({.x = 2, .y = 3, .z = 0.5F});
static_assert(approximately_equal(SAMPLE_MATRIX * inverse(SAMPLE_MATRIX), Matrix4::identity(), 1e-5F));
static_assert(std::abs(determinant(SAMPLE_MATRIX) - 3.0F) < 1e-4F, "the scaling's determinant: rotations and translations keep volumes");

// Yaw, pitch and roll round trip at compile time.
constexpr YawPitchRoll SAMPLE_ANGLES{.yaw = 0.5F, .pitch = -0.3F, .roll = 1.1F};
constexpr YawPitchRoll SAMPLE_ROUND_TRIP = to_yaw_pitch_roll(Quaternion::from_yaw_pitch_roll(SAMPLE_ANGLES));
static_assert(std::abs(SAMPLE_ROUND_TRIP.yaw - 0.5F) < 1e-5F && std::abs(SAMPLE_ROUND_TRIP.pitch + 0.3F) < 1e-5F
              && std::abs(SAMPLE_ROUND_TRIP.roll - 1.1F) < 1e-5F);

// ===================================================================================================== inputs

/// A deterministic generator of floats in [-1, 1] (xorshift), usable in constant expressions.
class Generator
{
public:
    constexpr explicit Generator(std::uint32_t seed)
        : m_state{seed}
    {
    }

    constexpr auto next() -> float
    {
        m_state ^= m_state << 13U;
        m_state ^= m_state >> 17U;
        m_state ^= m_state << 5U;
        return static_cast<float>(m_state % 20001U) / 10000.0F - 1.0F;
    }

    constexpr auto vector3() -> Vector3
    {
        return {.x = next() * 10, .y = next() * 10, .z = next() * 10};
    }

    constexpr auto vector4() -> Vector4
    {
        return {.x = next() * 10, .y = next() * 10, .z = next() * 10, .w = next() * 10};
    }

    constexpr auto rotation() -> Quaternion
    {
        return normalized(Quaternion{.x = next(), .y = next(), .z = next(), .w = next() + 1.5F});
    }

    constexpr auto transform() -> Transform
    {
        return {.position = vector3(), .orientation = rotation()};
    }

    /// An affine matrix with a well-conditioned linear part (rotation, scale between 0.5 and 2).
    constexpr auto matrix() -> Matrix4
    {
        const Vector3 scale{.x = 1.25F + 0.75F * next(), .y = 1.25F + 0.75F * next(), .z = 1.25F + 0.75F * next()};
        return Matrix4::from_transform(transform()) * Matrix4::scaling(scale);
    }

private:
    std::uint32_t m_state;
};

constexpr std::size_t COUNT = 64;

struct Inputs
{
    std::array<Vector4, COUNT> vectors{};
    std::array<Quaternion, COUNT> rotations{};
    std::array<Matrix4, COUNT> matrices{};
    std::array<Matrix3, COUNT> linear{};
};

constexpr auto
make_inputs() -> Inputs
{
    Generator generator{2026};
    Inputs inputs{};
    for (std::size_t index = 0; index < COUNT; ++index)
    {
        inputs.vectors[index] = generator.vector4();
        inputs.rotations[index] = generator.rotation();
        inputs.matrices[index] = generator.matrix();
        inputs.linear[index] = generator.matrix().linear();
    }
    return inputs;
}

/// What the SIMD paths compute, for each input and its neighbor.
struct Results
{
    std::array<Vector4, COUNT> sums{};
    std::array<float, COUNT> dots{};
    std::array<Vector4, COUNT> normalized_vectors{};
    std::array<Vector4, COUNT> minimums{};
    std::array<Quaternion, COUNT> products{};
    std::array<Quaternion, COUNT> normalized_rotations{};
    std::array<Matrix4, COUNT> matrix_products{};
    std::array<Vector4, COUNT> matrix_vectors{};
    std::array<Matrix4, COUNT> transposes{};
    std::array<float, COUNT> determinants{};
    std::array<Matrix4, COUNT> inverses{};
    std::array<Matrix4, COUNT> rigid_inverses{};
    std::array<Matrix3, COUNT> linear_products{};
};

constexpr auto
compute(const Inputs& inputs) -> Results
{
    Results results{};
    for (std::size_t index = 0; index < COUNT; ++index)
    {
        const std::size_t next = (index + 1) % COUNT;
        results.sums[index] = inputs.vectors[index] + inputs.vectors[next] * 0.5F - inputs.vectors[next];
        results.dots[index] = dot(inputs.vectors[index], inputs.vectors[next]);
        results.normalized_vectors[index] = normalized(inputs.vectors[index]);
        results.minimums[index] = component_maximum(component_minimum(inputs.vectors[index], inputs.vectors[next]), component_multiply(inputs.vectors[index], {.x = 0.5F, .y = 0.5F, .z = 0.5F, .w = 0.5F}));
        results.products[index] = inputs.rotations[index] * inputs.rotations[next];
        results.normalized_rotations[index] = normalized(Quaternion{.x = inputs.vectors[index].x, .y = inputs.vectors[index].y, .z = inputs.vectors[index].z, .w = inputs.vectors[index].w});
        results.matrix_products[index] = inputs.matrices[index] * inputs.matrices[next];
        results.matrix_vectors[index] = inputs.matrices[index] * inputs.vectors[index];
        results.transposes[index] = transpose(inputs.matrices[index]);
        results.determinants[index] = determinant(inputs.matrices[index]);
        results.inverses[index] = inverse(inputs.matrices[index]);
        results.rigid_inverses[index] = rigid_inverse(Matrix4::rotation(inputs.rotations[index]) * Matrix4::translation(inputs.vectors[index].x == 0 ? Vector3{} : to_vector3(inputs.vectors[index])));
        results.linear_products[index] = inputs.linear[index] * inputs.linear[next];
    }
    return results;
}

constexpr Inputs INPUTS = make_inputs();
constexpr Results EXPECTED = compute(INPUTS);

// ===================================================================================================== run time

TEST(MathTest, SimdPathsAgreeWithTheCompileTimePaths)
{
    // The same computations at run time: through cglm (SIMD) where the compile-time path is plain C++.
    const Results results = compute(INPUTS);
    for (std::size_t index = 0; index < COUNT; ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_TRUE(approximately_equal(results.sums[index], EXPECTED.sums[index], 1e-5F));
        EXPECT_NEAR(results.dots[index], EXPECTED.dots[index], 1e-3F);
        EXPECT_TRUE(approximately_equal(results.normalized_vectors[index], EXPECTED.normalized_vectors[index], 1e-6F));
        EXPECT_TRUE(approximately_equal(results.minimums[index], EXPECTED.minimums[index], 0));
        EXPECT_TRUE(same_rotation(results.products[index], EXPECTED.products[index], 1e-6F)) << "cglm's quaternion product is Hamilton's";
        EXPECT_TRUE(same_rotation(results.normalized_rotations[index], EXPECTED.normalized_rotations[index], 1e-6F));
        EXPECT_TRUE(approximately_equal(results.matrix_products[index], EXPECTED.matrix_products[index], 1e-4F));
        EXPECT_TRUE(approximately_equal(results.matrix_vectors[index], EXPECTED.matrix_vectors[index], 1e-4F));
        EXPECT_EQ(results.transposes[index], EXPECTED.transposes[index]);
        EXPECT_NEAR(results.determinants[index], EXPECTED.determinants[index], 1e-3F * std::abs(EXPECTED.determinants[index]));
        EXPECT_TRUE(approximately_equal(results.inverses[index], EXPECTED.inverses[index], 1e-4F));
        EXPECT_TRUE(approximately_equal(results.rigid_inverses[index], EXPECTED.rigid_inverses[index], 1e-4F));
        EXPECT_TRUE(approximately_equal(results.linear_products[index], EXPECTED.linear_products[index], 1e-4F));
    }
}

TEST(MathTest, TrigonometryAtCompileTimeMatchesTheStandardLibrary)
{
    // Angles over several turns, both signs: the compile-time series against <cmath> at run time.
    constexpr std::size_t SAMPLES = 97;
    static constexpr auto angles = [] {
        std::array<float, SAMPLES> values{};
        for (std::size_t index = 0; index < SAMPLES; ++index)
        {
            values[index] = (static_cast<float>(index) - 48.0F) * 0.29F;
        }
        return values;
    }();
    constexpr auto compile_time = [] {
        std::array<std::array<float, 4>, SAMPLES> values{};
        for (std::size_t index = 0; index < SAMPLES; ++index)
        {
            const float angle = angles[index];
            values[index] = {sine(angle), cosine(angle), arc_tangent(angle), square_root(std::abs(angle) * 37.0F)};
        }
        return values;
    }();
    for (std::size_t index = 0; index < SAMPLES; ++index)
    {
        const float angle = angles[index];
        EXPECT_NEAR(compile_time[index][0], std::sin(angle), 2e-6F) << angle;
        EXPECT_NEAR(compile_time[index][1], std::cos(angle), 2e-6F) << angle;
        EXPECT_NEAR(compile_time[index][2], std::atan(angle), 2e-6F) << angle;
        EXPECT_FLOAT_EQ(compile_time[index][3], std::sqrt(std::abs(angle) * 37.0F)) << angle;
    }
    static constexpr std::array<float, 5> inverse_inputs{-1.0F, -0.4F, 0.0F, 0.7F, 1.0F};
    constexpr auto inverse_compile_time = [] {
        std::array<std::array<float, 2>, inverse_inputs.size()> values{};
        for (std::size_t index = 0; index < inverse_inputs.size(); ++index)
        {
            values[index] = {arc_sine(inverse_inputs[index]), arc_cosine(inverse_inputs[index])};
        }
        return values;
    }();
    for (std::size_t index = 0; index < inverse_inputs.size(); ++index)
    {
        EXPECT_NEAR(inverse_compile_time[index][0], std::asin(inverse_inputs[index]), 2e-6F);
        EXPECT_NEAR(inverse_compile_time[index][1], std::acos(inverse_inputs[index]), 2e-6F);
    }
}

TEST(MathTest, RotationsFollowTheDocumentedConventions)
{
    // Yaw turns about +Y (+Z to +X), pitch about +X (+Y to +Z), roll about +Z (+X to +Y); roll applies first.
    EXPECT_TRUE(approximately_equal(rotate(Quaternion::from_yaw_pitch_roll({.yaw = PI / 2}), {.z = 1}), {.x = 1}, 1e-6F));
    EXPECT_TRUE(approximately_equal(rotate(Quaternion::from_yaw_pitch_roll({.pitch = PI / 2}), {.y = 1}), {.z = 1}, 1e-6F));
    EXPECT_TRUE(approximately_equal(rotate(Quaternion::from_yaw_pitch_roll({.roll = PI / 2}), {.x = 1}), {.y = 1}, 1e-6F));
    const Quaternion yaw_then_roll = Quaternion::from_yaw_pitch_roll({.yaw = PI / 2, .roll = PI / 2});
    EXPECT_TRUE(approximately_equal(rotate(yaw_then_roll, {.x = 1}), {.y = 1}, 1e-6F)) << "roll takes +X to +Y, then yaw leaves +Y alone";

    // Quaternions and matrices agree; matrices of rotations round trip through quaternions (every branch of Shepperd's
    // method: large trace, then each large diagonal element).
    Generator generator{7};
    for (int sample = 0; sample < 200; ++sample)
    {
        const Quaternion rotation = sample < 3 ? Quaternion::from_axis_angle(sample == 0 ? Vector3{.x = 1} : sample == 1 ? Vector3{.y = 1} : Vector3{.z = 1}, 3.0F)
                                               : generator.rotation();
        const Vector3 vector = generator.vector3();
        EXPECT_TRUE(approximately_equal(Matrix3::from_rotation(rotation) * vector, rotate(rotation, vector), 1e-4F));
        EXPECT_TRUE(same_rotation(Quaternion::from_rotation_matrix(Matrix3::from_rotation(rotation)), rotation, 1e-5F)) << sample;
        const YawPitchRoll angles = to_yaw_pitch_roll(rotation);
        EXPECT_TRUE(same_rotation(Quaternion::from_yaw_pitch_roll(angles), rotation, 1e-4F)) << sample;
    }

    // At a pitch of a quarter turn (gimbal lock), the roll is folded into the yaw and the rotation is kept.
    const Quaternion locked = Quaternion::from_yaw_pitch_roll({.yaw = 0.4F, .pitch = PI / 2, .roll = 0.3F});
    const YawPitchRoll locked_angles = to_yaw_pitch_roll(locked);
    EXPECT_EQ(locked_angles.roll, 0.0F);
    EXPECT_TRUE(same_rotation(Quaternion::from_yaw_pitch_roll(locked_angles), locked, 1e-4F));
}

TEST(MathTest, QuaternionHelpersCoverTheirEdgeCases)
{
    // The rotation between two vectors, opposite vectors included.
    const Vector3 from = normalized(Vector3{.x = 1, .y = 2, .z = -1});
    const Vector3 to = normalized(Vector3{.x = -3, .y = 0.5F, .z = 2});
    EXPECT_TRUE(approximately_equal(rotate(Quaternion::from_rotation_between(from, to), from), to, 1e-5F));
    EXPECT_TRUE(approximately_equal(rotate(Quaternion::from_rotation_between(from, -from), from), -from, 1e-5F));
    EXPECT_TRUE(same_rotation(Quaternion::from_rotation_between(from, from), Quaternion{}, 1e-6F));

    // Spherical interpolation: its ends, its middle, and the shorter way.
    const Quaternion start = Quaternion::from_axis_angle({.y = 1}, 0.2F);
    const Quaternion end = Quaternion::from_axis_angle({.y = 1}, 1.4F);
    EXPECT_TRUE(same_rotation(interpolate(start, end, 0), start, 1e-6F));
    EXPECT_TRUE(same_rotation(interpolate(start, end, 1), end, 1e-6F));
    EXPECT_TRUE(same_rotation(interpolate(start, end, 0.5F), Quaternion::from_axis_angle({.y = 1}, 0.8F), 1e-6F));
    const Quaternion flipped{.x = -end.x, .y = -end.y, .z = -end.z, .w = -end.w};
    EXPECT_TRUE(same_rotation(interpolate(start, flipped, 0.5F), Quaternion::from_axis_angle({.y = 1}, 0.8F), 1e-6F));
    EXPECT_NEAR(rotation_angle(end), 1.4F, 1e-5F);

    // Inverses: of a unit quaternion and of a scaled one.
    const Quaternion scaled{.x = 0.2F, .y = -0.4F, .z = 0.6F, .w = 2.0F};
    EXPECT_TRUE(same_rotation(scaled * inverse(scaled), Quaternion{}, 1e-6F));
    EXPECT_EQ(normalized(Quaternion{.x = 0, .y = 0, .z = 0, .w = 0}), Quaternion{}) << "the zero quaternion normalizes to the identity";
}

TEST(MathTest, MatricesAndTransformsAgree)
{
    Generator generator{99};
    for (int sample = 0; sample < 100; ++sample)
    {
        // A transform, its matrix, and both inverses.
        const Transform transform = generator.transform();
        const Vector3 point = generator.vector3();
        const Matrix4 matrix = Matrix4::from_transform(transform);
        EXPECT_TRUE(approximately_equal(transform_point(matrix, point), transform_point(transform, point), 1e-4F));
        EXPECT_TRUE(approximately_equal(transform_point(inverse(transform), transform_point(transform, point)), point, 1e-4F));
        EXPECT_TRUE(approximately_equal(rigid_inverse(matrix), inverse(matrix), 1e-4F));

        // Composition: the second transform applies first.
        const Transform other = generator.transform();
        EXPECT_TRUE(approximately_equal(transform_point(transform * other, point), transform_point(transform, transform_point(other, point)), 1e-3F));
        EXPECT_TRUE(approximately_equal(Matrix4::from_transform(transform * other), matrix * Matrix4::from_transform(other), 1e-4F));

        // 3×3 inverses and determinants.
        const Matrix3 linear = generator.matrix().linear();
        EXPECT_TRUE(approximately_equal(linear * inverse(linear), Matrix3::identity(), 1e-4F));
        EXPECT_NEAR(determinant(linear) * determinant(inverse(linear)), 1.0F, 1e-4F);
    }

    // Projective points divide by w; the cross-product matrix is the cross product.
    const Matrix4 projective{.columns = {Vector4{.x = 1}, Vector4{.y = 1}, Vector4{.z = 1, .w = -1}, Vector4{.z = -1}}};
    EXPECT_TRUE(approximately_equal(project_point(projective, {.x = 2, .y = 4, .z = -2}), {.x = 1, .y = 2, .z = -1.5F}, 1e-6F)) << "z = -2 - 1, w = 2";
    const Vector3 first{.x = 1, .y = -2, .z = 0.5F};
    const Vector3 second{.x = 0.3F, .y = 4, .z = -1};
    EXPECT_TRUE(approximately_equal(Matrix3::cross_product(first) * second, cross(first, second), 1e-6F));
    EXPECT_TRUE(approximately_equal(Matrix3::outer_product(first, second) * Vector3{.x = 1}, first * second.x, 1e-6F));
}

// ===================================================================================================== batches

/// Every point count from 0 to 13 (whole groups of four and every remainder), at compile time.
constexpr auto
compile_time_batches(bool translate)
{
    std::array<std::array<Vector3, 13>, 14> results{};
    for (std::size_t count = 0; count < 14; ++count)
    {
        Generator generator{static_cast<std::uint32_t>(count + 1)};
        std::array<Vector3, 13> points{};
        for (Vector3& point : points)
        {
            point = generator.vector3();
        }
        const Matrix4 matrix = generator.matrix();
        std::array<Vector3, 13> output{};
        if (translate)
        {
            transform_points(matrix, std::span{points}.first(count), output);
        }
        else
        {
            transform_vectors(matrix, std::span{points}.first(count), output);
        }
        results[count] = output;
    }
    return results;
}

TEST(MathTest, BatchTransformsMatchOnePointAtATime)
{
    constexpr auto POINTS = compile_time_batches(true);
    constexpr auto VECTORS = compile_time_batches(false);
    for (std::size_t count = 0; count < 14; ++count)
    {
        SCOPED_TRACE(count);

        // The same inputs at run time: through the SIMD kernel, in place and not.
        Generator generator{static_cast<std::uint32_t>(count + 1)};
        std::array<Vector3, 13> points{};
        for (Vector3& point : points)
        {
            point = generator.vector3();
        }
        const Matrix4 matrix = generator.matrix();
        std::array<Vector3, 13> output{};
        transform_points(matrix, std::span{points}.first(count), output);
        std::array<Vector3, 13> in_place = points;
        transform_vectors(matrix, std::span{in_place}.first(count), std::span{in_place}.first(count));
        for (std::size_t index = 0; index < 13; ++index)
        {
            const bool transformed = index < count;
            EXPECT_TRUE(approximately_equal(output[index], POINTS[count][index], 1e-4F)) << index;
            EXPECT_TRUE(approximately_equal(output[index], transformed ? transform_point(matrix, points[index]) : Vector3{}, 1e-4F)) << index;
            EXPECT_TRUE(approximately_equal(in_place[index], transformed ? VECTORS[count][index] : points[index], 1e-4F)) << index;
        }
    }

    // Rigid transforms through the same kernel.
    const Transform transform{.position = {.x = 1, .y = 2, .z = 3}, .orientation = Quaternion::from_axis_angle({.z = 1}, PI / 2)};
    std::array<Vector3, 5> points{Vector3{.x = 1}, Vector3{.y = 1}, Vector3{.z = 1}, Vector3{.x = -1, .y = 2}, Vector3{}};
    std::array<Vector3, 5> output{};
    transform_points(transform, points, output);
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        EXPECT_TRUE(approximately_equal(output[index], transform_point(transform, points[index]), 1e-6F)) << index;
    }
    transform_vectors(transform, points, output);
    EXPECT_TRUE(approximately_equal(output[0], {.y = 1}, 1e-6F));
}

TEST(MathTest, MultiplyAddMatchesOneElementAtATime)
{
    for (std::size_t count = 0; count < 14; ++count)
    {
        SCOPED_TRACE(count);
        std::vector<float> destination(count);
        std::vector<float> source(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            destination[index] = static_cast<float>(index) * 0.5F;
            source[index] = 3.0F - static_cast<float>(index);
        }
        multiply_add(destination, source, 0.25F);
        for (std::size_t index = 0; index < count; ++index)
        {
            EXPECT_EQ(destination[index], static_cast<float>(index) * 0.5F + 0.25F * (3.0F - static_cast<float>(index)));
        }

        // In place: x += 2x.
        multiply_add(source, source, 2.0F);
        for (std::size_t index = 0; index < count; ++index)
        {
            EXPECT_EQ(source[index], 3.0F * (3.0F - static_cast<float>(index)));
        }
    }
    std::array<Vector3, 3> velocities{Vector3{.x = 1}, Vector3{.y = 2}, Vector3{.z = 3}};
    std::array<Vector3, 3> positions{};
    multiply_add(positions, velocities, 0.5F);
    EXPECT_EQ(positions[2], (Vector3{.z = 1.5F}));
}

TEST(MathTest, BatchesRefuseShortOutputs)
{
    std::array<Vector3, 4> points{};
    std::array<Vector3, 3> output{};
    EXPECT_THROW(transform_points(Matrix4::identity(), points, output), InvalidUsageError);
    std::array<float, 2> destination{};
    std::array<float, 3> source{};
    EXPECT_THROW(multiply_add(destination, source, 1.0F), InvalidUsageError);
}

} // namespace

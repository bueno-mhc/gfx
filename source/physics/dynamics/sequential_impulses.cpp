module gfx.physics.dynamics;

import std;
import gfx.core.math;
import gfx.physics.enums;
import gfx.physics.errors;
import gfx.physics.narrow_phase;
import gfx.physics.tasks;
import gfx.physics.interface;

namespace gfx::physics
{

namespace
{

/// Below it, a mass or a length counts as zero.
constexpr float TINY = 1.0e-9F;

/// The smallest color solved on the scheduler's threads, and the range each task takes.
constexpr std::uint32_t PARALLEL_COLOR_SIZE = 128;
constexpr std::uint32_t PARALLEL_RANGE = 32;

/// No body: the world's side of a joint, or a body corrections do not move.
constexpr std::uint32_t NONE = NO_SOLVER_BODY;

/// Throws the PhysicsError of a step the solver cannot take.
[[noreturn]] void
reject_solve(std::string problem,
             std::string suggestion)
{
    throw PhysicsError({.backend = PhysicsSubsystem::DYNAMICS, .operation = "SequentialImpulseSolver::solve", .problem = std::move(problem), .suggestion = std::move(suggestion)});
}

/// @p rotation turned further by the small rotation vector @p angle (radians, world axes).
[[nodiscard]] auto
turned(const Quaternion& rotation,
       Vector3 angle) noexcept -> Quaternion
{
    const Quaternion change = Quaternion{.x = angle.x * 0.5F, .y = angle.y * 0.5F, .z = angle.z * 0.5F, .w = 0.0F} * rotation;
    return normalized(Quaternion{.x = rotation.x + change.x, .y = rotation.y + change.y, .z = rotation.z + change.z, .w = rotation.w + change.w});
}

[[nodiscard]] auto
frame_of(const SolverBody& body) noexcept -> ConstraintFrame
{
    return {.center = body.center, .orientation = body.orientation, .linear_velocity = body.linear_velocity, .angular_velocity = body.angular_velocity};
}

/// The two bodies of a constraint: both readable (the world as a body at rest), each writable when it moves.
struct BodyPair
{
    const SolverBody* first = nullptr;
    const SolverBody* second = nullptr;
    SolverBody* first_moving = nullptr;
    SolverBody* second_moving = nullptr;
};

// ------------------------------------------------------------------------------------------------ rows

/// The rate of @p row at the bodies' velocities.
[[nodiscard]] auto
rate_of(const ConstraintRow& row,
        const BodyPair& bodies) noexcept -> float
{
    return dot(row.linear_first, bodies.first->linear_velocity) + dot(row.angular_first, bodies.first->angular_velocity)
           + dot(row.linear_second, bodies.second->linear_velocity) + dot(row.angular_second, bodies.second->angular_velocity);
}

/// The inverse effective mass of @p row (J M⁻¹ Jᵀ).
[[nodiscard]] auto
inverse_mass_of(const ConstraintRow& row,
                const BodyPair& bodies) noexcept -> float
{
    float sum = 0.0F;
    if (const SolverBody* body = bodies.first_moving)
    {
        sum += body->inverse_mass * dot(row.linear_first, component_multiply(row.linear_first, body->linear_freedom)) + dot(row.angular_first, body->inverse_inertia * row.angular_first);
    }
    if (const SolverBody* body = bodies.second_moving)
    {
        sum += body->inverse_mass * dot(row.linear_second, component_multiply(row.linear_second, body->linear_freedom))
               + dot(row.angular_second, body->inverse_inertia * row.angular_second);
    }
    return sum;
}

/// The bodies' velocities changed by @p impulse along @p row.
void
apply_row(const ConstraintRow& row,
          const BodyPair& bodies,
          float impulse) noexcept
{
    if (SolverBody* body = bodies.first_moving)
    {
        body->linear_velocity += component_multiply(row.linear_first, body->linear_freedom) * (body->inverse_mass * impulse);
        body->angular_velocity += body->inverse_inertia * (row.angular_first * impulse);
    }
    if (SolverBody* body = bodies.second_moving)
    {
        body->linear_velocity += component_multiply(row.linear_second, body->linear_freedom) * (body->inverse_mass * impulse);
        body->angular_velocity += body->inverse_inertia * (row.angular_second * impulse);
    }
}

/// The bodies' positions changed by the positional impulse @p impulse along @p row.
void
move_row(const ConstraintRow& row,
         const BodyPair& bodies,
         float impulse) noexcept
{
    if (SolverBody* body = bodies.first_moving)
    {
        body->center += component_multiply(row.linear_first, body->linear_freedom) * (body->inverse_mass * impulse);
        body->orientation = turned(body->orientation, body->inverse_inertia * (row.angular_first * impulse));
    }
    if (SolverBody* body = bodies.second_moving)
    {
        body->center += component_multiply(row.linear_second, body->linear_freedom) * (body->inverse_mass * impulse);
        body->orientation = turned(body->orientation, body->inverse_inertia * (row.angular_second * impulse));
    }
}

/// Whether @p row turns only (no linear part): its slop and largest correction are angles.
[[nodiscard]] auto
is_angular(const ConstraintRow& row) noexcept -> bool
{
    return row.linear_first == Vector3{} && row.linear_second == Vector3{};
}

// ------------------------------------------------------------------------------------------------ contacts

/// The velocity of @p body's point at @p lever from its center.
[[nodiscard]] auto
point_velocity(const SolverBody& body,
               Vector3 lever) noexcept -> Vector3
{
    return body.linear_velocity + cross(body.angular_velocity, lever);
}

/// The inverse effective mass of an impulse along @p direction at the levers @p first and @p second.
[[nodiscard]] auto
contact_inverse_mass(const BodyPair& bodies,
                     Vector3 first,
                     Vector3 second,
                     Vector3 direction) noexcept -> float
{
    float sum = 0.0F;
    if (const SolverBody* body = bodies.first_moving)
    {
        const Vector3 arm = cross(first, direction);
        sum += body->inverse_mass * dot(direction, component_multiply(direction, body->linear_freedom)) + dot(arm, body->inverse_inertia * arm);
    }
    if (const SolverBody* body = bodies.second_moving)
    {
        const Vector3 arm = cross(second, direction);
        sum += body->inverse_mass * dot(direction, component_multiply(direction, body->linear_freedom)) + dot(arm, body->inverse_inertia * arm);
    }
    return sum;
}

/// The impulse @p impulse at the levers: away from the first body, onto the second.
void
apply_impulse(const BodyPair& bodies,
              Vector3 first,
              Vector3 second,
              Vector3 impulse) noexcept
{
    if (SolverBody* body = bodies.first_moving)
    {
        body->linear_velocity -= component_multiply(impulse, body->linear_freedom) * body->inverse_mass;
        body->angular_velocity -= body->inverse_inertia * cross(first, impulse);
    }
    if (SolverBody* body = bodies.second_moving)
    {
        body->linear_velocity += component_multiply(impulse, body->linear_freedom) * body->inverse_mass;
        body->angular_velocity += body->inverse_inertia * cross(second, impulse);
    }
}

/// A contact point as the solver works on it.
struct PreparedPoint
{
    Vector3 lever_first{};          ///< From the first center to the point.
    Vector3 lever_second{};         ///< From the second center.
    Vector3 local_first{};          ///< The point on the first shape's core, about the first center in its orientation.
    Vector3 local_second{};         ///< On the second's.
    float separation = 0.0F;        ///< When prepared.
    float normal_mass = 0.0F;       ///< Effective mass along the normal.
    std::array<float, 2> tangent_mass{};
    float normal_impulse = 0.0F;    ///< Accumulated.
    std::array<float, 2> tangent_impulse{};
    float relative_velocity = 0.0F; ///< Along the normal, before the solve (restitution).
    float largest_impulse = 0.0F;   ///< The largest normal impulse of the solve (restitution applies only to points that pushed).
};

/// A pair's contacts as the solver works on them.
struct PreparedContact
{
    std::uint32_t contact = 0;
    BodyPair bodies{};
    Vector3 normal{};
    std::array<Vector3, 2> tangents{};
    Vector3 local_normal{}; ///< The normal in the first body's orientation.
    std::array<PreparedPoint, MAXIMUM_MANIFOLD_POINTS> points{};
    std::uint32_t point_count = 0;
    float static_friction = 0.0F;
    float dynamic_friction = 0.0F;
    float rolling = 0.0F;
    float restitution = 0.0F;
    Vector3 rolling_impulse{};
    Matrix3 rolling_mass{.columns = {Vector3{}, Vector3{}, Vector3{}}};
    float first_margin = 0.0F;  ///< The shapes' margins (their points kept on their cores).
    float second_margin = 0.0F;
};

/// Equality rows solved together at most (a point and an orientation held).
constexpr std::uint32_t MAXIMUM_BLOCK = 6;

/// Blocks a joint keeps at most (further runs of equalities are solved row by row).
constexpr std::uint32_t MAXIMUM_BLOCKS = 4;

/// A run of equality rows solved as one system (a point held: three rows coupled through the levers), its matrix
/// factored once per step.
struct RowBlock
{
    std::uint32_t start = 0;
    std::uint32_t size = 0;
    std::array<float, MAXIMUM_BLOCK * MAXIMUM_BLOCK> factor{}; ///< Cholesky's lower triangle, rows of MAXIMUM_BLOCK.
};

/// A joint (or user constraint) as the solver works on it.
struct PreparedJoint
{
    std::uint32_t joint = 0;
    BodyPair bodies{};
    std::uint32_t row_count = 0;
    std::array<ConstraintRow, MAXIMUM_CONSTRAINT_ROWS> rows{};
    std::array<float, MAXIMUM_CONSTRAINT_ROWS> masses{};
    std::array<float, MAXIMUM_CONSTRAINT_ROWS> biases{};
    std::array<float, MAXIMUM_CONSTRAINT_ROWS> softness{};
    std::array<float, MAXIMUM_CONSTRAINT_ROWS> impulses{};
    std::array<RowBlock, MAXIMUM_BLOCKS> blocks{};
    std::uint32_t block_count = 0;
};

/// J₁ M⁻¹ J₂ᵀ of two rows.
[[nodiscard]] auto
coupling(const ConstraintRow& first,
         const ConstraintRow& second,
         const BodyPair& bodies) noexcept -> float
{
    float sum = 0.0F;
    if (const SolverBody* body = bodies.first_moving)
    {
        sum += body->inverse_mass * dot(first.linear_first, component_multiply(second.linear_first, body->linear_freedom))
               + dot(first.angular_first, body->inverse_inertia * second.angular_first);
    }
    if (const SolverBody* body = bodies.second_moving)
    {
        sum += body->inverse_mass * dot(first.linear_second, component_multiply(second.linear_second, body->linear_freedom))
               + dot(first.angular_second, body->inverse_inertia * second.angular_second);
    }
    return sum;
}

/// The matrix of the rows [@p start, @p start + @p size) factored in place (Cholesky); false when it is singular.
[[nodiscard]] auto
factor_block(std::span<const ConstraintRow> rows,
             std::uint32_t start,
             std::uint32_t size,
             const BodyPair& bodies,
             std::array<float, MAXIMUM_BLOCK * MAXIMUM_BLOCK>& factor) noexcept -> bool
{
    const auto at = [&factor](std::uint32_t row, std::uint32_t column) -> float& { return factor[row * MAXIMUM_BLOCK + column]; };
    for (std::uint32_t row = 0; row < size; ++row)
    {
        for (std::uint32_t column = 0; column <= row; ++column)
        {
            at(row, column) = coupling(rows[start + row], rows[start + column], bodies);
        }
    }
    for (std::uint32_t column = 0; column < size; ++column)
    {
        float diagonal = at(column, column);
        for (std::uint32_t inner = 0; inner < column; ++inner)
        {
            diagonal -= at(column, inner) * at(column, inner);
        }
        if (!(diagonal > TINY * std::max(1.0F, std::abs(at(column, column)))))
        {
            return false;
        }
        at(column, column) = std::sqrt(diagonal);
        for (std::uint32_t row = column + 1; row < size; ++row)
        {
            float value = at(row, column);
            for (std::uint32_t inner = 0; inner < column; ++inner)
            {
                value -= at(row, inner) * at(column, inner);
            }
            at(row, column) = value / at(column, column);
        }
    }
    return true;
}

/// Solves L Lᵀ x = b in place of @p values.
void
solve_block(const std::array<float, MAXIMUM_BLOCK * MAXIMUM_BLOCK>& factor,
            std::uint32_t size,
            std::array<float, MAXIMUM_BLOCK>& values) noexcept
{
    for (std::uint32_t row = 0; row < size; ++row)
    {
        for (std::uint32_t column = 0; column < row; ++column)
        {
            values[row] -= factor[row * MAXIMUM_BLOCK + column] * values[column];
        }
        values[row] /= factor[row * MAXIMUM_BLOCK + row];
    }
    for (std::uint32_t row = size; row-- > 0;)
    {
        for (std::uint32_t column = row + 1; column < size; ++column)
        {
            values[row] -= factor[column * MAXIMUM_BLOCK + row] * values[column];
        }
        values[row] /= factor[row * MAXIMUM_BLOCK + row];
    }
}

/// The runs of equality rows of @p rows (two to MAXIMUM_BLOCK each), factored, into @p blocks.
auto
find_blocks(std::span<const ConstraintRow> rows,
            const BodyPair& bodies,
            std::array<RowBlock, MAXIMUM_BLOCKS>& blocks) noexcept -> std::uint32_t
{
    std::uint32_t count = 0;
    const auto total = static_cast<std::uint32_t>(rows.size());
    for (std::uint32_t row = 0; row < total && count < MAXIMUM_BLOCKS;)
    {
        if (rows[row].kind != ConstraintRowKind::EQUALITY)
        {
            ++row;
            continue;
        }
        std::uint32_t end = row;
        while (end < total && end - row < MAXIMUM_BLOCK && rows[end].kind == ConstraintRowKind::EQUALITY)
        {
            ++end;
        }
        RowBlock& block = blocks[count];
        block.start = row;
        block.size = end - row;
        if (block.size >= 2 && factor_block(rows, block.start, block.size, bodies, block.factor))
        {
            ++count;
        }
        row = end;
    }
    return count;
}

/// The rows of @p joint with its bodies where they are now.
auto
rows_of(const SolverJoint& joint,
        const BodyPair& bodies,
        std::span<ConstraintRow, MAXIMUM_CONSTRAINT_ROWS> rows) noexcept -> std::uint32_t
{
    const ConstraintFrame first = frame_of(*bodies.first);
    const ConstraintFrame second = frame_of(*bodies.second);
    if (joint.user != nullptr)
    {
        const std::uint32_t count = std::min(joint.user->row_count(), MAXIMUM_CONSTRAINT_ROWS);
        joint.user->rows(first, second, std::span<ConstraintRow>{rows}.first(count));
        return count;
    }
    return joint_rows(joint, first, second, rows);
}

/// Constraints in colors: no two of a color move one body.
struct Coloring
{
    std::vector<std::uint32_t> order{};  ///< Constraint indices, color after color.
    std::vector<std::uint32_t> starts{}; ///< Where each color begins, then where the last ends.
};

} // namespace

// ===================================================================================================== the solver's state

struct SequentialImpulseSolver::State
{
    std::vector<PreparedContact> contacts{};
    std::vector<PreparedJoint> joints{};
    Coloring contact_colors{};
    Coloring joint_colors{};
    std::vector<std::uint32_t> colors{};
    std::vector<std::uint32_t> remaining{};
    std::vector<std::uint32_t> next{};
    std::vector<std::uint64_t> taken{};
    SolverBody world{};

    /// The bodies of indices @p first and @p second.
    [[nodiscard]] auto pair_of(std::span<SolverBody> bodies,
                               std::uint32_t first,
                               std::uint32_t second) noexcept -> BodyPair
    {
        const auto readable = [&](std::uint32_t index) -> const SolverBody* { return index == NONE ? &world : &bodies[index]; };
        const auto writable = [&](std::uint32_t index) -> SolverBody* { return index != NONE && bodies[index].inverse_mass > 0.0F ? &bodies[index] : nullptr; };
        return {.first = readable(first), .second = readable(second), .first_moving = writable(first), .second_moving = writable(second)};
    }

    /// Colors @p count constraints whose moving bodies @p bodies_of gives (NONE for a body that does not move).
    template<class BodiesOf>
    void color(Coloring& coloring,
               std::uint32_t count,
               std::size_t body_count,
               BodiesOf&& bodies_of)
    {
        // Greedily, 64 colors at a time: the lowest color neither body has yet.
        colors.assign(count, 0);
        remaining.resize(count);
        std::ranges::iota(remaining, 0U);
        taken.resize(body_count);
        std::uint32_t color_count = 0;
        for (std::uint32_t base = 0; !remaining.empty(); base += 64)
        {
            std::ranges::fill(taken, std::uint64_t{0});
            next.clear();
            for (const std::uint32_t index : remaining)
            {
                const auto [first, second] = bodies_of(index);
                const std::uint64_t mask = (first != NONE ? taken[first] : 0) | (second != NONE ? taken[second] : 0);
                if (mask == ~std::uint64_t{0})
                {
                    next.push_back(index);
                    continue;
                }
                const auto bit = static_cast<std::uint32_t>(std::countr_one(mask));
                colors[index] = base + bit;
                color_count = std::max(color_count, base + bit + 1);
                for (const std::uint32_t body : {first, second})
                {
                    if (body != NONE)
                    {
                        taken[body] |= std::uint64_t{1} << bit;
                    }
                }
            }
            remaining.swap(next);
        }

        // The indices color after color, in order within each.
        coloring.starts.assign(color_count + 1, 0);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            ++coloring.starts[colors[index] + 1];
        }
        std::partial_sum(coloring.starts.begin(), coloring.starts.end(), coloring.starts.begin());
        coloring.order.resize(count);
        next.assign(coloring.starts.begin(), coloring.starts.end() - 1);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            coloring.order[next[colors[index]]++] = index;
        }
    }
};

namespace
{

/// Calls @p work on every constraint of @p coloring, color after color, a large color on the scheduler's threads.
template<class Work>
void
for_each_colored(const Coloring& coloring,
                 TaskScheduler* scheduler,
                 Work& work)
{
    const bool parallel = scheduler != nullptr && scheduler->concurrency() > 1;
    for (std::size_t color = 0; color + 1 < coloring.starts.size(); ++color)
    {
        const std::uint32_t begin = coloring.starts[color];
        const std::uint32_t size = coloring.starts[color + 1] - begin;
        if (parallel && size >= PARALLEL_COLOR_SIZE)
        {
            struct Context
            {
                const std::uint32_t* order;
                Work* work;
            };
            const Context context{.order = coloring.order.data() + begin, .work = &work};
            const Context* shared = &context;
            scheduler->parallel_for(size, PARALLEL_RANGE, [shared](std::uint32_t first, std::uint32_t last) {
                for (std::uint32_t index = first; index < last; ++index)
                {
                    (*shared->work)(shared->order[index]);
                }
            });
            continue;
        }
        for (std::uint32_t index = begin; index < begin + size; ++index)
        {
            work(coloring.order[index]);
        }
    }
}

} // namespace

// ===================================================================================================== SequentialImpulseSolver

SequentialImpulseSolver::SequentialImpulseSolver()
    : Solver{PhysicsSubsystem::DYNAMICS}
    , m_state{std::make_unique<State>()}
{
}

SequentialImpulseSolver::~SequentialImpulseSolver() = default;

auto
SequentialImpulseSolver::kind() const noexcept -> SolverKind
{
    return SolverKind::SEQUENTIAL_IMPULSE;
}

auto
SequentialImpulseSolver::is_external() const noexcept -> bool
{
    return true;
}

void
SequentialImpulseSolver::solve(const SolverStep& step)
{
    State& state = *m_state;
    const float seconds = step.seconds;
    const auto body_count = static_cast<std::uint32_t>(step.bodies.size());

    // The step checked.
    if (!(seconds > 0.0F) || !std::isfinite(seconds))
    {
        reject_solve(std::format("the step of {} seconds is not finite and positive", seconds), "step by a positive time");
    }
    for (std::size_t index = 0; index < step.contacts.size(); ++index)
    {
        if (step.contacts[index].first >= body_count || step.contacts[index].second >= body_count)
        {
            reject_solve(std::format("contact {} names body {} or {} of {}", index, step.contacts[index].first, step.contacts[index].second, body_count), "name bodies of the step");
        }
    }
    for (std::size_t index = 0; index < step.joints.size(); ++index)
    {
        const SolverJoint& joint = step.joints[index];
        if (joint.first >= body_count || (joint.second != NONE && joint.second >= body_count))
        {
            reject_solve(std::format("joint {} names body {} or {} of {}", index, joint.first, joint.second, body_count), "name bodies of the step, or NO_SOLVER_BODY for the world");
        }
    }
    const SolverDescription& settings = step.settings;
    const float rate = 1.0F / seconds;

    // ------------------------------------------------------------------------------------------------ prepare
    // Contacts: levers, local points, masses, tangents, the approach speed before the solve, last step's impulses.
    state.contacts.resize(step.contacts.size());
    for (std::uint32_t index = 0; index < step.contacts.size(); ++index)
    {
        const SolverContact& contact = step.contacts[index];
        PreparedContact& prepared = state.contacts[index];
        prepared = {.contact = index, .bodies = state.pair_of(step.bodies, contact.first, contact.second)};
        const SolverBody& first = *prepared.bodies.first;
        const SolverBody& second = *prepared.bodies.second;
        const Vector3 normal = contact.manifold.normal;
        prepared.normal = normal;
        prepared.tangents[0] = normalized(any_perpendicular(normal));
        prepared.tangents[1] = cross(normal, prepared.tangents[0]);
        prepared.local_normal = rotate(conjugate(first.orientation), normal);
        prepared.point_count = std::min(contact.manifold.point_count, MAXIMUM_MANIFOLD_POINTS);
        prepared.static_friction = contact.friction.static_coefficient;
        prepared.dynamic_friction = contact.friction.dynamic_coefficient;
        prepared.rolling = contact.friction.rolling;
        prepared.restitution = contact.restitution;
        prepared.rolling_impulse = settings.warm_starting ? contact.rolling_impulse : Vector3{};
        prepared.first_margin = contact.first_margin;
        prepared.second_margin = contact.second_margin;
        for (std::uint32_t point = 0; point < prepared.point_count; ++point)
        {
            const ContactPoint& source = contact.manifold.points[point];
            PreparedPoint& target = prepared.points[point];
            target.lever_first = source.position - first.center;
            target.lever_second = source.position - second.center;
            target.local_first = rotate(conjugate(first.orientation), source.position - normal * (0.5F * source.separation + contact.first_margin) - first.center);
            target.local_second = rotate(conjugate(second.orientation), source.position + normal * (0.5F * source.separation + contact.second_margin) - second.center);
            target.separation = source.separation;
            const float normal_inverse = contact_inverse_mass(prepared.bodies, target.lever_first, target.lever_second, normal);
            target.normal_mass = normal_inverse > TINY ? 1.0F / normal_inverse : 0.0F;
            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                const float tangent_inverse = contact_inverse_mass(prepared.bodies, target.lever_first, target.lever_second, prepared.tangents[axis]);
                target.tangent_mass[axis] = tangent_inverse > TINY ? 1.0F / tangent_inverse : 0.0F;
            }
            target.relative_velocity = dot(point_velocity(second, target.lever_second) - point_velocity(first, target.lever_first), normal);
            if (settings.warm_starting)
            {
                target.normal_impulse = source.normal_impulse;
                target.tangent_impulse = source.tangent_impulse;
            }
        }
        if (prepared.rolling > 0.0F)
        {
            const Matrix3 zero{.columns = {Vector3{}, Vector3{}, Vector3{}}};
            const Matrix3 inverse_inertia = (prepared.bodies.first_moving != nullptr ? first.inverse_inertia : zero) + (prepared.bodies.second_moving != nullptr ? second.inverse_inertia : zero);
            prepared.rolling_mass = std::abs(determinant(inverse_inertia)) > std::numeric_limits<float>::min() ? inverse(inverse_inertia) : zero;
        }
    }

    // Joints: their rows where the bodies are, effective masses, biases (limits not reached yet, springs), last step's
    // impulses.
    state.joints.resize(step.joints.size());
    for (std::uint32_t index = 0; index < step.joints.size(); ++index)
    {
        const SolverJoint& joint = step.joints[index];
        PreparedJoint& prepared = state.joints[index];
        prepared.joint = index;
        prepared.bodies = state.pair_of(step.bodies, joint.first, joint.second);
        prepared.row_count = rows_of(joint, prepared.bodies, prepared.rows);
        for (std::uint32_t row_index = 0; row_index < prepared.row_count; ++row_index)
        {
            const ConstraintRow& row = prepared.rows[row_index];
            const float inverse = inverse_mass_of(row, prepared.bodies);
            prepared.masses[row_index] = inverse > TINY ? 1.0F / inverse : 0.0F;
            prepared.biases[row_index] = 0.0F;
            prepared.softness[row_index] = 0.0F;
            if (row.kind == ConstraintRowKind::INEQUALITY && row.error > 0.0F)
            {
                prepared.biases[row_index] = row.error * rate;
            }
            else if (row.kind == ConstraintRowKind::SPRING && inverse > TINY)
            {
                // A soft constraint of the spring's frequency and damping ratio (Catto, "Soft constraints").
                const float mass = 1.0F / inverse;
                const float angular_frequency = 2.0F * std::numbers::pi_v<float> * row.spring.frequency;
                const float stiffness = mass * angular_frequency * angular_frequency;
                const float damping = 2.0F * mass * row.spring.damping_ratio * angular_frequency;
                const float softness = seconds * (damping + seconds * stiffness);
                const float gamma = softness > 0.0F ? 1.0F / softness : 0.0F;
                prepared.softness[row_index] = gamma;
                prepared.biases[row_index] = row.error * seconds * stiffness * gamma;
                prepared.masses[row_index] = 1.0F / (inverse + gamma);
            }
            prepared.impulses[row_index] = settings.warm_starting ? joint.impulses[row_index] : 0.0F;
        }
        prepared.block_count = find_blocks(std::span<const ConstraintRow>{prepared.rows}.first(prepared.row_count), prepared.bodies, prepared.blocks);
    }

    // Colors by the bodies each moves.
    const auto moving = [&](std::uint32_t body) { return body != NONE && step.bodies[body].inverse_mass > 0.0F ? body : NONE; };
    state.color(state.contact_colors, static_cast<std::uint32_t>(step.contacts.size()), step.bodies.size(),
                [&](std::uint32_t index) { return std::pair{moving(step.contacts[index].first), moving(step.contacts[index].second)}; });
    state.color(state.joint_colors, static_cast<std::uint32_t>(step.joints.size()), step.bodies.size(),
                [&](std::uint32_t index) { return std::pair{moving(step.joints[index].first), moving(step.joints[index].second)}; });

    // ------------------------------------------------------------------------------------------------ warm starting
    auto warm_joint = [&](std::uint32_t index) {
        PreparedJoint& joint = state.joints[index];
        for (std::uint32_t row = 0; row < joint.row_count; ++row)
        {
            apply_row(joint.rows[row], joint.bodies, joint.impulses[row]);
        }
    };
    auto warm_contact = [&](std::uint32_t index) {
        PreparedContact& contact = state.contacts[index];
        for (std::uint32_t point = 0; point < contact.point_count; ++point)
        {
            const PreparedPoint& prepared = contact.points[point];
            const Vector3 impulse = contact.normal * prepared.normal_impulse + contact.tangents[0] * prepared.tangent_impulse[0] + contact.tangents[1] * prepared.tangent_impulse[1];
            apply_impulse(contact.bodies, prepared.lever_first, prepared.lever_second, impulse);
        }
        if (SolverBody* body = contact.bodies.first_moving)
        {
            body->angular_velocity -= body->inverse_inertia * contact.rolling_impulse;
        }
        if (SolverBody* body = contact.bodies.second_moving)
        {
            body->angular_velocity += body->inverse_inertia * contact.rolling_impulse;
        }
    };
    if (settings.warm_starting)
    {
        for_each_colored(state.joint_colors, step.scheduler, warm_joint);
        for_each_colored(state.contact_colors, step.scheduler, warm_contact);
    }

    // ------------------------------------------------------------------------------------------------ velocities
    auto solve_joint = [&](std::uint32_t index) {
        PreparedJoint& joint = state.joints[index];
        std::uint32_t block_index = 0;
        for (std::uint32_t row_index = 0; row_index < joint.row_count; ++row_index)
        {
            if (block_index < joint.block_count && joint.blocks[block_index].start == row_index)
            {
                // A block of equalities: their rates to 0 together.
                const RowBlock& block = joint.blocks[block_index++];
                std::array<float, MAXIMUM_BLOCK> impulses{};
                for (std::uint32_t inner = 0; inner < block.size; ++inner)
                {
                    impulses[inner] = -rate_of(joint.rows[block.start + inner], joint.bodies);
                }
                solve_block(block.factor, block.size, impulses);
                for (std::uint32_t inner = 0; inner < block.size; ++inner)
                {
                    joint.impulses[block.start + inner] += impulses[inner];
                    apply_row(joint.rows[block.start + inner], joint.bodies, impulses[inner]);
                }
                row_index += block.size - 1;
                continue;
            }
            const ConstraintRow& row = joint.rows[row_index];
            float& accumulated = joint.impulses[row_index];
            const float rate_now = rate_of(row, joint.bodies);
            float impulse = 0.0F;
            switch (row.kind)
            {
                case ConstraintRowKind::EQUALITY:
                    impulse = -joint.masses[row_index] * rate_now;
                    accumulated += impulse;
                    break;
                case ConstraintRowKind::SPRING:
                    impulse = -joint.masses[row_index] * (rate_now + joint.biases[row_index] + joint.softness[row_index] * accumulated);
                    accumulated += impulse;
                    break;
                case ConstraintRowKind::INEQUALITY:
                {
                    const float total = std::max(accumulated - joint.masses[row_index] * (rate_now + joint.biases[row_index]), 0.0F);
                    impulse = total - accumulated;
                    accumulated = total;
                    break;
                }
                case ConstraintRowKind::MOTOR:
                {
                    const float limit = row.maximum_force * seconds;
                    const float total = std::clamp(accumulated - joint.masses[row_index] * (rate_now - row.speed), -limit, limit);
                    impulse = total - accumulated;
                    accumulated = total;
                    break;
                }
            }
            apply_row(row, joint.bodies, impulse);
        }
    };
    auto solve_contact = [&](std::uint32_t index) {
        PreparedContact& contact = state.contacts[index];
        const SolverBody& first = *contact.bodies.first;
        const SolverBody& second = *contact.bodies.second;

        // Coulomb friction at each point: the impulse that stops the sliding while it fits the static cone (sticking),
        // else as much as the dynamic coefficient allows (sliding).
        float total_normal = 0.0F;
        for (std::uint32_t point = 0; point < contact.point_count; ++point)
        {
            PreparedPoint& prepared = contact.points[point];
            const Vector3 relative = point_velocity(second, prepared.lever_second) - point_velocity(first, prepared.lever_first);
            std::array<float, 2> totals{};
            for (std::size_t axis = 0; axis < 2; ++axis)
            {
                totals[axis] = prepared.tangent_impulse[axis] - prepared.tangent_mass[axis] * dot(relative, contact.tangents[axis]);
            }
            const float size = std::sqrt(totals[0] * totals[0] + totals[1] * totals[1]);
            const float limit = contact.dynamic_friction * prepared.normal_impulse;
            if (size > contact.static_friction * prepared.normal_impulse && size > 0.0F)
            {
                totals[0] *= limit / size;
                totals[1] *= limit / size;
            }
            const Vector3 impulse = contact.tangents[0] * (totals[0] - prepared.tangent_impulse[0]) + contact.tangents[1] * (totals[1] - prepared.tangent_impulse[1]);
            prepared.tangent_impulse = totals;
            apply_impulse(contact.bodies, prepared.lever_first, prepared.lever_second, impulse);
            total_normal += prepared.normal_impulse;
        }

        // Rolling resistance: the relative turn opposed, up to the coefficient (a lever, in meters) times the push.
        if (contact.rolling > 0.0F)
        {
            const Vector3 turn = second.angular_velocity - first.angular_velocity;
            Vector3 total = contact.rolling_impulse - contact.rolling_mass * turn;
            const float limit = contact.rolling * total_normal;
            const float size = length(total);
            if (size > limit && size > 0.0F)
            {
                total = total * (limit / size);
            }
            const Vector3 impulse = total - contact.rolling_impulse;
            contact.rolling_impulse = total;
            if (SolverBody* body = contact.bodies.first_moving)
            {
                body->angular_velocity -= body->inverse_inertia * impulse;
            }
            if (SolverBody* body = contact.bodies.second_moving)
            {
                body->angular_velocity += body->inverse_inertia * impulse;
            }
        }

        // The normal impulses: apart points let the bodies approach until they touch (speculative contacts).
        for (std::uint32_t point = 0; point < contact.point_count; ++point)
        {
            PreparedPoint& prepared = contact.points[point];
            const float approach = dot(point_velocity(second, prepared.lever_second) - point_velocity(first, prepared.lever_first), contact.normal);
            const float bias = prepared.separation > 0.0F ? prepared.separation * rate : 0.0F;
            const float total = std::max(prepared.normal_impulse - prepared.normal_mass * (approach + bias), 0.0F);
            apply_impulse(contact.bodies, prepared.lever_first, prepared.lever_second, contact.normal * (total - prepared.normal_impulse));
            prepared.normal_impulse = total;
            prepared.largest_impulse = std::max(prepared.largest_impulse, total);
        }
    };
    for (std::uint32_t iteration = 0; iteration < std::max(1U, settings.velocity_iterations); ++iteration)
    {
        for_each_colored(state.joint_colors, step.scheduler, solve_joint);
        for_each_colored(state.contact_colors, step.scheduler, solve_contact);
    }

    // Restitution: the points that hit faster than the threshold, and pushed, bounce.
    auto restitute = [&](std::uint32_t index) {
        PreparedContact& contact = state.contacts[index];
        if (!(contact.restitution > 0.0F))
        {
            return;
        }
        for (std::uint32_t point = 0; point < contact.point_count; ++point)
        {
            PreparedPoint& prepared = contact.points[point];
            if (prepared.relative_velocity > -settings.restitution_threshold || !(prepared.largest_impulse > 0.0F))
            {
                continue;
            }
            const float approach = dot(point_velocity(*contact.bodies.second, prepared.lever_second) - point_velocity(*contact.bodies.first, prepared.lever_first), contact.normal);
            const float total = std::max(prepared.normal_impulse - prepared.normal_mass * (approach + contact.restitution * prepared.relative_velocity), 0.0F);
            apply_impulse(contact.bodies, prepared.lever_first, prepared.lever_second, contact.normal * (total - prepared.normal_impulse));
            prepared.normal_impulse = total;
        }
    };
    for_each_colored(state.contact_colors, step.scheduler, restitute);

    // What the contacts and joints accumulated; the joints' forces and torques.
    for (const PreparedContact& contact : state.contacts)
    {
        SolverContact& target = step.contacts[contact.contact];
        for (std::uint32_t point = 0; point < contact.point_count; ++point)
        {
            target.manifold.points[point].normal_impulse = contact.points[point].normal_impulse;
            target.manifold.points[point].tangent_impulse = contact.points[point].tangent_impulse;
        }
        target.rolling_impulse = contact.rolling_impulse;
    }
    for (const PreparedJoint& joint : state.joints)
    {
        // On the second body; for rows that move only the first (a user constraint to the world), the reaction.
        SolverJoint& target = step.joints[joint.joint];
        target.impulses = joint.impulses;
        Vector3 force_first{};
        Vector3 torque_first{};
        target.force = {};
        target.torque = {};
        for (std::uint32_t row = 0; row < joint.row_count; ++row)
        {
            target.force += joint.rows[row].linear_second * (joint.impulses[row] * rate);
            target.torque += joint.rows[row].angular_second * (joint.impulses[row] * rate);
            force_first += joint.rows[row].linear_first * (joint.impulses[row] * rate);
            torque_first += joint.rows[row].angular_first * (joint.impulses[row] * rate);
        }
        if (target.force == Vector3{} && target.torque == Vector3{})
        {
            target.force = -force_first;
            target.torque = -torque_first;
        }
    }

    // ------------------------------------------------------------------------------------------------ positions
    // Every body moved by its velocities (kinematic bodies too).
    for (SolverBody& body : step.bodies)
    {
        if (body.linear_velocity != Vector3{} || body.angular_velocity != Vector3{})
        {
            body.center += body.linear_velocity * seconds;
            body.orientation = turned(body.orientation, body.angular_velocity * seconds);
        }
    }

    // Position iterations (non-linear Gauss–Seidel): the joints' errors where the bodies now are, then the overlaps.
    auto correct_joint = [&](std::uint32_t index) {
        PreparedJoint& joint = state.joints[index];
        std::array<ConstraintRow, MAXIMUM_CONSTRAINT_ROWS> rows{};
        const std::uint32_t count = rows_of(step.joints[joint.joint], joint.bodies, rows);
        std::array<RowBlock, MAXIMUM_BLOCKS> blocks{};
        const std::uint32_t block_count = find_blocks(std::span<const ConstraintRow>{rows}.first(count), joint.bodies, blocks);
        std::uint32_t block_index = 0;
        for (std::uint32_t row_index = 0; row_index < count; ++row_index)
        {
            if (block_index < block_count && blocks[block_index].start == row_index)
            {
                // A block of equalities: their errors (each clamped) corrected together.
                const RowBlock& block = blocks[block_index++];
                std::array<float, MAXIMUM_BLOCK> impulses{};
                for (std::uint32_t inner = 0; inner < block.size; ++inner)
                {
                    const ConstraintRow& row = rows[block.start + inner];
                    const float largest = is_angular(row) ? MAXIMUM_ANGULAR_CORRECTION : MAXIMUM_LINEAR_CORRECTION;
                    impulses[inner] = -POSITION_CORRECTION_FACTOR * std::clamp(row.error, -largest, largest);
                }
                solve_block(block.factor, block.size, impulses);
                for (std::uint32_t inner = 0; inner < block.size; ++inner)
                {
                    move_row(rows[block.start + inner], joint.bodies, impulses[inner]);
                }
                row_index += block.size - 1;
                continue;
            }
            const ConstraintRow& row = rows[row_index];
            const bool angular = is_angular(row);
            const float largest = angular ? MAXIMUM_ANGULAR_CORRECTION : MAXIMUM_LINEAR_CORRECTION;
            float correction = 0.0F;
            if (row.kind == ConstraintRowKind::EQUALITY)
            {
                correction = POSITION_CORRECTION_FACTOR * std::clamp(row.error, -largest, largest);
            }
            else if (row.kind == ConstraintRowKind::INEQUALITY && row.error < 0.0F)
            {
                correction = std::clamp(POSITION_CORRECTION_FACTOR * (row.error + (angular ? ANGULAR_SLOP : LINEAR_SLOP)), -largest, 0.0F);
            }
            const float inverse = inverse_mass_of(row, joint.bodies);
            if (correction != 0.0F && inverse > TINY)
            {
                move_row(row, joint.bodies, -correction / inverse);
            }
        }
    };
    auto correct_contact = [&](std::uint32_t index) {
        PreparedContact& contact = state.contacts[index];
        for (std::uint32_t point = 0; point < contact.point_count; ++point)
        {
            // The separation where the bodies are now (the normal turns with the first body).
            const SolverBody& first = *contact.bodies.first;
            const SolverBody& second = *contact.bodies.second;
            const PreparedPoint& prepared = contact.points[point];
            const Vector3 normal = rotate(first.orientation, contact.local_normal);
            const Vector3 on_first = first.center + rotate(first.orientation, prepared.local_first) + normal * contact.first_margin;
            const Vector3 on_second = second.center + rotate(second.orientation, prepared.local_second) - normal * contact.second_margin;
            const float separation = dot(on_second - on_first, normal);
            const float correction = std::clamp(POSITION_CORRECTION_FACTOR * (separation + LINEAR_SLOP), -MAXIMUM_LINEAR_CORRECTION, 0.0F);
            if (correction == 0.0F)
            {
                continue;
            }
            const Vector3 middle = (on_first + on_second) * 0.5F;
            const Vector3 lever_first = middle - first.center;
            const Vector3 lever_second = middle - second.center;
            const float inverse = contact_inverse_mass(contact.bodies, lever_first, lever_second, normal);
            if (!(inverse > TINY))
            {
                continue;
            }
            const Vector3 impulse = normal * (-correction / inverse);
            if (SolverBody* body = contact.bodies.first_moving)
            {
                body->center -= component_multiply(impulse, body->linear_freedom) * body->inverse_mass;
                body->orientation = turned(body->orientation, body->inverse_inertia * -cross(lever_first, impulse));
            }
            if (SolverBody* body = contact.bodies.second_moving)
            {
                body->center += component_multiply(impulse, body->linear_freedom) * body->inverse_mass;
                body->orientation = turned(body->orientation, body->inverse_inertia * cross(lever_second, impulse));
            }
        }
    };
    for (std::uint32_t iteration = 0; iteration < settings.position_iterations; ++iteration)
    {
        for_each_colored(state.joint_colors, step.scheduler, correct_joint);
        for_each_colored(state.contact_colors, step.scheduler, correct_contact);
    }
}

} // namespace gfx::physics

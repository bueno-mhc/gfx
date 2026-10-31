#version 460
// Lit solids: a directional light and a sky/ground ambient term; the floor's grid (instance flag bit 0) every meter,
// stronger every five, antialiased by the screen-space derivatives of the position; a highlight (flag bit 1). The
// wireframe (push flag bit 1) draws the edges in one dark color, except the floor's.

layout(push_constant) uniform Push
{
    mat4 view_projection;
    vec4 light;
    uint vertex_buffer;
    uint vertex_word;
    uint instance_buffer;
    uint instance_word;
    uint flags;
} push;

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) flat in vec4 color;
layout(location = 3) flat in uint instance_flags;

layout(location = 0) out vec4 result;

// How much of a grid line of the given spacing covers this pixel (0 to 1).
float grid_line(vec2 coordinates, float spacing, float width)
{
    const vec2 cell = coordinates / spacing;
    const vec2 distance_to_line = abs(fract(cell - 0.5) - 0.5) / max(fwidth(cell), vec2(1.0e-6));
    return 1.0 - clamp(min(distance_to_line.x, distance_to_line.y) - width, 0.0, 1.0);
}

void main()
{
    if ((push.flags & 2u) != 0u)
    {
        // The floor (with the grid) has no wireframe: its long diagonal would cross the scene.
        if ((instance_flags & 1u) != 0u)
        {
            discard;
        }
        result = vec4(0.02, 0.022, 0.03, 1.0);
        return;
    }

    // Back faces (two-sided meshes) are lit from their own side.
    vec3 normal = normalize(world_normal);
    if (!gl_FrontFacing)
    {
        normal = -normal;
    }

    vec3 base = color.rgb;
    if ((instance_flags & 1u) != 0u && normal.y > 0.5)
    {
        const float minor = grid_line(world_position.xz, 1.0, 0.0);
        const float major = grid_line(world_position.xz, 5.0, 0.5);
        base *= 1.0 - 0.25 * max(minor, major * 1.6);
    }
    if ((instance_flags & 2u) != 0u)
    {
        base = mix(base, vec3(1.0, 0.85, 0.3), 0.35);
    }

    const float diffuse = max(dot(normal, push.light.xyz), 0.0) * push.light.w;
    const vec3 sky = vec3(0.30, 0.34, 0.40);
    const vec3 ground = vec3(0.12, 0.11, 0.10);
    const vec3 ambient = mix(ground, sky, 0.5 + 0.5 * normal.y);
    result = vec4(base * (ambient + diffuse * vec3(1.0, 0.96, 0.9)), 1.0);
}

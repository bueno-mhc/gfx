"""Writes the test meshes of tools/tetrahedralize.py into a directory: a torus (OBJ, quads: not convex, a hole through
it), a sphere (binary STL, a triangle soup: every vertex repeated), a box (ASCII PLY) and a two-box union (OFF, the boxes
overlapping: their surfaces cross, the ray vote decides). Only the Python standard library.

    python tests/physics/meshes/make_test_meshes.py <directory>
"""
import math
import pathlib
import struct
import sys


def torus(major: float, minor: float, rings: int, sides: int) -> tuple[list, list]:
    vertices = []
    for ring in range(rings):
        theta = 2.0 * math.pi * ring / rings
        for side in range(sides):
            phi = 2.0 * math.pi * side / sides
            radius = major + minor * math.cos(phi)
            vertices.append((radius * math.cos(theta), minor * math.sin(phi), radius * math.sin(theta)))
    quads = []
    for ring in range(rings):
        for side in range(sides):
            a = ring * sides + side
            b = ((ring + 1) % rings) * sides + side
            c = ((ring + 1) % rings) * sides + (side + 1) % sides
            d = ring * sides + (side + 1) % sides
            quads.append((a, d, c, b))
    return vertices, quads


def sphere(radius: float, stacks: int, slices: int) -> list:
    """Triangles (each with its own three corners: an STL soup)."""
    def point(stack: int, slice_: int):
        theta = math.pi * stack / stacks
        phi = 2.0 * math.pi * slice_ / slices
        return (radius * math.sin(theta) * math.cos(phi), radius * math.cos(theta), radius * math.sin(theta) * math.sin(phi))
    triangles = []
    for stack in range(stacks):
        for slice_ in range(slices):
            a, b = point(stack, slice_), point(stack + 1, slice_)
            c, d = point(stack + 1, slice_ + 1), point(stack, slice_ + 1)
            if stack != 0:
                triangles.append((a, d, b))
            if stack != stacks - 1:
                triangles.append((b, d, c))
    return triangles


def box(center, half) -> tuple[list, list]:
    vertices = [(center[0] + sx * half[0], center[1] + sy * half[1], center[2] + sz * half[2]) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return vertices, faces


def main() -> int:
    directory = pathlib.Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)

    vertices, quads = torus(1.0, 0.35, 32, 16)
    lines = [f"v {x!r} {y!r} {z!r}" for x, y, z in vertices] + [f"f {a + 1} {b + 1} {c + 1} {d + 1}" for a, b, c, d in quads]
    (directory / "torus.obj").write_text("# a torus (quads)\n" + "\n".join(lines) + "\n", encoding="utf-8")

    triangles = sphere(0.5, 12, 24)
    data = bytearray(b"gfx test sphere".ljust(80, b" ")) + struct.pack("<I", len(triangles))
    for a, b, c in triangles:
        data += struct.pack("<12fH", 0.0, 0.0, 0.0, *a, *b, *c, 0)
    (directory / "sphere.stl").write_bytes(bytes(data))

    vertices, faces = box((0.0, 0.0, 0.0), (0.6, 0.3, 0.2))
    header = ["ply", "format ascii 1.0", f"element vertex {len(vertices)}", "property float x", "property float y", "property float z",
              f"element face {len(faces)}", "property list uchar int vertex_indices", "end_header"]
    body = [f"{x!r} {y!r} {z!r}" for x, y, z in vertices] + [f"4 {a} {b} {c} {d}" for a, b, c, d in faces]
    (directory / "box.ply").write_text("\n".join(header + body) + "\n", encoding="ascii")

    first, first_faces = box((0.0, 0.0, 0.0), (0.4, 0.4, 0.4))
    second, second_faces = box((0.5, 0.2, 0.0), (0.4, 0.2, 0.3))
    vertices = first + second
    faces = first_faces + [tuple(index + len(first) for index in face) for face in second_faces]
    lines = ["OFF", f"{len(vertices)} {len(faces)} 0"] + [f"{x!r} {y!r} {z!r}" for x, y, z in vertices] + [f"4 {a} {b} {c} {d}" for a, b, c, d in faces]
    (directory / "two_boxes.off").write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"wrote torus.obj, sphere.stl, box.ply, two_boxes.off into {directory}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

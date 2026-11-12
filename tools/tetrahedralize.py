"""Tetrahedralizes a closed triangle mesh for XPBD soft bodies (gfx.physics: TetrahedralMesh, volume_preset()).

Reads OBJ, STL (ASCII or binary), PLY (ASCII or binary little-endian) and OFF; welds coincident vertices and drops degenerate
and repeated triangles; samples points on the surface (its vertices, thinned to the spacing) and on a grid inside it; joins
them by a Delaunay tetrahedralization (Bowyer-Watson, or scipy.spatial.Delaunay when SciPy is installed); keeps the
tetrahedra whose centers are inside the surface (a vote of rays along the three axes, which tolerates small holes and
repeated faces); removes slivers from the boundary (a tetrahedron flatter than --minimum-quality with a face on the boundary:
inner ones stay, so no hole opens); orients every tetrahedron positively and drops unused vertices. Writes:

    TetGen          <output>.node and <output>.ele (+ <output>.face, the boundary): gfx::physics::read_tetgen_mesh() reads
                    the first two (any TetGen-made mesh too)
    JSON            {"positions": [[x, y, z], ...], "tetrahedra": [[a, b, c, d], ...], "surface": [[a, b, c], ...],
                     "edges": [[a, b], ...], "statistics": {...}}
    C++ module      export module <name>; constexpr arrays of the positions (x, y, z) and the tetrahedra (four indices each)

    python tools/tetrahedralize.py mesh.obj --output out/mesh [--format tetgen|json|cpp] [--resolution 12]
        [--surface-spacing 0.5] [--minimum-quality 0.02] [--scale 1] [--module-name meshes.mesh] [--seed 1] [--check]

--resolution: interior grid cells along the mesh's longest side (more: finer tetrahedra); --surface-spacing: the surface
vertices kept, thinned to that fraction of the grid's spacing (0: every vertex). --check validates what was written (every
volume positive, every vertex used, the boundary closed, the volume within 10 % of the surface's) and prints statistics:
the exit code is 1 when a check fails. Only the Python standard library is required.
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import random
import struct
import sys
from dataclasses import dataclass, field

Vector = tuple[float, float, float]


# ===================================================================================================== reading meshes

@dataclass
class SurfaceMesh:
    vertices: list[Vector] = field(default_factory=list)
    triangles: list[tuple[int, int, int]] = field(default_factory=list)


def read_obj(data: bytes) -> SurfaceMesh:
    mesh = SurfaceMesh()
    for raw in data.decode("utf-8", errors="replace").splitlines():
        parts = raw.split()
        if not parts:
            continue
        if parts[0] == "v" and len(parts) >= 4:
            mesh.vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
        elif parts[0] == "f" and len(parts) >= 4:
            indices = []
            for token in parts[1:]:
                index = int(token.split("/")[0])
                indices.append(index - 1 if index > 0 else len(mesh.vertices) + index)
            for corner in range(1, len(indices) - 1):  # a polygon as a fan
                mesh.triangles.append((indices[0], indices[corner], indices[corner + 1]))
    return mesh


def read_stl(data: bytes) -> SurfaceMesh:
    mesh = SurfaceMesh()
    is_binary = len(data) >= 84 and 84 + 50 * struct.unpack_from("<I", data, 80)[0] == len(data)
    if is_binary:
        count = struct.unpack_from("<I", data, 80)[0]
        for triangle in range(count):
            values = struct.unpack_from("<12f", data, 84 + 50 * triangle)
            first = len(mesh.vertices)
            mesh.vertices.extend([tuple(values[3:6]), tuple(values[6:9]), tuple(values[9:12])])
            mesh.triangles.append((first, first + 1, first + 2))
        return mesh
    corners: list[Vector] = []
    for raw in data.decode("utf-8", errors="replace").splitlines():
        parts = raw.split()
        if len(parts) == 4 and parts[0] == "vertex":
            corners.append((float(parts[1]), float(parts[2]), float(parts[3])))
            if len(corners) == 3:
                first = len(mesh.vertices)
                mesh.vertices.extend(corners)
                mesh.triangles.append((first, first + 1, first + 2))
                corners = []
    return mesh


def read_ply(data: bytes) -> SurfaceMesh:
    header_end = data.find(b"end_header")
    if header_end < 0:
        raise ValueError("a PLY file without end_header")
    body_start = data.index(b"\n", header_end) + 1
    header = data[:header_end].decode("ascii", errors="replace").splitlines()
    form = "ascii"
    elements: list[tuple[str, int, list[tuple[str, ...]]]] = []
    for line in header:
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "format":
            form = parts[1]
        elif parts[0] == "element":
            elements.append((parts[1], int(parts[2]), []))
        elif parts[0] == "property" and elements:
            elements[-1][2].append(tuple(parts[1:]))
    if form not in ("ascii", "binary_little_endian"):
        raise ValueError(f"PLY format {form} is not read (ascii and binary_little_endian are)")
    sizes = {"char": "b", "int8": "b", "uchar": "B", "uint8": "B", "short": "h", "int16": "h", "ushort": "H", "uint16": "H",
             "int": "i", "int32": "i", "uint": "I", "uint32": "I", "float": "f", "float32": "f", "double": "d", "float64": "d"}
    mesh = SurfaceMesh()
    if form == "ascii":
        tokens = data[body_start:].decode("ascii", errors="replace").split()
        position = 0

        def next_value(kind: str) -> float:
            nonlocal position
            value = tokens[position]
            position += 1
            return float(value) if sizes[kind] in "fd" else int(value)
    else:
        offset = body_start

        def next_value(kind: str) -> float:
            nonlocal offset
            code = "<" + sizes[kind]
            value = struct.unpack_from(code, data, offset)[0]
            offset += struct.calcsize(code)
            return value
    for name, count, properties in elements:
        for _ in range(count):
            values: dict[str, object] = {}
            for prop in properties:
                if prop[0] == "list":
                    length = int(next_value(prop[1]))
                    values[prop[3]] = [int(next_value(prop[2])) for _ in range(length)]
                else:
                    values[prop[1]] = next_value(prop[0])
            if name == "vertex":
                mesh.vertices.append((float(values["x"]), float(values["y"]), float(values["z"])))
            elif name == "face":
                indices = values.get("vertex_indices", values.get("vertex_index", []))
                for corner in range(1, len(indices) - 1):
                    mesh.triangles.append((indices[0], indices[corner], indices[corner + 1]))
    return mesh


def read_off(data: bytes) -> SurfaceMesh:
    tokens = [line.split("#")[0] for line in data.decode("utf-8", errors="replace").splitlines()]
    words = " ".join(tokens).split()
    if not words or not words[0].endswith("OFF"):
        raise ValueError("an OFF file without its OFF header")
    vertex_count, face_count = int(words[1]), int(words[2])
    position = 4
    mesh = SurfaceMesh()
    for _ in range(vertex_count):
        mesh.vertices.append((float(words[position]), float(words[position + 1]), float(words[position + 2])))
        position += 3
    for _ in range(face_count):
        length = int(words[position])
        indices = [int(word) for word in words[position + 1:position + 1 + length]]
        position += 1 + length
        for corner in range(1, len(indices) - 1):
            mesh.triangles.append((indices[0], indices[corner], indices[corner + 1]))
    return mesh


def read_mesh(path: pathlib.Path) -> SurfaceMesh:
    readers = {".obj": read_obj, ".stl": read_stl, ".ply": read_ply, ".off": read_off}
    reader = readers.get(path.suffix.lower())
    if reader is None:
        raise ValueError(f"{path.suffix} files are not read (OBJ, STL, PLY and OFF are)")
    return reader(path.read_bytes())


# ===================================================================================================== vector helpers

def sub(a: Vector, b: Vector) -> Vector:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def dot(a: Vector, b: Vector) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vector, b: Vector) -> Vector:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def signed_volume(a: Vector, b: Vector, c: Vector, d: Vector) -> float:
    return dot(sub(b, a), cross(sub(c, a), sub(d, a))) / 6.0


def quality(a: Vector, b: Vector, c: Vector, d: Vector) -> float:
    """1 for a regular tetrahedron, 0 for a flat one: 6√2 · volume / (root-mean-square edge)³."""
    edges = [sub(b, a), sub(c, a), sub(d, a), sub(c, b), sub(d, b), sub(d, c)]
    rms = math.sqrt(sum(dot(edge, edge) for edge in edges) / 6.0)
    if rms == 0.0:
        return 0.0
    return 6.0 * math.sqrt(2.0) * abs(signed_volume(a, b, c, d)) / (rms ** 3)


# ===================================================================================================== cleaning

def clean(mesh: SurfaceMesh) -> SurfaceMesh:
    """Coincident vertices welded (within a millionth of the bounding box's diagonal); degenerate and repeated triangles
    dropped; unused vertices dropped."""
    if not mesh.vertices or not mesh.triangles:
        raise ValueError("the mesh has no triangle")
    low = [min(vertex[axis] for vertex in mesh.vertices) for axis in range(3)]
    high = [max(vertex[axis] for vertex in mesh.vertices) for axis in range(3)]
    diagonal = math.dist(low, high) or 1.0
    tolerance = diagonal * 1e-6
    welded: dict[tuple[int, int, int], int] = {}
    remap: list[int] = []
    vertices: list[Vector] = []
    for vertex in mesh.vertices:
        key = tuple(round(coordinate / tolerance) for coordinate in vertex)
        if key not in welded:
            welded[key] = len(vertices)
            vertices.append(vertex)
        remap.append(welded[key])
    triangles: list[tuple[int, int, int]] = []
    seen: set[tuple[int, int, int]] = set()
    for triangle in mesh.triangles:
        a, b, c = (remap[index] for index in triangle)
        if a == b or b == c or a == c:
            continue
        area = math.sqrt(dot(*(2 * [cross(sub(vertices[b], vertices[a]), sub(vertices[c], vertices[a]))])))
        if area <= tolerance * tolerance:
            continue
        key = tuple(sorted((a, b, c)))
        if key in seen:
            continue
        seen.add(key)
        triangles.append((a, b, c))
    used = sorted({index for triangle in triangles for index in triangle})
    compact = {old: new for new, old in enumerate(used)}
    return SurfaceMesh([vertices[index] for index in used], [tuple(compact[index] for index in triangle) for triangle in triangles])


def enclosed_volume(mesh: SurfaceMesh) -> float:
    """The volume the surface encloses (divergence theorem; exact for a closed, consistently oriented surface)."""
    total = 0.0
    for a, b, c in mesh.triangles:
        total += dot(mesh.vertices[a], cross(mesh.vertices[b], mesh.vertices[c])) / 6.0
    return abs(total)


# ===================================================================================================== inside test

class RayGrid:
    """The triangles projected along one axis into a 2D grid of buckets: rays along that axis meet only their bucket's."""

    def __init__(self, mesh: SurfaceMesh, axis: int, buckets: int):
        self.mesh = mesh
        self.axis = axis
        self.u, self.v = [(1, 2), (2, 0), (0, 1)][axis]
        coordinates_u = [vertex[self.u] for vertex in mesh.vertices]
        coordinates_v = [vertex[self.v] for vertex in mesh.vertices]
        self.low_u, self.low_v = min(coordinates_u), min(coordinates_v)
        span = max(max(coordinates_u) - self.low_u, max(coordinates_v) - self.low_v) or 1.0
        self.cell = span / buckets
        self.buckets: dict[tuple[int, int], list[int]] = {}
        for index, (a, b, c) in enumerate(mesh.triangles):
            corners = [mesh.vertices[a], mesh.vertices[b], mesh.vertices[c]]
            u_low = int((min(corner[self.u] for corner in corners) - self.low_u) // self.cell)
            u_high = int((max(corner[self.u] for corner in corners) - self.low_u) // self.cell)
            v_low = int((min(corner[self.v] for corner in corners) - self.low_v) // self.cell)
            v_high = int((max(corner[self.v] for corner in corners) - self.low_v) // self.cell)
            for i in range(u_low, u_high + 1):
                for j in range(v_low, v_high + 1):
                    self.buckets.setdefault((i, j), []).append(index)

    def crossings(self, point: Vector) -> tuple[int, int]:
        """Triangles the ray from point toward +axis passes through (the point nudged off edges and vertices): how many,
        and their sum signed by which way each faces (+1 leaving, -1 entering an outward-wound surface: a winding number)."""
        pu = point[self.u] + self.cell * 1.234567e-5
        pv = point[self.v] + self.cell * 2.345679e-5
        key = (int((pu - self.low_u) // self.cell), int((pv - self.low_v) // self.cell))
        count = 0
        winding = 0
        for index in self.buckets.get(key, ()):
            a, b, c = (self.mesh.vertices[corner] for corner in self.mesh.triangles[index])
            # Barycentric coordinates of (pu, pv) in the projected triangle.
            x0, y0 = a[self.u], a[self.v]
            x1, y1 = b[self.u], b[self.v]
            x2, y2 = c[self.u], c[self.v]
            denominator = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if denominator == 0.0:
                continue
            first = ((y1 - y2) * (pu - x2) + (x2 - x1) * (pv - y2)) / denominator
            second = ((y2 - y0) * (pu - x2) + (x0 - x2) * (pv - y2)) / denominator
            third = 1.0 - first - second
            if first < 0.0 or second < 0.0 or third < 0.0:
                continue
            height = first * a[self.axis] + second * b[self.axis] + third * c[self.axis]
            if height > point[self.axis]:
                count += 1
                winding += 1 if denominator > 0.0 else -1
        return count, winding


class InsideTest:
    """Inside when rays along at least two of the three axes say so: an odd number of crossings (any winding), or a
    winding number other than 0 (parts overlapping: two surfaces crossed, still inside)."""

    def __init__(self, mesh: SurfaceMesh):
        buckets = max(8, int(math.sqrt(len(mesh.triangles))))
        self.grids = [RayGrid(mesh, axis, buckets) for axis in range(3)]

    def __call__(self, point: Vector) -> bool:
        votes = 0
        for grid in self.grids:
            count, winding = grid.crossings(point)
            votes += 1 if count % 2 == 1 or winding != 0 else 0
        return votes >= 2


# ===================================================================================================== sampling

def sample_points(mesh: SurfaceMesh, inside: InsideTest, resolution: int, surface_spacing: float, seed: int) -> tuple[list[Vector], float]:
    low = [min(vertex[axis] for vertex in mesh.vertices) for axis in range(3)]
    high = [max(vertex[axis] for vertex in mesh.vertices) for axis in range(3)]
    spacing = max(high[axis] - low[axis] for axis in range(3)) / max(resolution, 1)

    # The surface: its vertices, one per cell of surface_spacing × spacing.
    points: list[Vector] = []
    if surface_spacing > 0.0:
        cell = surface_spacing * spacing
        kept: set[tuple[int, int, int]] = set()
        for vertex in mesh.vertices:
            key = tuple(int(math.floor(coordinate / cell)) for coordinate in vertex)
            if key not in kept:
                kept.add(key)
                points.append(vertex)
    else:
        points.extend(mesh.vertices)

    # The inside: grid points (jittered: a Delaunay tetrahedralization of a regular grid is degenerate) at least half the
    # spacing from the surface's points.
    generator = random.Random(seed)
    near: dict[tuple[int, int, int], list[Vector]] = {}
    for point in points:
        near.setdefault(tuple(int(math.floor(coordinate / spacing)) for coordinate in point), []).append(point)

    def far_from_surface(point: Vector) -> bool:
        key = tuple(int(math.floor(coordinate / spacing)) for coordinate in point)
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                for dk in (-1, 0, 1):
                    for other in near.get((key[0] + di, key[1] + dj, key[2] + dk), ()):
                        if math.dist(point, other) < 0.5 * spacing:
                            return False
        return True

    counts = [int(math.ceil((high[axis] - low[axis]) / spacing)) + 1 for axis in range(3)]
    for i in range(counts[0]):
        for j in range(counts[1]):
            for k in range(counts[2]):
                point = tuple(low[axis] + spacing * (index + 0.5) + spacing * 0.05 * (generator.random() - 0.5)
                              for axis, index in zip(range(3), (i, j, k)))
                if inside(point) and far_from_surface(point):
                    points.append(point)
    return points, spacing


# ===================================================================================================== Delaunay

def circumsphere(a: Vector, b: Vector, c: Vector, d: Vector) -> tuple[Vector, float] | None:
    ba, ca, da = sub(b, a), sub(c, a), sub(d, a)
    determinant = 2.0 * dot(ba, cross(ca, da))
    if abs(determinant) < 1e-30:
        return None
    lb, lc, ld = dot(ba, ba), dot(ca, ca), dot(da, da)
    term = (cross(ca, da), cross(da, ba), cross(ba, ca))
    offset = tuple((lb * term[0][axis] + lc * term[1][axis] + ld * term[2][axis]) / determinant for axis in range(3))
    return (a[0] + offset[0], a[1] + offset[1], a[2] + offset[2]), dot(offset, offset)


def delaunay_bowyer_watson(points: list[Vector]) -> list[tuple[int, int, int, int]]:
    """The Delaunay tetrahedra of points (Bowyer-Watson: each point removes the tetrahedra whose circumspheres hold it and
    fills the cavity from it; points inserted in Morton order, located by walking from the last tetrahedron made)."""
    low = [min(point[axis] for point in points) for axis in range(3)]
    high = [max(point[axis] for point in points) for axis in range(3)]
    size = max(high[axis] - low[axis] for axis in range(3)) or 1.0
    # Normalized, then perturbed by a millionth of a unit (deterministically): grids and regular surfaces put many points on
    # one sphere, which leaves the Delaunay tetrahedralization undecided; the output keeps the points as given.
    perturbation = random.Random(12345)
    normalized = [tuple((point[axis] - low[axis]) / size + 1e-6 * (perturbation.random() - 0.5) for axis in range(3)) for point in points]

    # A tetrahedron far around the unit cube.
    far = 100.0
    vertices: list[Vector] = normalized + [(-far, -far, -far), (far * 3, -far, -far), (-far, far * 3, -far), (-far, -far, far * 3)]
    super_first = len(normalized)

    tetrahedra: list[list[int] | None] = []
    spheres: list[tuple[Vector, float] | None] = []
    neighbors: list[list[int]] = []

    def add(corners: list[int]) -> int:
        tetrahedra.append(corners)
        spheres.append(circumsphere(*(vertices[index] for index in corners)))
        neighbors.append([-1, -1, -1, -1])
        return len(tetrahedra) - 1

    add([super_first, super_first + 1, super_first + 2, super_first + 3])
    walker = random.Random(54321)

    def faces(corners: list[int]) -> list[tuple[int, int, int]]:
        a, b, c, d = corners
        return [(b, c, d), (a, c, d), (a, b, d), (a, b, c)]  # face i is opposite corner i

    def in_sphere(index: int, point: Vector) -> bool:
        sphere = spheres[index]
        if sphere is None:
            return True
        center, radius = sphere
        offset = sub(point, center)
        return dot(offset, offset) < radius * (1.0 + 1e-12)

    def beyond(index: int, face: int, point: Vector) -> bool:
        """Whether point lies strictly beyond face of tetrahedron index (the side away from the opposite corner)."""
        corners = tetrahedra[index]
        i, j, k = faces(corners)[face]
        a, b, c = vertices[i], vertices[j], vertices[k]
        normal = cross(sub(b, a), sub(c, a))
        return dot(sub(point, a), normal) * dot(sub(vertices[corners[face]], a), normal) < 0.0

    def contains(index: int, point: Vector) -> int:
        """-1 when point is inside tetrahedron index, else a face it lies beyond, chosen at random among them (a
        stochastic walk never cycles)."""
        order = [0, 1, 2, 3]
        walker.shuffle(order)
        for face in order:
            if beyond(index, face, point):
                return face
        return -1

    def morton(point: Vector) -> int:
        code = 0
        cells = [min(1023, int(coordinate * 1023)) for coordinate in point]
        for bit in range(10):
            for axis in range(3):
                code |= ((cells[axis] >> bit) & 1) << (3 * bit + axis)
        return code

    last = 0
    for point_index in sorted(range(len(normalized)), key=lambda index: morton(normalized[index])):
        point = vertices[point_index]
        # Locate: walk toward the point from the last tetrahedron (its neighbors through the faces the point is beyond).
        current = last if tetrahedra[last] is not None else next(index for index, tet in enumerate(tetrahedra) if tet is not None)
        found = False
        for _ in range(4 * len(tetrahedra) + 16):
            face = contains(current, point)
            if face < 0:
                found = True
                break
            following = neighbors[current][face]
            if following < 0:
                break
            current = following
        if not found:
            # The walk got lost (numerically flat tetrahedra): every live tetrahedron tried.
            current = next((index for index, tet in enumerate(tetrahedra) if tet is not None and contains(index, point) < 0),
                           next(index for index, tet in enumerate(tetrahedra) if tet is not None and in_sphere(index, point)))
        # The cavity: tetrahedra whose circumspheres hold the point, connected to the one containing it.
        cavity = {current}
        stack = [current]
        while stack:
            index = stack.pop()
            for neighbor in neighbors[index]:
                if neighbor >= 0 and neighbor not in cavity and in_sphere(neighbor, point):
                    cavity.add(neighbor)
                    stack.append(neighbor)
        # Its boundary faces, each joined to the point: every one must face the point (rounding may leave the cavity not
        # star-shaped around it: then the tetrahedron beyond a face that does not is added, until all do).
        while True:
            boundary: list[tuple[tuple[int, int, int], int]] = []
            hidden = None
            for index in cavity:
                corners = tetrahedra[index]
                for face, face_corners in enumerate(faces(corners)):
                    outside = neighbors[index][face]
                    if outside >= 0 and outside in cavity:
                        continue
                    # The point must be on the cavity's side of the face, strictly.
                    a, b, c = (vertices[corner] for corner in face_corners)
                    normal = cross(sub(b, a), sub(c, a))
                    inner = dot(sub(vertices[corners[face]], a), normal)
                    side = dot(sub(point, a), normal)
                    if side * inner <= 0.0 or abs(side) <= 1e-14 * abs(inner):
                        hidden = outside
                        break
                    boundary.append((face_corners, outside))
                if hidden is not None:
                    break
            if hidden is None or hidden < 0:
                break
            cavity.add(hidden)
        for index in cavity:
            tetrahedra[index] = None
            spheres[index] = None
        made: dict[tuple[int, int, int], tuple[int, int]] = {}
        for face_corners, outside in boundary:
            corners_list = [face_corners[0], face_corners[1], face_corners[2], point_index]
            if signed_volume(*(vertices[corner] for corner in corners_list)) < 0.0:
                corners_list = [face_corners[1], face_corners[0], face_corners[2], point_index]
            created = add(corners_list)
            corners = tetrahedra[created]
            # The face opposite the new point faces outside.
            point_face = corners.index(point_index)
            neighbors[created][point_face] = outside
            if outside >= 0:
                outside_corners = tetrahedra[outside]
                shared = set(face_corners)
                for face, other in enumerate(faces(outside_corners)):
                    if set(other) == shared:
                        neighbors[outside][face] = created
                        break
            # The faces through the new point join the other new tetrahedra.
            for face, face_corner_set in enumerate(faces(corners)):
                if face == point_face:
                    continue
                key = tuple(sorted(face_corner_set))
                if key in made:
                    other, other_face = made.pop(key)
                    neighbors[created][face] = other
                    neighbors[other][other_face] = created
                else:
                    made[key] = (created, face)
            last = created
    return [tuple(tet) for tet in tetrahedra if tet is not None and max(tet) < super_first]


def delaunay(points: list[Vector]) -> list[tuple[int, int, int, int]]:
    try:
        from scipy.spatial import Delaunay  # type: ignore
    except ImportError:
        return delaunay_bowyer_watson(points)
    return [tuple(int(index) for index in simplex) for simplex in Delaunay(points).simplices]


# ===================================================================================================== the mesh

@dataclass
class TetrahedralMesh:
    positions: list[Vector]
    tetrahedra: list[tuple[int, int, int, int]]

    def surface(self) -> list[tuple[int, int, int]]:
        """The faces of one tetrahedron only, wound outward."""
        count: dict[tuple[int, int, int], int] = {}
        oriented: dict[tuple[int, int, int], tuple[int, int, int]] = {}
        for a, b, c, d in self.tetrahedra:
            for face in ((b, c, d), (a, d, c), (a, b, d), (a, c, b)):  # outward for a positive tetrahedron
                key = tuple(sorted(face))
                count[key] = count.get(key, 0) + 1
                oriented[key] = face
        return [oriented[key] for key, number in count.items() if number == 1]

    def edges(self) -> list[tuple[int, int]]:
        unique: set[tuple[int, int]] = set()
        for tet in self.tetrahedra:
            for i in range(4):
                for j in range(i + 1, 4):
                    unique.add((min(tet[i], tet[j]), max(tet[i], tet[j])))
        return sorted(unique)

    def volume(self) -> float:
        return sum(signed_volume(*(self.positions[index] for index in tet)) for tet in self.tetrahedra)


def tetrahedralize(mesh: SurfaceMesh, resolution: int, surface_spacing: float, minimum_quality: float, seed: int) -> TetrahedralMesh:
    mesh = clean(mesh)
    inside = InsideTest(mesh)
    points, spacing = sample_points(mesh, inside, resolution, surface_spacing, seed)
    tetrahedra = delaunay(points)

    # The tetrahedra inside the surface (their centers), oriented positively, without flat ones.
    kept: list[tuple[int, int, int, int]] = []
    for tet in tetrahedra:
        corners = [points[index] for index in tet]
        center = tuple(sum(corner[axis] for corner in corners) / 4.0 for axis in range(3))
        volume = signed_volume(*corners)
        if abs(volume) <= (spacing ** 3) * 1e-9 or not inside(center):
            continue
        kept.append(tet if volume > 0.0 else (tet[0], tet[2], tet[1], tet[3]))

    # Slivers with a face on the boundary removed, repeatedly (their removal exposes others); inner ones stay.
    if minimum_quality > 0.0:
        while True:
            faces: dict[tuple[int, int, int], int] = {}
            for tet in kept:
                for face in ((tet[1], tet[2], tet[3]), (tet[0], tet[2], tet[3]), (tet[0], tet[1], tet[3]), (tet[0], tet[1], tet[2])):
                    key = tuple(sorted(face))
                    faces[key] = faces.get(key, 0) + 1
            removed = []
            for index, tet in enumerate(kept):
                if quality(*(points[corner] for corner in tet)) >= minimum_quality:
                    continue
                for face in ((tet[1], tet[2], tet[3]), (tet[0], tet[2], tet[3]), (tet[0], tet[1], tet[3]), (tet[0], tet[1], tet[2])):
                    if faces[tuple(sorted(face))] == 1:
                        removed.append(index)
                        break
            if not removed:
                break
            removed_set = set(removed)
            kept = [tet for index, tet in enumerate(kept) if index not in removed_set]

    # The vertices used, in first-use order.
    order: dict[int, int] = {}
    for tet in kept:
        for corner in tet:
            order.setdefault(corner, len(order))
    positions = [points[old] for old in order]
    return TetrahedralMesh(positions, [tuple(order[corner] for corner in tet) for tet in kept])


# ===================================================================================================== writing

def write_tetgen(mesh: TetrahedralMesh, output: pathlib.Path) -> list[pathlib.Path]:
    node = [f"{len(mesh.positions)} 3 0 0"] + [f"{index} {x!r} {y!r} {z!r}" for index, (x, y, z) in enumerate(mesh.positions)]
    element = [f"{len(mesh.tetrahedra)} 4 0"] + [f"{index} {a} {b} {c} {d}" for index, (a, b, c, d) in enumerate(mesh.tetrahedra)]
    surface = mesh.surface()
    face = [f"{len(surface)} 0"] + [f"{index} {a} {b} {c}" for index, (a, b, c) in enumerate(surface)]
    paths = [output.with_suffix(".node"), output.with_suffix(".ele"), output.with_suffix(".face")]
    for path, lines in zip(paths, (node, element, face)):
        path.write_text("# gfx tools/tetrahedralize.py (indices from 0)\n" + "\n".join(lines) + "\n", encoding="utf-8")
    return paths


def write_json(mesh: TetrahedralMesh, output: pathlib.Path, statistics: dict) -> list[pathlib.Path]:
    path = output.with_suffix(".json")
    path.write_text(json.dumps({"positions": [list(position) for position in mesh.positions],
                                "tetrahedra": [list(tet) for tet in mesh.tetrahedra],
                                "surface": [list(face) for face in mesh.surface()],
                                "edges": [list(edge) for edge in mesh.edges()],
                                "statistics": statistics}, indent=None), encoding="utf-8")
    return [path]


def write_cpp(mesh: TetrahedralMesh, output: pathlib.Path, module_name: str) -> list[pathlib.Path]:
    path = output.with_suffix(".cppm")
    floats = ",\n    ".join(f"{x!r}F, {y!r}F, {z!r}F" for x, y, z in mesh.positions)
    indices = ",\n    ".join(f"{a}U, {b}U, {c}U, {d}U" for a, b, c, d in mesh.tetrahedra)
    name = module_name.split(".")[-1]
    path.write_text(f"""// Generated by gfx tools/tetrahedralize.py: a tetrahedral mesh for gfx::physics::volume_preset().
//   TetrahedralMesh mesh;
//   for (std::size_t i = 0; i < {name}::positions.size(); i += 3) mesh.positions.push_back({{.x = {name}::positions[i], .y = ..., .z = ...}});
//   for (std::size_t i = 0; i < {name}::tetrahedra.size(); i += 4) mesh.tetrahedra.push_back({{...four indices...}});
export module {module_name};

import std;

export namespace {name}
{{

/// Vertex positions, x, y, z each.
inline constexpr std::array<float, {3 * len(mesh.positions)}> positions{{
    {floats}}};

/// Tetrahedra, four vertex indices each (positive volume: (b - a) · ((c - a) × (d - a)) > 0).
inline constexpr std::array<std::uint32_t, {4 * len(mesh.tetrahedra)}> tetrahedra{{
    {indices}}};

}} // namespace {name}
""", encoding="utf-8")
    return [path]


# ===================================================================================================== checking

def read_tetgen(output: pathlib.Path) -> TetrahedralMesh:
    def rows(path: pathlib.Path) -> list[list[str]]:
        return [line.split("#")[0].split() for line in path.read_text(encoding="utf-8").splitlines() if line.split("#")[0].split()]
    nodes = rows(output.with_suffix(".node"))
    elements = rows(output.with_suffix(".ele"))
    first_node = int(nodes[1][0]) if len(nodes) > 1 else 0
    positions = [(float(row[1]), float(row[2]), float(row[3])) for row in nodes[1:1 + int(nodes[0][0])]]
    tetrahedra = [tuple(int(value) - first_node for value in row[1:5]) for row in elements[1:1 + int(elements[0][0])]]
    return TetrahedralMesh(positions, tetrahedra)


def voxel_volume(surface: SurfaceMesh, inside: InsideTest, cells: int = 32) -> float:
    """The volume inside the surface by the inside test at the centers of cells**3 boxes over its bounds (overlapping
    parts once, as the tetrahedralization sees them)."""
    low = [min(vertex[axis] for vertex in surface.vertices) for axis in range(3)]
    high = [max(vertex[axis] for vertex in surface.vertices) for axis in range(3)]
    steps = [(high[axis] - low[axis]) / cells for axis in range(3)]
    count = 0
    for i in range(cells):
        for j in range(cells):
            for k in range(cells):
                count += 1 if inside((low[0] + (i + 0.5) * steps[0], low[1] + (j + 0.5) * steps[1], low[2] + (k + 0.5) * steps[2])) else 0
    return count * steps[0] * steps[1] * steps[2]


def check(mesh: TetrahedralMesh, surface_volume: float) -> list[str]:
    problems = []
    if not mesh.tetrahedra:
        problems.append("no tetrahedron")
        return problems
    for index, tet in enumerate(mesh.tetrahedra):
        if any(corner < 0 or corner >= len(mesh.positions) for corner in tet):
            problems.append(f"tetrahedron {index} names a vertex out of range")
        elif signed_volume(*(mesh.positions[corner] for corner in tet)) <= 0.0:
            problems.append(f"tetrahedron {index} is not positively oriented")
    used = {corner for tet in mesh.tetrahedra for corner in tet}
    if len(used) != len(mesh.positions):
        problems.append(f"{len(mesh.positions) - len(used)} vertices are unused")
    edges: dict[tuple[int, int], int] = {}
    for a, b, c in mesh.surface():
        for edge in ((a, b), (b, c), (c, a)):
            key = (min(edge), max(edge))
            edges[key] = edges.get(key, 0) + 1
    open_edges = sum(1 for count in edges.values() if count % 2 != 0)
    if open_edges:
        problems.append(f"the boundary is not closed ({open_edges} edges on an odd number of its faces)")
    volume = mesh.volume()
    if surface_volume > 0.0 and abs(volume - surface_volume) > 0.1 * surface_volume:
        problems.append(f"the tetrahedra's volume {volume:.6g} is not within 10 % of the surface's {surface_volume:.6g}")
    return problems


def statistics(mesh: TetrahedralMesh, surface_volume: float) -> dict:
    qualities = [quality(*(mesh.positions[corner] for corner in tet)) for tet in mesh.tetrahedra] or [0.0]
    return {"vertices": len(mesh.positions), "tetrahedra": len(mesh.tetrahedra), "boundary_faces": len(mesh.surface()),
            "volume": mesh.volume(), "surface_volume": surface_volume, "minimum_quality": min(qualities),
            "mean_quality": sum(qualities) / len(qualities)}


# ===================================================================================================== main

def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("mesh", type=pathlib.Path, help="an OBJ, STL, PLY or OFF triangle mesh (closed)")
    parser.add_argument("--output", type=pathlib.Path, required=True, help="the output path without its extension")
    parser.add_argument("--format", choices=("tetgen", "json", "cpp"), default="tetgen")
    parser.add_argument("--resolution", type=int, default=12, help="interior grid cells along the longest side")
    parser.add_argument("--surface-spacing", type=float, default=0.5, help="surface vertices kept per this fraction of the spacing (0: all)")
    parser.add_argument("--minimum-quality", type=float, default=0.02, help="boundary tetrahedra flatter than this are removed (0: none)")
    parser.add_argument("--scale", type=float, default=1.0, help="multiplies the positions")
    parser.add_argument("--module-name", default="tetrahedral_mesh", help="--format cpp: the module's name (its last part names the namespace)")
    parser.add_argument("--seed", type=int, default=1, help="the interior points' jitter (the same seed, the same mesh)")
    parser.add_argument("--check", action="store_true", help="validate what was written; exit code 1 if a check fails")
    options = parser.parse_args(arguments)

    surface = read_mesh(options.mesh)
    surface.vertices = [tuple(coordinate * options.scale for coordinate in vertex) for vertex in surface.vertices]
    cleaned = clean(surface)
    surface_volume = voxel_volume(cleaned, InsideTest(cleaned))
    mesh = tetrahedralize(cleaned, options.resolution, options.surface_spacing, options.minimum_quality, options.seed)
    stats = statistics(mesh, surface_volume)
    options.output.parent.mkdir(parents=True, exist_ok=True)
    if options.format == "tetgen":
        written = write_tetgen(mesh, options.output)
    elif options.format == "json":
        written = write_json(mesh, options.output, stats)
    else:
        written = write_cpp(mesh, options.output, options.module_name)
    print(f"{options.mesh.name}: {stats['vertices']} vertices, {stats['tetrahedra']} tetrahedra, {stats['boundary_faces']} boundary faces; "
          f"volume {stats['volume']:.6g} (the surface's {surface_volume:.6g}); quality minimum {stats['minimum_quality']:.3f}, "
          f"mean {stats['mean_quality']:.3f}")
    for path in written:
        print(f"  wrote {path}")
    if options.check:
        written_mesh = read_tetgen(options.output) if options.format == "tetgen" else mesh
        problems = check(written_mesh, surface_volume)
        for problem in problems:
            print(f"  check failed: {problem}")
        if problems:
            return 1
        print("  checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

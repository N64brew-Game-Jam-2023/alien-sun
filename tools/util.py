import io, itertools, math, numpy, os, pycparser, pytiled_parser, re
from pathlib import Path
from struct import pack
from typing import Optional

C_ARRAY_RE = re.compile(r'(\w+)_paths')
C_PATH_STEM_RE = re.compile(r'"rom:/([^"]+)\.\w+"')

def err(msg: str):
    raise RuntimeError(msg)

def rotate_point(x0: int, y0: int, rot: int, xc: int, yc: int) -> (int, int):
    x0 -= xc
    y0 -= yc
    rot = math.radians(rot)
    sin = math.sin(rot)
    cos = math.cos(rot)
    return (x0 * cos - y0 * sin + xc, y0 * cos + x0 * sin + yc)

class CEnum:
    values: dict[str, int]
    def __init__(self, path: Path, typename: str):
        self.values = {}
        ast = pycparser.parse_file(path, use_cpp=True, cpp_args='-w')
        for decl in ast.ext:
            if isinstance(decl, pycparser.c_ast.Typedef) \
                    and decl.name == typename \
                    and isinstance(decl.type.type, pycparser.c_ast.Enum):
                counter = 0
                for value in decl.type.type.values.enumerators:
                    v = value.value
                    if v is None:
                        v = counter
                        counter += 1
                    else:
                        err(f'explicit enum values not supported: {v}')
                    self.values[value.name] = v
                return
        err(f'`{typename}` enum typedef not found')
    def value(self, name: str) -> int:
        return self.values[name]


class AssetList:
    groups: dict[bytes, list[Optional[str]]]
    def __init__(self, path: Path, asset_dir: Path):
        self.groups = {}
        self.asset_dir = asset_dir
        ast = pycparser.parse_file(path, use_cpp=True, cpp_args='-w')
        for decl in ast.ext:
            if not isinstance(decl, pycparser.c_ast.Decl) \
                    or not isinstance(decl.init, pycparser.c_ast.InitList):
                continue
            m = C_ARRAY_RE.fullmatch(decl.name)
            if not m:
                continue
            group = []
            for expr in decl.init.exprs:
                if isinstance(expr, pycparser.c_ast.Constant) and expr.type == 'string':
                    pm = C_PATH_STEM_RE.fullmatch(expr.value)
                    if pm is not None:
                        group.append(pm.group(1))
                        continue
                group.append(None)
            self.groups[m.group(1)] = group
    def index(self, cat: str, path: Path):
        key = str(Path(os.path.abspath(path)).relative_to(self.asset_dir).with_suffix(''))
        try:
            return self.groups[cat].index(key)
        except ValueError:
            err(f'unknown asset `{key}`')

class PoolObj:
    data: io.BytesIO
    ptr_pos: int
    objs: list['PoolObj']
    priority: int
    def __init__(self, pos: int, priority: int = 0):
        self.data = io.BytesIO()
        self.ptr_pos = pos
        self.objs = []
        self.priority = priority
    def write(self, b: bytes):
        self.data.write(b)
    def write_ref(self, priority: int = 0) -> 'PoolObj':
        pos = self.data.tell()
        self.data.write(b'\x00' * 4)
        obj = PoolObj(pos, priority)
        self.objs.append(obj)
        return obj

class DataPool:
    data: io.BytesIO
    objs: list[PoolObj]
    def __init__(self, init: bytes | None = None):
        self.objs = []
        self.data = io.BytesIO(init)
        self.data.seek(0, io.SEEK_END)
    def write(self, b: bytes):
        self.data.write(b)
    def write_ref(self, priority: int = 0) -> PoolObj:
        pos = self.data.tell()
        self.data.write(b'\x00' * 4)
        obj = PoolObj(pos, priority)
        self.objs.append(obj)
        return obj
    def finish(self) -> bytes:
        nextobjs = []
        objs = self.objs
        self.objs = []
        unique_objs = {}

        while len(objs) > 0:
            objs.sort(reverse=True, key=lambda o: o.priority)

            for obj in objs:
                data_pos = self.data.tell()
                data = obj.data.getvalue()
                if len(data) == 0:
                    data_pos = 0
                elif len(obj.objs) == 0:
                    data_pos = unique_objs.setdefault(data, data_pos)
                if data_pos == self.data.tell():
                    self.data.write(data)
                self.data.seek(obj.ptr_pos, io.SEEK_SET)
                self.data.write(pack('>I', data_pos))
                self.data.seek(0, io.SEEK_END)
                for subobj in obj.objs:
                    subobj.ptr_pos += data_pos
                nextobjs += obj.objs
            objs = nextobjs
            nextobjs = []
        while (self.data.tell() & 15) != 0: # pad to 16 byte boundary
            self.data.write(b'\x00')
        value = self.data.getvalue()
        self.data = io.BytesIO()
        return value

POINT_SCALE = 1/16

COLL_END = 0
COLL_CIRCLE = 1
COLL_AABB = 2
COLL_TRIANGLE = 3
COLL_QUAD = 4
COLL_POLY = 5
COLL_EDGE = 6
COLL_CHAIN = 7

def pack_collision_points(points, typ: str, flags: int = 0, fid: bytes = b'') -> bytes:
    pcount = len(points)
    if pcount < 2:
        return bytes()

    points = numpy.array(points).round() * POINT_SCALE

    def header(typ: int) -> bytes:
        return pack('>HH4s', typ, flags, fid)

    if typ == 'circle':
        rad, x, y = points
        return header(COLL_CIRCLE) + pack('>fff', rad, x + rad, y + rad)

    if typ == 'aabb':
        x, y, w, h = points
        return header(COLL_AABB) + pack('>ffff', x, y, x + w, y + h)

    if pcount == 2:
        return header(COLL_EDGE) + pack('>ffff', *points.flatten())
    elif typ == 'polyline':
        ax, ay = points[0]
        bx, by = points[-1]
        buf = header(COLL_CHAIN) + pack('>Iffff', pcount, ax, ay, bx, by)
        for x, y in points:
            buf += pack('>ff', x, y)
        return buf
    elif pcount == 3:
        return header(COLL_TRIANGLE) + pack('>ffffff', *points.flatten())
    elif pcount == 4:
        return header(COLL_QUAD) + pack('>ffffffff', *points.flatten())
    else:
        buf = header(COLL_POLY) + pack('>I', pcount)
        for x, y in points:
            buf += pack('>ff', x, y)
        return buf

    return bytes()

def pack_collision_obj(collision: DataPool, obj: pytiled_parser.tiled_object.TiledObject, offset: pytiled_parser.OrderedPair = (0, 0)):
    tx, ty = offset
    flags = 0
    if (obj.properties or {}).get('sensor') == True:
        flags |= 1
    if (obj.properties or {}).get('interactive') == True:
        flags |= 3
    fid = bytes(obj.name, 'ascii') if obj.name else bytes()
    if isinstance(obj, pytiled_parser.tiled_object.Rectangle):
        if obj.rotation:
            x0, y0 = obj.coordinates
            x1 = x0 + obj.size.width
            y1 = y0 + obj.size.height
            points = numpy.array(
                    [rotate_point(x, y, obj.rotation, x0, y0) for x, y in
                     [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]]) + offset
            collision.write(pack_collision_points(points, 'polygon', flags, fid))
        else:
            x, y = obj.coordinates
            w, h = obj.size
            points = [x + tx, y + ty, w, h]
            collision.write(pack_collision_points(points, 'aabb', flags, fid))
    elif isinstance(obj, pytiled_parser.tiled_object.Ellipse):
        if obj.size.width != obj.size.height:
            err(f'Ellipse collision object {obj.id} is not a circle')
        rad = obj.size.width / 2
        x, y = obj.coordinates
        points = [rad, x + tx, y + ty]
        collision.write(pack_collision_points(points, 'circle', flags, fid))
    elif isinstance(obj, pytiled_parser.tiled_object.Polygon):
        ox, oy = obj.coordinates
        points = offset + numpy.array(
                [rotate_point(p.x + ox, p.y + oy, obj.rotation, ox, oy) for p in obj.points]
                if obj.rotation else [(p.x + ox, p.y + oy) for p in obj.points])
        collision.write(pack_collision_points(points, 'polygon', flags, fid))
    elif isinstance(obj, pytiled_parser.tiled_object.PolyLine):
        ox, oy = obj.coordinates
        points = offset + numpy.array(
                [rotate_point(p.x + ox, p.y + oy, obj.rotation, ox, oy) for p in obj.points]
                if obj.rotation else [(p.x + ox, p.y + oy) for p in obj.points])
        collision.write(pack_collision_points(points, 'polyline', flags, fid))
    else:
        err(f'collision object type {type(obj).name} not supported')


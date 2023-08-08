#!/usr/bin/env python3

import array, argparse, mapscriptparser, math, numpy, os, pyclipr, pytiled_parser, re, sys, util
from PIL import Image
from mapscriptparser import ScriptCommand, ScriptColor, ScriptFunc, \
                            ScriptFuncArgs, ScriptIdent, ScriptValue, \
                            script_parser, inline_script_parser
from pathlib import Path
from scriptcmds import SCRIPT_COMMANDS
from struct import pack
from types import NoneType
from typing import Optional
from util import err

DEFAULT_GRAVITY = (0, 1000)

IDENT_RE = re.compile(r"[_a-zA-Z][_a-zA-Z0-9]*")

TMX_FLIPX = (1<<31)
TMX_FLIPY = (1<<30)
TMX_FLIPD = (1<<29)
MAP_FLIPX = 0x8000
MAP_FLIPY = 0x4000
MAP_FLIPD = 0x2000
AF_CUR_PLAYER = (1<<29)
AF_FLIPX  = (1<<28)
AF_FLIPY  = (1<<27)
AF_FLIPD  = (1<<26)

parser = argparse.ArgumentParser(prog='Tile Definition Builder Tool')
parser.add_argument('filename', type=Path)
parser.add_argument('-a', '--asset-list', type=Path, required=True)
parser.add_argument('-t', '--actor-types', type=Path, required=True)
parser.add_argument('-s', '--script-ops', type=Path, required=True)
parser.add_argument('-o', '--output', type=Path, default=Path('.'))
parser.add_argument('-v', '--verbose', action='store_true')
parser.add_argument('-S', '--svg-dump', action='store_true')

args = parser.parse_args()
#args.svg_dump = True

def pack_color(c: Optional[pytiled_parser.Color]) -> int:
    if c is None:
        return 0
    return (c.red << 24) + (c.green << 16) + (c.blue << 8) + c.alpha

def process_map(path: Path):
    if args.asset_list is None:
        err('argument -a is required to build maps')

    tmap = pytiled_parser.parse_map(path)
    map_props = tmap.properties or {}

    # load types
    actor_types = util.CEnum(args.actor_types, 'actor_type_t')
    script_ops = util.CEnum(args.script_ops, 'script_op_t')

    # load asset lists
    orig_dir = os.getcwd()
    asset_dir = Path(__file__).absolute().parent.parent.joinpath('assets/')
    assets = util.AssetList(args.asset_list, asset_dir)
    os.chdir(path.parent)

    # sanity checks
    if tmap.orientation != 'orthogonal':
        err('only orthogonal maps are supported')
    if tmap.render_order != 'right-down':
        err('only maps with right-down render order are supported')
    if tmap.tile_size.width != 16 or tmap.tile_size.height != 16:
        err('tilemap must have 16x16 tiles')
    if not tmap.infinite:
        err('only infinite maps are supported')
    if tmap.map_size.width % 16 != 0 or tmap.map_size.height % 16 != 0:
        err(f'map size must be a multiple of 16')

    # parse scripts
    script_dict: dict[str, int] = {}
    scripts: list[ScriptFunc] = []
    startup_script = None
    script_src = map_props.get('scripts')
    if script_src:
        scripts = script_parser.parse(script_src)
        i = 0
        for script in scripts:
            if script.name in script_dict:
                err(f'duplicate script {script.name}')
            for attrib in script.attributes:
                if attrib == 'startup':
                    if startup_script is not None:
                        err('only one script can be #[startup]')
                    startup_script = i
                elif attrib == 'singleton':
                    script.singleton = True
                else:
                    err(f'unknown script attribute {attrib}')
            script.source = 'map'
            script_dict[script.name] = i
            i += 1
    if startup_script is None:
        startup_script = 0xffffffff

    # scan tilesets
    all_tilesets: list[pytiled_parser.Tileset] = list(tmap.tilesets.values())
    actor_tileset: Optional[pytiled_parser.Tileset] = None
    actor_last_gid: Optional[int] = None
    prop_tileset: Optional[pytiled_parser.Tileset] = None
    prop_last_gid: Optional[int] = None
    next_gid: int = 1
    tid_map: dict[int, int] = {}
    tilesets: list[pytiled_parser.Tileset] = []

    for index, tiles in enumerate(all_tilesets):
        if tiles.name == 'actors':
            if actor_tileset is not None:
                err('duplicate `actors` tileset')
            if tiles.alignment != 'topleft':
                err('actors tileset must have alignment `topleft`')
            actor_tileset = tiles
            actor_last_gid = all_tilesets[index + 1].firstgid if index < len(tmap.tilesets) - 1 else sys.maxsize
        elif tiles.name == 'props':
            if prop_tileset is not None:
                err('duplicate `props` tileset')
            if tiles.alignment != 'topleft':
                err('props tileset must have alignment `topleft`')
            prop_tileset = tiles
            prop_last_gid = all_tilesets[index + 1].firstgid if index < len(tmap.tilesets) - 1 else sys.maxsize
        else:
            if tiles.image is None:
                err(f'tileset `{tiles.name}` must be a spritesheet')
            if tiles.tile_width != 16 or tiles.tile_height != 16:
                err(f'tileset `{tiles.name}` must have 16x16 tiles')
            if tiles.image_width & (tiles.image_width - 1) != 0:
                err(f'image in tileset `{tiles.name}` must have width be a power of two')
            if tiles.tile_count > 0:
                tilesets.append(tiles)
                tid_map[tiles.firstgid] = next_gid
                next_gid = (next_gid + tiles.tile_count + 0b1111) & ~0b1111

    # scan layers
    bgs: list[tuple[pytiled_parser.ImageLayer, int]] = []
    waypoints: list[pytiled_parser.tiled_object.Point] = []
    lower_x: int = 0
    lower_y: int = 0
    cur_layer: int = 0
    chunks = {}
    actors: list[pytiled_parser.tiled_object.Tile] = []
    actor_layer: Optional[pytiled_parser.ObjectLayer] = None
    camera_start = None
    player_actor = None
    water_line = -0x80000000
    water_color = 0
    script_objs = ScriptObjPool()
    map_collision = MapCollisionBuilder(tmap, tilesets)
    triggers: list[pytiled_parser.tiled_object.TiledObject] = []

    for layer in tmap.layers:
        layer_collide = (layer.properties or {}).get('collide') != False
        if isinstance(layer, pytiled_parser.ImageLayer):
            if layer.name.lower() == 'water':
                water_line = int(layer.offset.y) + 8
                if layer.tint_color:
                    water_color = pack_color(layer.tint_color)
            else:
                if args.verbose:
                    print(f'BG Layer `{layer.name}` with image `{layer.image}`')
                bgs.append((layer, cur_layer))
        elif isinstance(layer, pytiled_parser.TileLayer):
            if args.verbose:
                print(f'FG Layer `{layer.name}` with size `{layer.size}`')
            if layer.offset.x != 0 or layer.offset.y != 0:
                err(f'offsets not supported on tile layer `{layer.name}`')
            if layer.parallax_factor.x != 1 or layer.parallax_factor.y != 1:
                err(f'parallax factors != 1 not supported on tile layer `{layer.name}`')
            if layer.coordinates.x % 256 != 0 or layer.coordinates.y % 256 != 0:
                err(f'tile `{layer.name}` layer coordinates must be a multiple of 256')
            if layer.size.width % 16 != 0 or layer.size.height % 16 != 0:
                err(f'tile `{layer.name}` size must be a multiple of 16')
            lower_x = min(lower_x, layer.coordinates.x >> 8)
            lower_y = min(lower_y, layer.coordinates.y >> 8)
            if cur_layer == 0:
                cur_layer = 1
            elif cur_layer == 2:
                cur_layer = 3
            for chunk in (layer.chunks or []):
                if chunk.coordinates.x % 16 != 0 or chunk.coordinates.y % 16 != 0:
                    err(f'all chunk coordinates must be a multiple of 16 (got {chunk.coordinates})')
                if chunk.size.width != 16 or chunk.size.height != 16:
                    err('all chunks must have size 16x16')
                if layer_collide:
                    map_collision.add_chunk(layer, chunk)
                cdata = []
                for y in range(16):
                    for x in range(16):
                        orig_tid = chunk.data[y][x]
                        tid = orig_tid & 0x0fffffff
                        if tid > 0:
                            gid_found = False
                            for tiles in reversed(tilesets):
                                if tid >= tiles.firstgid:
                                    tid = tid - tiles.firstgid + tid_map[tiles.firstgid]
                                    gid_found = True
                                    break
                            if not gid_found:
                                err(f'invalid tile ID {tid}')
                        if orig_tid & TMX_FLIPX:
                            tid |= MAP_FLIPX
                        if orig_tid & TMX_FLIPY:
                            tid |= MAP_FLIPY
                        if orig_tid & TMX_FLIPD:
                            tid |= MAP_FLIPD
                        cdata.append(tid)
                coord = (int(chunk.coordinates.x) >> 4, int(chunk.coordinates.y) >> 4)
                c = chunks.setdefault(coord, [[], [], None])
                if c[2] is None and cur_layer == 3:
                    c[2] = len(c[0])
                c[0].append(cdata)
        elif isinstance(layer, pytiled_parser.ObjectLayer):
            if layer.parallax_factor.x != 1 or layer.parallax_factor.y != 1:
                err(f'parallax factors != 1 not supported on object layer `{layer.name}`')
            for obj in layer.tiled_objects:
                obj.properties = obj.properties or {}
                if isinstance(obj, pytiled_parser.tiled_object.Point):
                    obj.coordinates = pytiled_parser.OrderedPair(
                            obj.coordinates.x + layer.offset.x,
                            obj.coordinates.y + layer.offset.y)
                    if obj.name == 'camera-start':
                        camera_start = (int(obj.coordinates.x), int(obj.coordinates.y))
                    else:
                        wp_name = obj.properties.get('name')
                        if type(wp_name) == str:
                            script_objs.insert(wp_name, 'waypoint', len(waypoints))
                        waypoints.append(obj)
                elif isinstance(obj, pytiled_parser.tiled_object.Rectangle) \
                        or isinstance(obj, pytiled_parser.tiled_object.Ellipse) \
                        or isinstance(obj, pytiled_parser.tiled_object.Polygon) \
                        or isinstance(obj, pytiled_parser.tiled_object.Polyline):
                    obj.coordinates = pytiled_parser.OrderedPair(
                            obj.coordinates.x + layer.offset.x,
                            obj.coordinates.y + layer.offset.y)
                    trigger_script = obj.properties.get('trigger')
                    if trigger_script:
                        if IDENT_RE.fullmatch(trigger_script):
                            script = script_dict.get(trigger_script)
                            if script is None:
                                err(f'unknown script {trigger_script}')
                            obj.properties['trigger'] = script
                        else:
                            script: ScriptFunc = inline_script_parser.parse(trigger_script)
                            for attrib in script.attributes:
                                if attrib == 'singleton':
                                    script.singleton = True
                                else:
                                    err(f'unknown script attribute {attrib}')
                            if len(script.commands) == 0:
                                continue
                            if len(script.commands) == 1 \
                                    and script.commands[0].name == 'jump' \
                                    and len(script.commands[0].args) == 1 \
                                    and len(script.commands[0].args.args) == 1 \
                                    and isinstance(script.commands[0].args.args[0].value, ScriptIdent):
                                script_name = script.commands[0].args.args[0].value.value
                                script = script_dict.get(script_name)
                                if script is None:
                                    err(f'unknown script {script_name}')
                                obj.properties['trigger'] = script
                            else:
                                script.source = f'trigger {obj.id}'
                                obj.properties['trigger'] = len(scripts)
                                scripts.append(script)
                        triggers.append(obj)
                        if obj.name:
                            script_objs.insert(obj.name, 'actor')
                    else:
                        map_collision.add_object(obj)
                elif isinstance(obj, pytiled_parser.tiled_object.Tile):
                    gid = obj.gid & 0x0fffffff
                    if actor_tileset is not None and gid >= actor_tileset.firstgid and gid < actor_last_gid:
                        if obj.properties.get('player') == True:
                            player_actor = obj
                        if obj.name:
                            script_objs.insert(obj.name, 'actor')
                        obj.properties['typename'] = actor_tileset.tiles[gid - actor_tileset.firstgid].properties['actor']
                        actors.append(obj)
                        if actor_layer is None:
                            actor_layer = layer
                            cur_layer = 2
                        elif actor_layer != layer:
                            err('actors can only be on one layer')
                    elif prop_tileset is not None and gid >= prop_tileset.firstgid and gid < prop_last_gid:
                        start = (int(obj.coordinates.x) // 256, int(obj.coordinates.y) // 256)
                        end = (int(obj.coordinates.x + obj.size.width) // 256, int(obj.coordinates.y + obj.size.height) // 256)
                        for y in range(start[1], end[1]+1):
                            for x in range(start[0], end[0]+1):
                                c = chunks.setdefault((x, y), [[], [], None])
                                c[1].append((cur_layer, obj))

    # compile scripts
    compiled_scripts: list[bytes] = []
    string_pool: list[str] = []
    script_actors: list[bytes] = []
    actor_count = len(actors) + len(triggers)

    def scripterr(func, tok, msg):
        pos = tok.pos if tok else func.pos
        err(f'{func.source} script: line {pos[0]} col {pos[1]}: {msg}')
    def checkargtype(func, value, expected, *types):
        vt = type(value.value)
        if all(not value.is_special(t[1:]) if type(t) == 'str' else vt != t for t in types):
            scripterr(script, value, f'expected {expected}, got {value.type_name()}')

    for script in scripts:
        script_buf = util.DataPool()
        if script.singleton:
            script_buf.write(pack('>I', script_ops.value('OP_SINGLETON')))
        if len(script.commands) == 0 or script.commands[-1].name not in ['jump', 'exit', 'return']:
            script.commands.append(ScriptCommand((-1, -1), 'return'))
        if args.verbose:
            print(f'Script {script.name or f"<{script.source}>"} with {len(script.commands)} commands');
        for command in script.commands:
            cmd_def = SCRIPT_COMMANDS.get(command.name)
            if cmd_def is None:
                scripterr(script, cmd, f'unknown script command {command}')
            cmd_id, arg_defs = cmd_def
            script_buf.write(pack('>I', script_ops.value(cmd_id)))
            buf = script_buf
            if command.name == 'spawn_actor':
                buf = util.DataPool()
            arg_counter = 0
            kwarg_counter = 0

            for typ in arg_defs:
                value = None
                name = None
                optional = False

                if type(typ) == tuple:
                    name, typ = typ
                    kwarg_counter += 1
                else:
                    name = arg_counter
                    arg_counter += 1

                if typ[0] == '?':
                    optional = True
                    typ = typ[1:]

                if name in command.args:
                    value = command.args[name]
                    if type(name) == int and kwarg_counter > 0:
                        scripterr(script, value, 'positional arguments must come before keyword arguments')
                elif optional:
                    if typ == 'int' or typ == 'float' or typ == 'color' or typ == 'angle':
                        value = ScriptValue((-1, -1), 0)
                    else:
                        value = ScriptValue((-1, -1), None)
                else:
                    if type(name) == int:
                        required = len(filter(lambda a: type(a) != tuple and a[0] != '?', arg_defs))
                        scripterr(script, cmd, f'expected {required} positional args, got {len(command.args.args)}')
                    else:
                        scripterr(script, cmd, f'missing required keyword argument {name}')

                if typ == 'script':
                    checkargtype(script, value, 'script identifier', ScriptIdent)
                    script_id = script_dict.get(value.value.value)
                    if script_id is None:
                        scripterr(script, value, f'unknown script `{value.value.value}`')
                    buf.write(pack('>I', script_id))
                elif typ == 'activescript':
                    checkargtype(script, value, 'script identifier or @child', ScriptIdent, '@child')
                    if value.is_special('child'):
                        index = 0xffffffff
                    else:
                        index = script_dict.get(value.value.value)
                        if index is None:
                            scripterr(script, value, f'unknown script `{value.value.value}`')
                    buf.write(pack('>I', index))
                elif typ == 'int':
                    checkargtype(script, value, 'integer', int)
                    if value.value > 0x7fffffff or value.value < -0x80000000:
                        scripterr(script, value, 'int32 value out of range')
                    buf.write(pack('>i', value.value))
                elif typ == 'uint':
                    checkargtype(script, value, 'unsigned integer', int)
                    if value.value > 0xffffffff or value.value < 0:
                        scripterr(script, value, 'uint32 value out of range')
                    buf.write(pack('>I', value.value))
                elif typ == 'ushort':
                    checkargtype(script, value, 'unsigned integer', int)
                    if value.value > 0xffff or value.value < 0:
                        scripterr(script, value, 'uint16 value out of range')
                    buf.write(pack('>H', value.value))
                elif typ == 'string':
                    checkargtype(script, value, 'string', str)
                    index = None
                    try:
                        index = string_pool.index(value.value)
                    except ValueError:
                        index = len(string_pool)
                        string_pool.append(value.value)
                    buf.write(pack('>I', index))
                elif typ == 'color':
                    checkargtype(script, value, 'color literal or unsigned integer', int, ScriptColor)
                    if type(value.value) == int:
                        color = value.value
                        if value.value > 0xffffffff or value.value < 0:
                            scripterr(script, value, 'uint32 value out of range')
                    else:
                        color = value.value.int_value()
                    buf.write(pack('>I', color))
                elif typ == 'actor':
                    checkargtype(script, value,
                                 'actor identifier or @caller' + (' or null' if optional else ''),
                                 ScriptIdent, '@caller', NoneType)
                    if value.value is None:
                        index = 0
                    elif value.is_special('caller'):
                        index = 0x80000001
                    else:
                        try:
                            index = script_objs.get('actor', value.value.value)
                        except Exception as e:
                            scripterr(script, value, e)
                    buf.write(pack('>I', index))
                elif typ == 'target':
                    checkargtype(script, value, 'identifier or @caller or @camera or null', ScriptIdent, '@caller', '@camera', NoneType)
                    if value.value is None:
                        index = 0
                    elif value.is_special('camera'):
                        index = 0x80000000
                    elif value.is_special('caller'):
                        index = 0x80000001
                    else:
                        index = script_objs.try_get('actor', value.value.value)
                        if index is None:
                            index = script_objs.try_get('waypoint', value.value.value)
                            if index is None:
                                scripterr(script, value, f'no such target `{value.value}`')
                            index = -(index + 1)
                    buf.write(pack('>I', index))
                elif typ == 'float':
                    checkargtype(script, value, 'float', float, int)
                    buf.write(pack('>f', value.value))
                elif typ == 'map':
                    checkargtype(script, value, 'string', str)
                    try:
                        map_id = assets.index('maps', value.value)
                    except Exception as e:
                        scripterr(script, value, e)
                    buf.write(pack('>I', map_id))
                elif typ == 'music':
                    checkargtype(script, value, 'string', str, NoneType)
                    music_id = 0
                    if value.value is not None:
                        try:
                            music_id = assets.index('mus', value.value)
                        except Exception as e:
                            scripterr(script, value, e)
                    buf.write(pack('>I', music_id))
                elif typ == 'sfx':
                    checkargtype(script, value, 'string', str, NoneType)
                    sfx_id = 0
                    if value.value is not None:
                        try:
                            sfx_id = assets.index('sfx', value.value)
                        except Exception as e:
                            scripterr(script, value, e)
                    buf.write(pack('>I', sfx_id))
                elif typ == 'actortype':
                    checkargtype(script, value, 'actor type identifier', ScriptIdent)
                    try:
                        actor_type_id = actor_types.value(value.value.value)
                    except ValueError:
                        scripterr(script, value, f'unknown actor type `{value.value.value}`')
                    buf.write(pack('>I', actor_type_id))
                elif typ == 'newtarget':
                    checkargtype(script, value, 'identifier or null', ScriptIdent, NoneType)
                    index = 0
                    if value.value is not None:
                        try:
                            index = script_objs.insert(value.value.value, 'actor')
                        except Exception as e:
                            scripterr(script, value, e)
                    buf.write(pack('>H', index))
                elif typ == 'angle':
                    checkargtype(script, value, 'number', float, int)
                    buf.write(pack('>H', degrees_to_ang16(value.value)))
                elif typ == 'fx':
                    checkargtype(script, value, 'string', str)
                    try:
                        gfx_id = assets.index('gfx', value.value)
                    except Exception as e:
                        scripterr(script, value, e)
                    try:
                        tiles_id = assets.index('tileset', value.value)
                    except Exception as e:
                        scripterr(script, value, e)
                    buf.write(pack('>HH', gfx_id, tiles_id))
                else:
                    err(f'unknown type in command definition: {typ}')

            if command.name == 'spawn_actor':
                buf.write(pack('>I', 0))
                actor_bytes = buf.finish()
                try:
                    index = script_actors.index(actor_bytes)
                except ValueError:
                    index = len(script_actors)
                    script_actors.append(actor_bytes)
                script_buf.write(pack('>I', actor_count + index))

        compiled_scripts.append(script_buf.finish())

    # misc map props
    gravity_x = map_props.get('gravity_x') or DEFAULT_GRAVITY[0]
    gravity_y = map_props.get('gravity_y') or map_props.get('gravity') or DEFAULT_GRAVITY[1]
    music_id = 0
    music = map_props.get('music')
    if music is not None:
        if not isinstance(music, Path):
            err('music property must be a file')
        music_id = assets.index('mus', music)

    if camera_start is None:
        if player_actor is not None:
            camera_start = (int(player_actor.coordinates.x), int(player_actor.coordinates.y))
        else:
            camera_start = (214, 120)

    # begin writing
    if args.verbose:
        print('Map Offset', lower_x, lower_y,
              'Size', tmap.map_size.width >> 4, tmap.map_size.height >> 4)

    # HEADER
    buf = util.DataPool(b'TMAP')
    buf.write(pack('>HHHHhhHHHHHH',
                   len(tid_map), len(bgs), len(waypoints), len(scripts),
                   lower_x, lower_y,
                   tmap.map_size.width >> 4, tmap.map_size.height >> 4,
                   len(chunks), len(string_pool),
                   actor_count, actor_count + len(script_actors)))
    actor_buf = buf.write_ref(-3)
    waypoint_buf = buf.write_ref(-4)
    collision_buf = buf.write_ref(-4)
    scripts_buf = buf.write_ref(-5)
    texts_buf = buf.write_ref(-6)
    buf.write(pack('>IIiiiiiIff', music_id, startup_script,
                   int(tmap.parallax_origin.x), int(tmap.parallax_origin.y),
                   camera_start[0], camera_start[1],
                   water_line, water_color, gravity_x, gravity_y))

    # TILESETS
    for firstgid, firsttid in tid_map.items():
        tiles = tmap.tilesets[firstgid]
        endtid = (firsttid + tiles.tile_count + 0b1111) & ~0b1111
        xmask = (tiles.image_width >> 4) - 1
        yshift = int(math.log2(xmask + 1))
        image_id = assets.index('gfx', tiles.image)
        if args.verbose:
            print('Tileset', firstgid, '=> (', firsttid, endtid, '], count', tiles.tile_count, ',',  tiles.image)
        buf.write(pack('>HHBBxxI', firsttid, endtid, xmask, yshift, image_id))

    # BGS
    for layer, bg_layer in bgs:
        properties = layer.properties or {}
        autoscroll_x = float(properties.get('autoscroll_x') or 0)
        autoscroll_y = float(properties.get('autoscroll_y') or 0)
        clear_top = pack_color(properties.get('clear_top'))
        clear_bottom = pack_color(properties.get('clear_bottom'))
        image_id = assets.index('gfx', layer.image)
        anim = (layer.properties or {}).get('anim')
        if isinstance(anim, Path):
            anim = assets.index('tileset', anim)
        else:
            anim = 0
        buf.write(pack('>ffffffIIB??xIIIff',
                       layer.offset.x, layer.offset.y,
                       autoscroll_x, autoscroll_y,
                       layer.parallax_factor.x, layer.parallax_factor.y,
                       clear_top, clear_bottom,
                       bg_layer, layer.repeat_x, layer.repeat_y,
                       image_id, anim, 0, 0, 1))

    # CHUNKS
    for (x, y), (layers, props, fg_layer) in chunks.items():
        if fg_layer is None:
            fg_layer = len(layers)
        chunk_buf = buf.write_ref(0)
        chunk_buf.write(pack('>hhiibbH', x, y, x << 8, y << 8, len(layers), fg_layer, len(props)))
        props_buf = chunk_buf.write_ref(-1)
        if args.verbose:
            print('Chunk at (', x, y, ') with', len(layers), 'layers')
        for layer, obj in reversed(props):
            prop_gid = (obj.gid & 0x0ffffff) - prop_tileset.firstgid
            tile = prop_tileset.tiles[prop_gid]
            anim = (tile.properties or {}).get('anim')
            if isinstance(anim, Path):
                anim = assets.index('tileset', os.path.join('..', anim))
            else:
                anim = 0
            prop_buf = props_buf.write_ref(-2)
            prop_buf.write(pack('>IiiIIIIIIIIIffI',
                                layer,
                                int(obj.coordinates.x), int(obj.coordinates.y),
                                int(obj.size.width), int(obj.size.height),
                                assets.index('gfx', tile.image), anim,
                                0, 0, 0, 0, 0, 0, 1, 0))
            if args.verbose:
                print('  Prop at layer', layer, obj.coordinates, tile.image)
        chunk_buf.write(pack('>I', 0))
        for layer in layers:
            a = array.array('H', layer)
            if sys.byteorder == 'little':
                a.byteswap()
            chunk_buf.write(a.tobytes())

    # ACTOR SPAWNS

    def actor_flags(actor: pytiled_parser.tiled_object.Tile, name: str) -> int:
        if name.startswith('AT_CLIFF_PLATFORM') or name in ['AT_UNDERWATER_PLATFORM']:
            PLATFORM_TYPES = {
               'linear':    0, 'hsine':    1, 'vsine':      2, 'circle':   3,
               'circle-cw': 3, 'cw':       3, 'circle-ccw': 4, 'ccw':      4,
               'swing-90':  5, 'swing-45': 6, 'swing':      6, 'swing-22': 7
            }
            return PLATFORM_TYPES.get(actor.properties.get('type'), 0)
        return 0

    def find_waypoint(oid: int):
        next_iter = filter(lambda p: p[1].id == oid, enumerate(waypoints))
        try:
            return next(next_iter)[0]
        except StopIteration:
            return None

    def write_actor_arg(actor: pytiled_parser.tiled_object.Tile, name: str, buf: util.PoolObj):
        if name.startswith('AT_CLIFF_PLATFORM') or name in ['AT_UNDERWATER_PLATFORM']:
            speed = min(max(int(float(actor.properties.get('speed', 1)) * 16), 0), 0xffff)
            waypoint = 0xffff
            wpid = actor.properties.get('waypoint')
            if wpid is not None:
                waypoint = find_waypoint(int(wpid))
                if waypoint is None:
                    err(f'no such waypoint `{wpid}` in object {actor.id}')
            buf.write(pack('>HH', speed, waypoint))
        else:
            buf.write(pack('>I', 0))

    for tile in actors:
        actor_type_id = actor_types.value(tile.properties['typename'])
        actor_id = script_objs.get('actor', tile.name or None) or 0
        x = int(actor_layer.offset.x + tile.coordinates.x)
        y = int(actor_layer.offset.y + tile.coordinates.y)
        flags = actor_flags(tile, tile.properties['typename'])
        if tile.gid & TMX_FLIPX:
            flags |= AF_FLIPX
        if tile.gid & TMX_FLIPY:
            flags |= AF_FLIPY
        if tile.gid & TMX_FLIPD:
            flags |= AF_FLIPD
        if tile.properties.get('player') == True:
            flags |= AF_CUR_PLAYER
        actor_buf.write(pack('>IiiIHH', actor_type_id, x, y, flags, actor_id,
                             degrees_to_ang16(tile.rotation % 360)))
        write_actor_arg(tile, tile.properties['typename'], actor_buf)
        if args.verbose:
            print('Actor', tile.properties['typename'], 'at (', x, y, ') flags', hex(flags), 'id', actor_id)

    for trigger in triggers:
        x = int(trigger.coordinates.x)
        y = int(trigger.coordinates.y)
        trigger_id = script_objs.get('actor', trigger.name or None) or 0
        flags = 0
        if trigger.properties.get('player') == True:
            flags |= 1<<1
        if trigger.properties.get('enemy') == True:
            flags |= 1<<2
        if trigger.properties.get('prop') == True:
            flags |= 1<<3
        if trigger.properties.get('projectile') == True:
            flags |= 1<<4
        if trigger.properties.get('repeatable') == True:
            flags |= 1<<8
        if trigger.properties.get('manual') == True:
            flags |= 1<<9
        if trigger.properties.get('current-player') == True:
            flags |= 1<<10
        actor_buf.write(pack('>IiiIHH', actor_types.value('AT_TRIGGER'), x, y, flags, trigger_id, 0))
        arg_buf = actor_buf.write_ref(-3)
        arg_buf.write(pack('>I', trigger.properties['trigger']))
        collision = arg_buf.write_ref(-10)
        util.pack_collision_obj(collision, trigger, (-x, -y))
        collision.write(pack('>HH', util.COLL_END, 0))
        if args.verbose:
            print('Trigger', type(trigger), 'at (', x, y, ') flags', hex(flags))

    # WAYPOINTS
    for obj in waypoints:
        next_point = (obj.properties or {}).get('next')
        if next_point is not None:
            next_point = int(next_point)
            if next_point == obj.id:
                err('waypoint cannot have itself as next waypoint')
            next_point = find_waypoint(next_point)
        if next_point is None:
            next_point = 0xffffffff
        waypoint_buf.write(pack('>iiI', int(obj.coordinates.x), int(obj.coordinates.y), next_point))
        if args.verbose:
            print('Waypoint', obj.coordinates, 'next', next_point if next_point != 0xffffffff else 'None')

    # COLLISION
    map_collision.build(collision_buf, os.path.join(orig_dir, path.stem + '.svg') if args.svg_dump else None)

    # SCRIPTS
    for actor in script_actors:
        actor_buf.write(actor)

    for script in compiled_scripts:
        scripts_buf.write_ref(-5).write(script)

    for text in string_pool:
        texts_buf.write_ref(-5).write(bytes(text, 'utf-8') + '\x00')

    os.chdir(orig_dir)
    with open(args.output.joinpath(path.stem + '.map'), 'wb') as f:
        f.write(buf.finish())

def degrees_to_ang16(v: float | int) -> int:
    return min(round((v % 360) / 360 * 65536), 65535)

class ScriptObjPool:
    counters: dict[str, int]
    ids: dict[str, (str, int)]
    def __init__(self):
        self.ids = {}
        self.counters = {}
    def try_get(self, typ: str, name: str) -> int | None:
        res = self.ids.get(name)
        if res is None or res[1] != typ:
            return None
        return res[1]
    def get(self, typ: str, name: str | None) -> int:
        if name is None:
            return 0
        res = self.ids.get(name)
        if res is None or res[0] != typ:
            err(f'no such {typ} `{name}`')
        return res[1]
    def insert(self, name: str, typ: str, oid: int | None = None) -> int:
        res = self.ids.get(name)
        if res is None:
            if oid is None:
                oid = self.counters.setdefault(typ, 0) + 1
                self.counters[typ] = oid
            self.ids[name] = (typ, oid)
            return oid
        elif res[0] != typ:
            err(f'script object `{name}` already exists as {res[0]}')
        return res[0]

class MapCollisionBuilder:
    clip: pyclipr.Clipper
    tile_shapes: dict[int, [tuple[str, numpy.ndarray]]]
    map_size: pytiled_parser.Size
    tile_size: pytiled_parser.Size
    origin: pytiled_parser.OrderedPair
    circles: [numpy.ndarray]
    polylines: [numpy.ndarray]

    def __init__(self, tmap: pytiled_parser.TiledMap, tilesets: list[pytiled_parser.Tileset]):
        self.clip = pyclipr.Clipper()
        self.clip.scaleFactor = 0x100000000
        self.clip.preserveCollinear = False
        self.tile_shapes = {}
        self.map_size = tmap.map_size
        self.tile_size = tmap.tile_size
        self.origin = pytiled_parser.OrderedPair(0, 0)
        self.circles = []
        self.polylines = []

        for tileset in tilesets:
            tw = tileset.tile_width
            th = tileset.tile_height
            image = Image.open(tileset.image)
            band = 0
            tvalue = None
            if image.mode == 'RGBA':
                band = 3
                if image.getextrema()[3][0] == 0:
                    tvalue = 0
            elif image.mode == 'P':
                tvalue = image.info.get('transparency')
            for tid in range(tileset.tile_count):
                tile = tileset.tiles and tileset.tiles.get(tid)
                shapes = []
                collide = tile and (tile.properties or {}).get('collide')
                if collide != False:
                    if tile and tile.objects and len(tile.objects.tiled_objects) > 0:
                        for obj in tile.objects.tiled_objects:
                            shape = tiled_object_to_shape(obj)
                            if shape:
                                shapes.append(shape)
                    else:
                        if collide is None:
                            if tvalue is None:
                                collide = True
                            else:
                                x = (tid % tileset.columns) * tw
                                y = int(tid / tileset.columns) * th
                                tiledata = image.copy().crop((x, y, x+tw, y+th)).getdata(band)
                                collide = any(a != tvalue for a in tiledata)
                        if collide == True:
                            shapes.append(('polygon', numpy.array([(0, 0), (tw, 0), (tw, th), (0, th)])))
                self.tile_shapes[tid + tileset.firstgid] = shapes

    def add_chunk(self, layer: pytiled_parser.Layer, chunk: pytiled_parser.Chunk):
        self.origin = pytiled_parser.OrderedPair(
                min(self.origin.x, chunk.coordinates.x),
                min(self.origin.y, chunk.coordinates.y))
        for y in range(int(chunk.size.height)):
            for x in range(int(chunk.size.width)):
                tid = chunk.data[y][x]
                shapes = self.tile_shapes.get(tid & 0x0fffffff, [])
                offset = (layer.offset.x + (chunk.coordinates.x + x) * self.tile_size.width,
                          layer.offset.y + (chunk.coordinates.y + y) * self.tile_size.height)
                for typ, points in shapes:
                    if typ == 'circle':
                        self.circles.append(numpy.array([points[0], points[1] + offset[0], points[2] + offset[1]]))
                    else:
                        if tid & TMX_FLIPD:
                            points = numpy.array([(y, x) for x, y in points])
                        if tid & TMX_FLIPX:
                            points = numpy.array([(self.tile_size.width - x, y) for x, y in points])
                        if tid & TMX_FLIPY:
                            points = numpy.array([(x, self.tile_size.height - y) for x, y in points])

                        if typ == 'polygon':
                            self.clip.addPath(points + offset, pyclipr.PathType.Subject)
                        elif typ == 'polyline':
                            self.polylines.append(points + offset)

    def add_object(self, obj: pytiled_parser.tiled_object.TiledObject):
        if isinstance(obj, pytiled_parser.tiled_object.Ellipse):
            self.circles.append(tiled_object_to_shape(obj)[1])
        elif isinstance(obj, pytiled_parser.tiled_object.Polyline):
            self.polylines.append(tiled_object_to_shape(obj)[1])
        else:
            shape = tiled_object_to_shape(obj)
            if shape:
                self.clip.addPath(shape[1], pyclipr.PathType.Subject)

    def build(self, buf: util.PoolObj, svg_path: Optional[Path]) -> bytes:
        paths = self.clip.execute(pyclipr.Union, pyclipr.EvenOdd)

        if svg_path:
            svg = open(svg_path, 'w')
            mx, my = self.origin
            mw, mh = self.map_size
            mx *= self.tile_size.width
            my *= self.tile_size.height
            mw *= self.tile_size.width
            mh *= self.tile_size.height
            svg.write(f'<svg viewBox="{mx} {my} {mw} {mh}" xmlns="http://www.w3.org/2000/svg">')
            svg.write('<path fill="green" stroke="black" d="')
            for path in paths:
                cmd = 'M'
                for x, y in path:
                    svg.write(f'{cmd} {x} {y} ')
                    cmd = 'L'
                svg.write('Z ')
            svg.write('"/>')
            for polyline in self.polylines:
                svg.write('<path fill="none" stroke="black" d="')
                cmd = 'M'
                for x, y in polyline:
                    svg.write(f'{cmd} {x} {y} ')
                    cmd = 'L'
                svg.write('"/>')
            for circ in self.circles:
                svg.write(f'<circle fill="green" stroke="black" r="{circ[0]}" cx="{circ[1]}" cy="{circ[2]}"/>')
            svg.write('</svg>')

        for path in paths:
            buf.write(util.pack_collision_points(path, 'polygon'))
        for polyline in self.polylines:
            buf.write(util.pack_collision_points(polyline, 'polyline'))
        for points in self.circles:
            buf.write(util.pack_collision_points(points, 'circle'))

        buf.write(pack('>HH', util.COLL_END, 0))

def tiled_object_to_shape(obj: pytiled_parser.tiled_object.TiledObject) -> Optional[tuple[str, numpy.ndarray]]:
    if isinstance(obj, pytiled_parser.tiled_object.Ellipse):
        if obj.size.width != obj.size.height:
            err(f'Ellipse collision object {obj.id} is not a circle')
        rad = obj.size.width / 2
        return ('circle', numpy.array([rad, obj.coordinates.x + rad, obj.coordinates.y + rad]))

    typ = None
    points = None

    if isinstance(obj, pytiled_parser.tiled_object.Rectangle):
        typ = 'polygon'
        ox, oy = obj.coordinates
        x0, y0 = obj.coordinates
        x1 = x0 + obj.size.width
        y1 = y0 + obj.size.height
        points = [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]
    elif isinstance(obj, pytiled_parser.tiled_object.Polygon):
        typ = 'polygon'
        ox, oy = obj.coordinates
        points = [(p.x + ox, p.y + oy) for p in obj.points]
    elif isinstance(obj, pytiled_parser.tiled_object.Polyline):
        typ = 'polyline'
        ox, oy = obj.coordinates
        points = [(p.x + ox, p.y + oy) for p in obj.points]

    if typ is None:
        return None

    if obj.rotation:
        points = [util.rotate_point(x, y, obj.rotation, ox, oy) for x, y in points]

    return (typ, numpy.array(points))

process_map(args.filename)

#!/usr/bin/env python3

import argparse, itertools, math, pytiled_parser, util
from pathlib import Path
from struct import pack
from util import err

parser = argparse.ArgumentParser(prog='Tile Definition Builder Tool')
parser.add_argument('filename', type=Path)
parser.add_argument('-o', '--output', type=Path, default=Path('.'))
parser.add_argument('-v', '--verbose', action='store_true')

args = parser.parse_args()

def process_tiles(path):
    tiles = pytiled_parser.parse_tileset(path)
    offset = tiles.tile_offset or pytiled_parser.OrderedPair(0, 0)
    props = tiles.properties or {}
    buf = util.DataPool(b'TILE')
    xmask = 0
    xshift = 0
    yshift = 0
    if tiles.image and tiles.image_width & (tiles.image_width - 1) == 0 \
            and tiles.tile_width & (tiles.tile_width - 1) == 0 \
            and tiles.tile_height & (tiles.tile_height - 1) == 0:
        yshift = int(math.log2(tiles.tile_height)) - int(math.log2(tiles.columns))
        if yshift < 0:
            yshift = 0
        else:
            xmask = tiles.columns - 1
            xshift = int(math.log2(tiles.tile_width))
    buf.write(pack('>iiHHHHHH', int(offset.x), int(offset.y),
                   tiles.tile_width, tiles.tile_height,
                   tiles.tile_count,
                   xmask, xshift, yshift))
    if args.verbose:
        print('Tile Count', tiles.tile_count, 'Height', tiles.tile_height, 'Offset', offset)
    anims = {}
    for tile in (tiles.tiles or {}).values():
        for i, frame in enumerate(tile.animation or []):
            if frame.tile_id in anims:
                err(f'tile id {frame.tile_id} in two animations')
            next_index = 0 if i + 1 >= len(tile.animation) else i + 1
            anims[frame.tile_id] = (frame.duration, tile.animation[next_index].tile_id)
    for tid in range(tiles.tile_count):
        tile = tiles.tiles.get(tid) if tiles.tiles else None
        props = (tile and tile.properties) or {}
        collision = buf.write_ref()
        if tile and tile.objects and props.get('collide') != False:
            for obj in tile.objects.tiled_objects:
                util.pack_collision_obj(collision, obj, offset)
        collision.write(pack('>HH', util.COLL_END, 0))
        duration, next_id = anims.get(tid) or (0, 0)
        if duration == 10000:
            duration = 0
        else:
            duration = int(duration / 1000 * 60)
        buf.write(pack('>HH', duration, next_id))
    with open(args.output.joinpath(path.stem + '.tiles'), 'wb') as f:
        f.write(buf.finish())

process_tiles(args.filename)

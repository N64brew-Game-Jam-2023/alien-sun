#!/usr/bin/env python3

import argparse
from pathlib import Path

parser = argparse.ArgumentParser(prog='Generate C heder with asset IDs and paths')
parser.add_argument('output', type=Path)
parser.add_argument('name', type=str)
parser.add_argument('filenames', type=Path, nargs='+')
parser.add_argument('-H', '--header', action='store_true')

args = parser.parse_args()

args.filenames.sort(key=lambda p: p.name)
name_upper = args.name.upper()

f = open(args.output, 'a')

if args.header:
    f.write('typedef enum {\n')
    f.write(f'  {name_upper}_NONE,\n')
    for filename in args.filenames:
        id = filename.stem.upper().replace('-', '_')
        f.write(f'  {name_upper}_{id},\n')
    f.write(f'  NUM_{name_upper}\n')
    f.write(f'}} {args.name}_id_t;\n\n')
    f.write(f'extern const char * const {args.name}_paths[NUM_{name_upper}];\n\n')
else:
    f.write(f'const char * const {args.name}_paths[NUM_{name_upper}] = {{\n')
    f.write('  (void *) 0,\n')
    for filename in args.filenames:
        f.write(f'  "rom:/{filename}",\n')
    f.write('};\n\n')

f.close()


from dataclasses import dataclass
from parsy import eof, line_info, regex, seq, string, success

@dataclass
class ScriptIdent:
    value: str

@dataclass
class ScriptSpecial:
    value: str

@dataclass
class ScriptDirective:
    value: str

@dataclass(init=False)
class ScriptColor:
    r: int
    g: int
    b: int
    a: int
    def __init__(self, r: int | float, g: int | float, b: int | float, a: int | float):
        if isinstance(r, float):
            r = round(r * 255)
        if isinstance(g, float):
            g = round(g * 255)
        if isinstance(b, float):
            b = round(b * 255)
        if isinstance(a, float):
            a = round(a * 255)
        self.r = min(max(r, 0), 255)
        self.g = min(max(g, 0), 255)
        self.b = min(max(b, 0), 255)
        self.a = min(max(a, 0), 255)
    def int_value(self):
        return (self.r << 24) | (self.g << 16) | (self.b << 8) | self.a

@dataclass
class ScriptValue:
    pos: (int, int)
    value: None | int | float | str | ScriptIdent | ScriptSpecial
    def type_name(self):
        if self.value is None:
            return 'null'
        typ = type(self.value)
        if typ == int:
            return 'int'
        if typ == float:
            return 'float'
        if typ == str:
            return 'string'
        if typ == ScriptIdent:
            return 'identifier'
        if typ == ScriptIdent:
            return '@' + self.value.value
        raise RuntimeError('unknown type')
    def is_special(self, spec: str):
        return type(self.value) == ScriptSpecial and self.value.value == spec

class ScriptFuncArgs:
    args: list[ScriptValue]
    kwargs: dict[str, ScriptValue]
    def __init__(self, *args: list[ScriptValue | tuple[str, ScriptValue]]):
        self.args = []
        self.kwargs = {}
        for arg in args:
            if isinstance(arg, tuple):
                self.kwargs[arg[0]] = arg[1]
            else:
                self.args.append(arg)
    def __len__(self):
        return len(self.args) + len(self.kwargs)
    def __repr__(self):
        return f'ScriptFuncArgs(args={self.args}, kwargs={self.kwargs})'
    def __getitem__(self, b: int | str):
        if isinstance(b, str):
            return self.kwargs[b]
        else:
            return self.args[b]
    def __contains__(self, b: int | str):
        if isinstance(b, str):
            return b in self.kwargs
        else:
            return b < len(self.args)

@dataclass
class ScriptCommand:
    pos: tuple[int, int]
    name: str
    args: ScriptFuncArgs = ScriptFuncArgs()

@dataclass
class ScriptFunc:
    pos: tuple[int, int]
    attributes: list[str]
    commands: list[ScriptCommand]
    name: str | None = None
    singleton: bool = False
    source = None

single_line_comment = regex(r"//.*[\r\n$]+\s*")
multi_line_comment = regex(r"/[*]([^*]|([*][^/]))*[*]/\s*")
comment = (single_line_comment | multi_line_comment).many()

whitespace = regex(r"\s*") | comment
lexeme = lambda p: p << whitespace

lparen = lexeme(string("("))
rparen = lexeme(string(")"))
lbrace = lexeme(string("{"))
rbrace = lexeme(string("}"))
lbrack = lexeme(string("["))
rbrack = lexeme(string("]"))
comma = lexeme(string(","))
semicolon = lexeme(string(";"))
equals = lexeme(string("="))
null = lexeme(string("null"))

ident_chars = regex(r"[_a-zA-Z][_a-zA-Z0-9]*")
ident = lexeme(ident_chars)
floatnumber = lexeme(regex(r"-?\s*(0|[1-9][0-9]*)(([.][0-9]+)|([eE][+-]?[0-9]+))")).map(float)
decnumber = lexeme(regex(r"-?\s*(0|[1-9][0-9]*)")).map(int)
hexnumber = lexeme(regex(r"-?\s*0x[0-9a-fA-F]+")).map(lambda x: int(x, 0))
binnumber = lexeme(regex(r"-?\s*0b[0-1]+")).map(lambda x: int(x, 0))
number = hexnumber | binnumber | floatnumber | decnumber
color = lexeme(string("color")) >> lparen >> seq(number << comma, number << comma, number << comma, number).combine(ScriptColor) << rparen
string_part = regex(r'[^"\\]+')
string_esc = string("\\") >> (
        string("\\")
        | string("/")
        | string('"')
        | string("b").result("\b")
        | string("f").result("\f")
        | string("n").result("\n")
        | string("r").result("\r")
        | string("t").result("\t")
        | regex(r"u[0-9a-fA-F]{4}").map(lambda s: chr(int(s[1:], 16)))
        )
quoted = lexeme(string('"') >> (string_part | string_esc).many().concat() << string('"'))

attrib = lbrack >> ident << rbrack
outer_attrib = string("#") >> attrib
inner_attrib = string("#!") >> attrib
literal = number | quoted | color | null
special = (string("@") >> ident).map(ScriptSpecial)
value = seq(pos=line_info, value=literal | special | ident.map(ScriptIdent)).combine_dict(ScriptValue)
kwarg = seq(ident << equals, value).map(tuple)
func_arg = kwarg | value
stmt = seq(pos=line_info, name=ident,
           args=(lparen >> func_arg.sep_by(comma).combine(ScriptFuncArgs) << rparen) | success([])
           ).combine_dict(ScriptCommand) << semicolon
func_body = stmt.many()
func = seq(pos=line_info,
           attributes=outer_attrib.many(),
           name=lexeme(string("script")) >> ident << lparen << rparen << lbrace,
           commands=stmt.many() << rbrace
           ).combine_dict(ScriptFunc)

script_parser = whitespace >> func.many()
inline_script_parser = whitespace >> seq(
        pos=line_info,
        attributes=inner_attrib.many(),
        commands=stmt.many()
        ).combine_dict(ScriptFunc)

if __name__ == "__main__":
    import sys
    stream = open(sys.argv[1]) if len(sys.argv) > 1 else sys.stdin
    print(repr(script_parser.parse(stream.read())))

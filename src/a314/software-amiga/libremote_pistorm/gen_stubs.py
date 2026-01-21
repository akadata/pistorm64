import json
import sys

in_file = f'libdecl-{sys.argv[1]}.json'

with open(in_file, 'rt') as f:
    libdecl = json.load(f)

funcs = libdecl['funcs']

contents = '#include "messages.h"\n\n'

contents += f'#define LIBRARY_NAME "{libdecl["library-name"]}"\n'
contents += f'#define SERVICE_NAME "{libdecl["service-name"]}"\n\n'

contents += '''struct LibRemote;
static ULONG null_func();
static BPTR expunge();
static BPTR close(struct LibRemote *lib __asm("a6"));
static ULONG send_request(struct LibRemote *lib __asm("a6"), UBYTE *write_buf, ULONG write_length);

'''

contents += f'#define LVO_COUNT {len(funcs) + 4}\n\n'

def gen_param(reg):
    if reg[0] == 'd':
        return f'ULONG {reg} __asm("{reg}")'
    else:
        return f'void *{reg} __asm("{reg}")'

for i, func in enumerate(funcs):
    args = func.get('args', [])
    contents += f'static ULONG func_{i}(struct LibRemote *lib __asm("a6")'
    if len(args):
        contents += ', ' + ', '.join(gen_param(reg) for reg, _, _ in args)
    contents += ')\n{\n'
    contents += f'    UBYTE write_buf[256];\n'
    for j, (reg, _, _) in enumerate(args):
        contents += f'    *(ULONG *)&write_buf[{2 + j*4}] = (ULONG){reg};\n'
    contents += '    write_buf[0] = MSG_OP_REQ;\n'
    contents += f'    write_buf[1] = {i};\n'
    contents += f'    return send_request(lib, write_buf, {2 + len(args)*4});\n'
    contents += '}\n\n'

contents += '''static ULONG funcs_vector[] =
{
    (ULONG)null_func,
    (ULONG)close,
    (ULONG)expunge,
    (ULONG)null_func,
'''

for i, _ in enumerate(funcs):
    contents += f'    (ULONG)func_{i},\n'

contents += '''};

'''

contents += '''static void fill_lvos(struct LibRemote *lib __asm("a6"))
{
    for (int i = 0; i < LVO_COUNT; i++)
    {
        UBYTE *lvo = (UBYTE *)lib - ((i + 1) * 6);
        *(UWORD *)lvo = 0x4ef9;
        *(ULONG *)&lvo[2] = funcs_vector[i];
    }
}
'''

with open('stubs.h', 'wt') as f:
    f.write(contents)

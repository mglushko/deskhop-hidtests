"""HID report descriptor items, and the corpus that holds them, shared by the tools.

The one item walker and the one reader of descriptors.h, replacing copies in
add_descriptor.py, corpus_table.py and emu/gen_desc.py that disagreed on long items and
on a 0xNN inside a comment.

    for item in walk_items(data): ...
    corpus = read_corpus(open("descriptors.h").read())   # "d_name" -> [bytes]
"""
import re
from collections import namedtuple

# offset is the index of the prefix byte and end the index just past the item, so for the
# last item of a well formed descriptor end == len(data). tag is the prefix with its size
# bits cleared, so 0xA0 is Collection and 0xC0 End Collection whatever their data length,
# and LONG_ITEM for a long item, which no short tag can equal. value is the data read
# little-endian, None for a long item, whose data has no single meaning.
Item = namedtuple("Item", "offset end tag size value")

LONG_ITEM = 0xFE


def walk_items(data):
    """Yield one Item per item. A final item cut short by the end of the data is still
    yielded, with end past len(data), so a caller comparing the last end against
    len(data) sees the truncation rather than a clean walk."""
    i = 0
    while i < len(data):
        p = data[i]
        if p == LONG_ITEM:
            size = data[i + 1] if i + 1 < len(data) else 0
            yield Item(i, i + 3 + size, LONG_ITEM, size, None)
            i += 3 + size
            continue
        size = p & 3
        size = 4 if size == 3 else size
        yield Item(i, i + 1 + size, p & 0xFC, size,
                   int.from_bytes(bytes(data[i + 1:i + 1 + size]), "little"))
        i += 1 + size


ARRAY = re.compile(r"static const uint8_t (d_\w+)\[\]\s*=\s*\{(.*?)\}\s*;", re.S)
BYTE = re.compile(r"0[xX]([0-9A-Fa-f]{1,2})")

# One alternation, so a // comment holding /* cannot open a block that swallows the next
# line, and a space in place of the comment so the tokens on either side cannot fuse.
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)


def strip_comments(text):
    """C source with its comments replaced by a space."""
    return COMMENT.sub(" ", text)


def read_corpus(text):
    """Every descriptor array in descriptors.h's text, as C name -> list of bytes. The
    comments go first, over the whole text, so a 0xNN or a }; inside one is prose."""
    return {name: [int(x, 16) for x in BYTE.findall(body)]
            for name, body in ARRAY.findall(strip_comments(text))}

"""HID report descriptor items, and the corpus that holds them, shared by the tools.

Four copies of the item-size decode used to live in add_descriptor.py, corpus_table.py
and emu/gen_desc.py, one of which knew about long items, and two readers of descriptors.h
disagreed about a comment inside an array: one stripped comments first, the other took
any 0xNN it found. This is the one walker and the one reader.

    for item in walk_items(data): ...
    corpus = read_corpus(open("descriptors.h").read())   # "d_name" -> [bytes]
"""
import re
from collections import namedtuple

# offset is the index of the prefix byte and end the index just past the item, so for the
# last item of a well formed descriptor end == len(data). tag is the prefix with its size
# bits cleared, so 0xA0 is Collection and 0xC0 End Collection whatever their data length.
# typ is 0 main, 1 global, 2 local, 3 long. value is the data read little-endian, None for
# a long item, whose data has no single meaning.
Item = namedtuple("Item", "offset end prefix tag typ size value")

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
            tag = data[i + 2] if i + 2 < len(data) else 0
            yield Item(i, i + 3 + size, p, tag, 3, size, None)
            i += 3 + size
            continue
        size = p & 3
        size = 4 if size == 3 else size
        yield Item(i, i + 1 + size, p, p & 0xFC, (p >> 2) & 3, size,
                   int.from_bytes(bytes(data[i + 1:i + 1 + size]), "little"))
        i += 1 + size


ARRAY = re.compile(r"static const uint8_t (d_\w+)\[\]\s*=\s*\{(.*?)\};", re.S)
BYTE = re.compile(r"0[xX]([0-9A-Fa-f]{1,2})")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def read_corpus(text):
    """Every descriptor array in descriptors.h's text, as C name -> list of bytes. The
    comments go first, so a 0xNN inside one is prose rather than a byte of the device."""
    return {name: [int(x, 16) for x in BYTE.findall(strip_comments(body))]
            for name, body in ARRAY.findall(text)}

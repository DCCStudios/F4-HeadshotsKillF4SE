#!/usr/bin/env python3
"""
Walk Fallout4.esm and dump every IMAD (TESImageSpaceModifier) record.
Output: FormID, EditorID, Duration, Animatable flag.
"""

import io
import os
import struct
import sys
import zlib
from pathlib import Path

REC_HDR_FMT = "<4sIIIHHHH"
REC_HDR_LEN = struct.calcsize(REC_HDR_FMT)
GRP_HDR_FMT = "<4sI4sIHHHH"
GRP_HDR_LEN = struct.calcsize(GRP_HDR_FMT)
SUB_HDR_FMT = "<4sH"
SUB_HDR_LEN = struct.calcsize(SUB_HDR_FMT)

REC_FLAG_COMPRESSED = 0x00040000


def parse_subrecords(buf: bytes):
    s = io.BytesIO(buf)
    pending_size = None
    while True:
        head = s.read(SUB_HDR_LEN)
        if len(head) < SUB_HDR_LEN:
            return
        st, sz = struct.unpack(SUB_HDR_FMT, head)
        if st == b"XXXX" and sz == 4:
            (pending_size,) = struct.unpack("<I", s.read(4))
            continue
        if pending_size is not None:
            sz = pending_size
            pending_size = None
        data = s.read(sz)
        yield st, data


def walk_records(buf: bytes, want_type: bytes):
    s = io.BytesIO(buf)
    while True:
        head = s.read(4)
        if len(head) < 4:
            return
        s.seek(-4, os.SEEK_CUR)
        if head == b"GRUP":
            ghead = s.read(GRP_HDR_LEN)
            (_, gsize, glabel, gtype, *_rest) = struct.unpack(GRP_HDR_FMT, ghead)
            inner = s.read(gsize - GRP_HDR_LEN)
            if glabel == want_type and gtype == 0:
                yield from iter_records_inner(inner, want_type)
            else:
                yield from walk_records(inner, want_type)
        else:
            rhead = s.read(REC_HDR_LEN)
            if len(rhead) < REC_HDR_LEN:
                return
            (rtype, dsize, flags, fid, *_rest) = struct.unpack(REC_HDR_FMT, rhead)
            rdata = s.read(dsize)
            if rtype == want_type:
                if flags & REC_FLAG_COMPRESSED:
                    decomp_size = struct.unpack("<I", rdata[:4])[0]
                    rdata = zlib.decompress(rdata[4:], -15, decomp_size)
                yield fid, rdata


def iter_records_inner(buf: bytes, want_type: bytes):
    s = io.BytesIO(buf)
    while True:
        head = s.read(4)
        if len(head) < 4:
            return
        s.seek(-4, os.SEEK_CUR)
        if head == b"GRUP":
            ghead = s.read(GRP_HDR_LEN)
            (_, gsize, *_rest) = struct.unpack(GRP_HDR_FMT, ghead)
            inner = s.read(gsize - GRP_HDR_LEN)
            yield from iter_records_inner(inner, want_type)
        else:
            rhead = s.read(REC_HDR_LEN)
            if len(rhead) < REC_HDR_LEN:
                return
            (rtype, dsize, flags, fid, *_rest) = struct.unpack(REC_HDR_FMT, rhead)
            rdata = s.read(dsize)
            if rtype == want_type:
                if flags & REC_FLAG_COMPRESSED:
                    decomp_size = struct.unpack("<I", rdata[:4])[0]
                    rdata = zlib.decompress(rdata[4:], -15, decomp_size)
                yield fid, rdata


def extract_imods(plugin_path: Path):
    data = plugin_path.read_bytes()
    # Skip TES4 header record
    (_, hdr_size, *_rest) = struct.unpack(REC_HDR_FMT, data[:REC_HDR_LEN])
    buf = data[REC_HDR_LEN + hdr_size:]

    results = []
    for fid, rdata in walk_records(buf, b"IMAD"):
        edid = ""
        duration = 0.0
        animatable = False

        for st, sdata in parse_subrecords(rdata):
            if st == b"EDID":
                edid = sdata.rstrip(b"\x00").decode("utf-8", errors="replace")
            elif st == b"DNAM":
                if len(sdata) >= 8:
                    anim_byte = sdata[0]
                    animatable = bool(anim_byte)
                    duration = struct.unpack_from("<f", sdata, 4)[0]

        results.append((fid, edid, duration, animatable))

    return results


def main():
    if len(sys.argv) < 2:
        esm_path = Path(r"f:\Modlists\LoreOut\Stock Game\Data\Fallout4.esm")
        if not esm_path.exists():
            esm_path = Path(r"E:\SteamLibrary\steamapps\common\Fallout 4\Data\Fallout4.esm")
    else:
        esm_path = Path(sys.argv[1])

    if not esm_path.exists():
        print(f"ERROR: File not found: {esm_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Parsing: {esm_path}")
    print(f"{'FormID':<12} {'EditorID':<50} {'Duration':>10} {'Animatable'}")
    print("-" * 90)

    results = extract_imods(esm_path)
    results.sort(key=lambda r: r[1].lower())

    for fid, edid, duration, animatable in results:
        print(f"{fid:08X}     {edid:<50} {duration:>10.2f} {'Yes' if animatable else 'No'}")

    print(f"\nTotal IMAD records: {len(results)}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Walk Fallout 4 ESP/ESM/ESL files and dump every AMMO record's EditorID,
DATA damage value, full name (if present), and projectile FormID.

Bethesda plugin layout (FO4 = same envelope as FO3/FNV with FO4 record sizes):

  TES4   record at offset 0
  GRUP*  groups, each containing records of one type
    record:  TYPE(4) dataSize(4) flags(4) formID(4) timestamp(2) vc(2) ver(2) unk(2)
             then dataSize bytes of subrecords. If flags & 0x00040000, those
             dataSize bytes are zlib-compressed; the first 4 bytes after the
             record header are the *uncompressed* size (uint32 LE), and the
             remaining (dataSize - 4) bytes are the zlib stream.
    subrec:  TYPE(4) size(2) data(size)
             Special case: if size == 0 AND TYPE == 'XXXX', the *next*
             subrecord's size is overridden by the 4-byte payload of XXXX.

We just need EDID (EditorID, null-terminated string), and ideally DATA / FULL /
DNAM (NamedProjectile pointer) for nicer reporting -- but FO4 AMMO subrecord
layout differs slightly. We only require EDID for the JSON.
"""

import io
import os
import struct
import sys
import zlib
from pathlib import Path


REC_HDR_FMT = "<4sIIIHHHH"      # type, dataSize, flags, formID, ts, vc, ver, unk
REC_HDR_LEN = struct.calcsize(REC_HDR_FMT)
GRP_HDR_FMT = "<4sI4sIHHHH"     # 'GRUP', size, label, type, ts, vc, ver, unk
GRP_HDR_LEN = struct.calcsize(GRP_HDR_FMT)
SUB_HDR_FMT = "<4sH"
SUB_HDR_LEN = struct.calcsize(SUB_HDR_FMT)

REC_FLAG_COMPRESSED = 0x00040000


def parse_subrecords(buf: bytes):
    """Yield (sub_type, sub_data) tuples from a record's payload."""
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
    """Iterate every record of the requested type inside buf (a TES file body
    excluding the TES4 header). Recurses into nested groups."""
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
            # Top-level groups for AMMO have label == b'AMMO' and gtype == 0
            if glabel == want_type and gtype == 0:
                yield from iter_records_inner(inner, want_type)
            else:
                # Could be a child group; descend anyway in case AMMO is nested.
                yield from walk_records(inner, want_type)
        else:
            # A bare record (rare at top level after groups exist).
            rhead = s.read(REC_HDR_LEN)
            (rtype, dsize, flags, formID, *_rest) = struct.unpack(REC_HDR_FMT, rhead)
            payload = s.read(dsize)
            if rtype == want_type:
                yield (formID, flags, payload)


def iter_records_inner(buf: bytes, want_type: bytes):
    s = io.BytesIO(buf)
    while True:
        peek = s.read(4)
        if len(peek) < 4:
            return
        s.seek(-4, os.SEEK_CUR)

        if peek == b"GRUP":
            ghead = s.read(GRP_HDR_LEN)
            (_, gsize, *_rest) = struct.unpack(GRP_HDR_FMT, ghead)
            inner = s.read(gsize - GRP_HDR_LEN)
            yield from iter_records_inner(inner, want_type)
            continue

        rhead = s.read(REC_HDR_LEN)
        if len(rhead) < REC_HDR_LEN:
            return
        (rtype, dsize, flags, formID, *_rest) = struct.unpack(REC_HDR_FMT, rhead)
        payload = s.read(dsize)
        if len(payload) < dsize:
            return
        if rtype == want_type:
            if flags & REC_FLAG_COMPRESSED:
                # First 4 bytes = uncompressed size
                if dsize >= 4:
                    body = zlib.decompress(payload[4:])
                else:
                    body = b""
            else:
                body = payload
            yield (formID, flags, body)


def parse_plugin(path: Path):
    """Return list of (editorID, formID, ammo_damage_or_None, full_name_or_None) for AMMO records."""
    raw = path.read_bytes()
    s = io.BytesIO(raw)

    # TES4 header is itself a record at offset 0.
    head = s.read(REC_HDR_LEN)
    rtype, dsize, flags, *_rest = struct.unpack(REC_HDR_FMT, head)
    if rtype != b"TES4":
        raise RuntimeError(f"{path.name}: not a TES4 plugin (got {rtype!r})")
    s.read(dsize)  # skip TES4 payload

    body = s.read()
    out = []
    for (formID, _flags, payload) in walk_records(body, b"AMMO"):
        edid = None
        full = None
        damage = None
        for st, data in parse_subrecords(payload):
            if st == b"EDID":
                edid = data.rstrip(b"\x00").decode("latin-1", errors="replace")
            elif st == b"FULL":
                full = data.rstrip(b"\x00").decode("latin-1", errors="replace")
            elif st == b"DATA":
                # FO4 AMMO DATA: projectile (formID,4) flags (1) damage (f4) value (i4)
                # Several variants exist; we just try to read damage as float at offset 5
                if len(data) >= 9:
                    try:
                        (damage,) = struct.unpack_from("<f", data, 5)
                    except struct.error:
                        damage = None
        if edid is not None:
            out.append((edid, formID, damage, full))
    return out


def main(argv):
    if len(argv) < 2:
        print("usage: extract_ammo.py <plugin>...")
        return 2
    for fname in argv[1:]:
        p = Path(fname)
        if not p.exists():
            print(f"!! missing: {fname}")
            continue
        try:
            entries = parse_plugin(p)
        except Exception as e:  # noqa: BLE001
            print(f"!! {fname}: {e}")
            continue
        print(f"\n=== {p.name}  ({len(entries)} AMMO records) ===")
        for edid, fid, dmg, full in sorted(entries, key=lambda x: x[0].lower()):
            tag = f"  dmg={dmg:.1f}" if dmg is not None else ""
            n = f'  "{full}"' if full else ""
            print(f"  {edid:<48s} 0x{fid:08X}{tag}{n}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

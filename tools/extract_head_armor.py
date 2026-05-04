#!/usr/bin/env python3
"""
Walk Fallout 4 plugin files and dump every ARMO record whose BipedObjectSlots
mask covers any head slot (30, 31, 32 = bits 0, 1, 2).

Output includes EditorID, FormID, armor rating, slot mask, and full name.
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

HEAD_SLOT_MASK = 0x07  # bits 0,1,2 = slots 30,31,32


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
                if dsize >= 4:
                    body = zlib.decompress(payload[4:])
                else:
                    body = b""
            else:
                body = payload
            yield (formID, flags, body)


def check_localized(path: Path) -> bool:
    """Check if the plugin uses localized strings (flag bit 7 in TES4 header flags)."""
    raw = path.read_bytes()
    s = io.BytesIO(raw)
    head = s.read(REC_HDR_LEN)
    _, _, flags, *_ = struct.unpack(REC_HDR_FMT, head)
    return bool(flags & 0x00000080)


def parse_plugin(path: Path, debug=False):
    """Return list of head armor: (editorID, formID, ballisticAR, energyAR, slotMask, fullName, keywords, health)."""
    raw = path.read_bytes()
    s = io.BytesIO(raw)
    localized = check_localized(path)

    head = s.read(REC_HDR_LEN)
    rtype, dsize, flags, *_rest = struct.unpack(REC_HDR_FMT, head)
    if rtype != b"TES4":
        raise RuntimeError(f"{path.name}: not a TES4 plugin (got {rtype!r})")
    s.read(dsize)

    body = s.read()
    out = []
    for (formID, _flags, payload) in walk_records(body, b"ARMO"):
        edid = None
        full = None
        slot_mask = None
        kwda_formids = []
        dama_pairs = {}  # keyword_formID -> uint32 value
        data_value = None
        data_weight = None
        fnam_health = None

        for st, data in parse_subrecords(payload):
            if st == b"EDID":
                edid = data.rstrip(b"\x00").decode("latin-1", errors="replace")
            elif st == b"FULL":
                if localized and len(data) == 4:
                    (str_idx,) = struct.unpack_from("<I", data, 0)
                    full = f"[LSTR:{str_idx}]"
                else:
                    full = data.rstrip(b"\x00").decode("utf-8", errors="replace")
            elif st == b"DAMA":
                # FO4: pairs of (DamageType keyword FormID: uint32, value: uint32)
                for i in range(0, len(data), 8):
                    if i + 8 <= len(data):
                        kw_fid, val = struct.unpack_from("<II", data, i)
                        dama_pairs[kw_fid] = val
            elif st == b"DATA":
                if len(data) >= 4:
                    (data_value,) = struct.unpack_from("<I", data, 0)
                if len(data) >= 8:
                    (data_weight,) = struct.unpack_from("<f", data, 4)
            elif st == b"FNAM":
                if len(data) >= 4:
                    (fnam_health,) = struct.unpack_from("<I", data, 0)
            elif st == b"BOD2":
                if len(data) >= 4:
                    (slot_mask,) = struct.unpack_from("<I", data, 0)
            elif st == b"KWDA":
                for i in range(0, len(data), 4):
                    if i + 4 <= len(data):
                        (kw_fid,) = struct.unpack_from("<I", data, i)
                        kwda_formids.append(kw_fid)

        if edid is None:
            continue
        if slot_mask is None:
            continue
        if not (slot_mask & HEAD_SLOT_MASK):
            continue
        # Skip "Skin" entries (creature body skins, not real armor)
        if edid.startswith("Skin"):
            continue

        # Known FO4 damage type keywords (from Fallout4.esm):
        # 0x00060A81 = dtPhysical (ballistic)
        # 0x00060A85 = dtEnergy
        # 0x00060A87 = dtRadiationExposure
        # 0x00060A89 = dtPoison
        ballistic_ar = dama_pairs.get(0x00060A81, 0)
        energy_ar = dama_pairs.get(0x00060A85, 0)

        slots_str = []
        for bit in range(32):
            if slot_mask & (1 << bit):
                slots_str.append(str(30 + bit))

        out.append((edid, formID, ballistic_ar, energy_ar, slot_mask, slots_str, full,
                     kwda_formids, data_value, data_weight, fnam_health))
    return out


def main(argv):
    if len(argv) < 2:
        print("usage: extract_head_armor.py <plugin>...")
        return 2

    for fname in argv[1:]:
        p = Path(fname)
        if not p.exists():
            print(f"!! missing: {fname}")
            continue
        try:
            entries = parse_plugin(p, debug=True)
        except Exception as e:
            print(f"!! {fname}: {e}")
            continue

        # Sort by ballistic AR (descending), then by EditorID
        entries.sort(key=lambda x: (-x[2], x[0].lower()))

        print(f"\n=== {p.name}  ({len(entries)} head-slot ARMO records, excluding Skin* entries) ===")
        print(f"{'EditorID':<55s} {'FormID':<12s} {'Bal':>5s} {'Ene':>5s}  {'Val':>5s} {'Wt':>5s} {'HP':>5s}  {'Slots':<25s} {'Name'}")
        print("-" * 150)
        for edid, fid, bal_ar, ene_ar, mask, slots, full, kwda, val, wt, hp in entries:
            name_str = full or ""
            slot_str = ",".join(slots)
            val_str = f"{val}" if val is not None else "?"
            wt_str = f"{wt:.1f}" if wt is not None else "?"
            hp_str = f"{hp}" if hp is not None else "?"
            # Check for ArmorTypePower keyword (0x0004D8A1 in vanilla)
            is_pa = "PA" if 0x0004D8A1 in kwda else ""
            print(f"  {edid:<53s} 0x{fid:08X}  {bal_ar:>5d} {ene_ar:>5d}  {val_str:>5s} {wt_str:>5s} {hp_str:>5s}  [{slot_str:<23s}] {is_pa:<3s}{name_str}")

        # Summary stats (ballistic AR)
        ratings = [e[2] for e in entries if e[2] > 0]
        eratings = [e[3] for e in entries if e[3] > 0]
        if ratings:
            print(f"\n  === Ballistic AR Summary ({len(ratings)} pieces with AR > 0) ===")
            print(f"    Min: {min(ratings)}")
            print(f"    Max: {max(ratings)}")
            print(f"    Mean: {sum(ratings)/len(ratings):.1f}")
            print(f"    Median: {sorted(ratings)[len(ratings)//2]}")

            buckets = [(1, 10), (10, 25), (25, 50), (50, 75), (75, 100), (100, 150), (150, 200), (200, 500)]
            print(f"\n    Distribution:")
            for lo, hi in buckets:
                count = sum(1 for r in ratings if lo <= r < hi)
                if count > 0:
                    items = [e[0] for e in entries if lo <= e[2] < hi]
                    print(f"      AR {lo:>3d}-{hi:>3d}: {count} pieces  ({', '.join(items[:5])}{'...' if len(items)>5 else ''})")

        if eratings:
            print(f"\n  === Energy AR Summary ({len(eratings)} pieces with EAR > 0) ===")
            print(f"    Min: {min(eratings)}")
            print(f"    Max: {max(eratings)}")
            print(f"    Mean: {sum(eratings)/len(eratings):.1f}")
            print(f"    Median: {sorted(eratings)[len(eratings)//2]}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

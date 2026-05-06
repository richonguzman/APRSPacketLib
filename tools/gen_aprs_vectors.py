#!/usr/bin/env python3
"""Generate APRS test vectors covering the Base91 (compressed) format.

Produces test/common/aprs_vectors.h, which Unity tests under
test/test_base91_*/ consume. All vectors are derived from APRS
Protocol Reference v1.2 §9 ("Compressed Position Report Data
Formats") — the script makes no calls into APRSPacketLib, so test
asserts compare library output against an independent oracle.

Re-run after editing the case lists below:

    tools/.venv/bin/python tools/gen_aprs_vectors.py

The header contains:

  * kCourseVectors / kSpeedVectors — single-byte (encoded, expected
    decode) pairs used by the c/s decode tests. These cross-check
    against aprslib (rossengeorgiev/aprs-python) when installed.

  * kBase91FixedPoints — five hand-picked sites (Munich, Cape Town,
    Auckland, North Pole, Equator-0) with the full set of base91
    fields derived from the spec formulas. Used by the encode and
    round-trip tests.

The generator aborts loudly if aprslib disagrees with the spec
formulas — meaning the generator itself is wrong.
"""

from __future__ import annotations

import math
import sys
import warnings
from pathlib import Path

# aprslib 0.7.2 has unescaped backslashes in regex string literals that
# trigger SyntaxWarning under Python 3.12+. Upstream noise; filter it
# rather than relying on cached .pyc to hide it.
warnings.filterwarnings(
    "ignore",
    category=SyntaxWarning,
    message=r"invalid escape sequence",
)


COURSE_CASES_DEG = [0, 88, 100, 180, 200, 356]
SPEED_CASES_KN = [0, 30, 36, 50, 99]

# Five fixed sites covering pole / equator / negative lat / negative lon.
# Tuple order: name, lat_deg, lon_deg, course_deg, speed_kn, altitude_m.
# altitude_ft is derived inside build_fixed_point_records (× 3.2808399).
FIXED_POINTS = [
    ("Munich",     48.138630,   11.573410,   90,  30,  525),
    ("CapeTown",  -33.918861,   18.423300,  180,  10,   25),
    ("Auckland",  -36.848460,  174.762189,  270, 100,    0),
    ("NorthPole",  89.999000,    0.000000,    0,   0, 5000),
    ("Equator0",    0.000000,    0.000000,    0,   0,    0),
]

OUT = Path(__file__).resolve().parent.parent / "test" / "common" / "aprs_vectors.h"

# T-byte ('compression type'). The library always emits 'G' for the
# course/speed slot and 'Q' for the altitude slot; the spec leaves the
# source / NMEA bits implementation-defined for our use, and these
# values are the ones aprslib accepts.
T_BYTE_CS = "G"
T_BYTE_ALT = "Q"


def base91_pack(value: int, width: int) -> str:
    """Pack a non-negative int into `width` base-91 ASCII chars
    (each char in '!'..'{', i.e. 33..123). Big-endian: most significant
    digit first. APRS12c §9 'Conversion of Latitude/Longitude'."""
    if value < 0:
        raise ValueError(f"base91 cannot encode negative value {value}")
    out = []
    for i in range(width - 1, -1, -1):
        digit = (value // (91 ** i)) % 91
        out.append(chr(digit + 33))
    return "".join(out)


def encode_lat_base91(lat_deg: float) -> str:
    """APRS12c §9: YYYY = base91( 380926 * (90 - lat_deg) )."""
    v = round(380926 * (90.0 - lat_deg))
    return base91_pack(v, 4)


def encode_lon_base91(lon_deg: float) -> str:
    """APRS12c §9: XXXX = base91( 190463 * (180 + lon_deg) )."""
    v = round(190463 * (180.0 + lon_deg))
    return base91_pack(v, 4)


def encode_course(course_deg: int) -> str:
    """APRS12c §9 'Course/Speed': c = chr(round(course/4) + 33)."""
    return chr(round(course_deg / 4) + 33)


def encode_speed(speed_kn: int) -> str:
    """APRS12c §9 'Course/Speed': s = chr(round(log_1.08(speed_kn+1)) + 33)."""
    return chr(round(math.log(speed_kn + 1) / math.log(1.08)) + 33)


def encode_altitude(altitude_ft: int) -> tuple[str, str]:
    """APRS12c §9 'Altitude': alt_c × 91 + alt_s = log_1.002(altitude_ft).

    The library uses int truncation (not round) when packing the two
    digits; we match that here so the on-the-wire bytes line up. For
    altitude_ft <= 0 the library short-circuits to ('!','!'); we do
    the same."""
    if altitude_ft <= 0:
        return ("!", "!")
    alt = math.log(altitude_ft) / math.log(1.002)
    alt1 = int(alt // 91)
    alt2 = int(alt) % 91
    return (chr(alt1 + 33), chr(alt2 + 33))


def decode_speed_kn(s_byte: str) -> float:
    """Spec-formula decode in knots, as a float (no rounding)."""
    return 1.08 ** (ord(s_byte) - 33) - 1


def decode_speed_kmh(s_byte: str) -> int:
    """Spec-formula decode rounded the way the C++ decoder rounds.

    The library returns `int` from a `double` expression — which in
    both C and Python truncates toward zero. Matching that here means
    the Unity test can assert exact equality with no tolerance.
    """
    return int(decode_speed_kn(s_byte) * 1.852)


def cross_check_course_speed(course_vecs, speed_vecs) -> list[str]:
    """Verify the spec formula matches aprslib for every c/s vector."""
    try:
        import aprslib
    except ImportError:
        print(
            "WARN: aprslib not installed — skipping third-party cross-check.\n"
            "      Install with `uv pip install --python tools/.venv/bin/python aprslib`.",
            file=sys.stderr,
        )
        return []

    failures: list[str] = []
    for c_byte, course_deg in course_vecs:
        for s_byte, speed_kmh in speed_vecs:
            packet = f"TEST>APRS:=/<<<<<<<<>{c_byte}{s_byte}{T_BYTE_CS}"
            parsed = aprslib.parse(packet)
            got_course = parsed.get("course")
            got_speed = parsed.get("speed")

            # APRS v1.2 reassigned encoded course 0 to mean "due north"
            # (= 360); 0 and 360 denote the same direction. Normalize.
            expected_course = 360 if course_deg == 0 else course_deg
            if got_course != expected_course:
                failures.append(
                    f"course byte {c_byte!r}: spec says {expected_course}°, "
                    f"aprslib says {got_course}°"
                )
            spec_kmh = decode_speed_kn(s_byte) * 1.852
            if got_speed is None or abs(got_speed - spec_kmh) > 1e-6:
                failures.append(
                    f"speed byte {s_byte!r}: spec says {spec_kmh} km/h, "
                    f"aprslib says {got_speed} km/h"
                )
    return failures


def cross_check_fixed_points(points) -> list[str]:
    """Round-trip each spec-encoded fixed point through aprslib's parser
    and verify decoded lat/lon come back within format resolution."""
    try:
        import aprslib
    except ImportError:
        return []

    failures: list[str] = []
    for p in points:
        info = (
            "=/"
            + p["base91_lat"]
            + p["base91_lon"]
            + ">"
            + p["base91_c"]
            + p["base91_s"]
            + T_BYTE_CS
        )
        packet = f"TEST>APRS:{info}"
        try:
            parsed = aprslib.parse(packet)
        except Exception as exc:  # pragma: no cover — diagnostic only
            failures.append(f"{p['name']}: aprslib raised {exc!r}")
            continue

        # Base91 lat resolution is ~1/380926 deg ≈ 0.3 m. Allow 1e-4 deg
        # to absorb any rounding the parser does internally.
        for field, expected in (("latitude", p["lat_deg"]),
                                ("longitude", p["lon_deg"])):
            got = parsed.get(field)
            if got is None or abs(got - expected) > 1e-4:
                failures.append(
                    f"{p['name']}: {field} expected {expected}, "
                    f"aprslib decoded {got}"
                )

    return failures


def build_fixed_point_records(points):
    records = []
    for name, lat, lon, course, speed, alt_m in points:
        # Library's `int altitude` parameter is in feet (per AGENTS.md
        # 'Units on the wire'). Caller (firmware) does the m→ft
        # conversion. Pre-compute and ship both.
        alt_ft = int(round(alt_m * 3.2808399))
        alt_c, alt_s = encode_altitude(alt_ft)
        records.append(
            {
                "name": name,
                "lat_deg": lat,
                "lon_deg": lon,
                "course_deg": course,
                "speed_kn": speed,
                "altitude_m": alt_m,
                "altitude_ft": alt_ft,
                "base91_lat": encode_lat_base91(lat),
                "base91_lon": encode_lon_base91(lon),
                "base91_c": encode_course(course),
                "base91_s": encode_speed(speed),
                "base91_alt_c": alt_c,
                "base91_alt_s": alt_s,
            }
        )
    return records


def c_string(s: str) -> str:
    """Quote a Python string as a valid C string literal. All our
    inputs are printable 7-bit ASCII, so only `"` and `\\` need
    escaping."""
    out = ['"']
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == '\\':
            out.append('\\\\')
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def c_char(c: str) -> str:
    """Quote a single Python char as a valid C character literal."""
    assert len(c) == 1
    if c == "'":
        return "'\\''"
    if c == "\\":
        return "'\\\\'"
    return f"'{c}'"


def emit_header(course_vecs, speed_vecs, fixed_points) -> None:
    lines = [
        "// Generated by tools/gen_aprs_vectors.py — DO NOT EDIT BY HAND.",
        "//",
        "// Source: APRS Protocol Reference v1.2 §9 ('Compressed Position",
        "// Report Data Formats'). Each row is computed from the spec",
        "// formulas; aprslib is consulted as a third-party cross-check.",
        "",
        "#pragma once",
        "",
        "struct CourseVector { char encoded; int course_deg; };",
        "struct SpeedVector  { char encoded; int speed_kmh; };",
        "",
        "// Full-packet vectors for the five fixed sites used by",
        "// test_base91_encode and test_base91_roundtrip. Strings are",
        "// 4 base-91 chars + null terminator.",
        "struct Base91FixedPoint {",
        "    const char* name;",
        "    float       lat_deg;",
        "    float       lon_deg;",
        "    int         course_deg;",
        "    int         speed_kn;",
        "    int         altitude_m;",
        "    int         altitude_ft;",
        "    char        base91_lat[5];",
        "    char        base91_lon[5];",
        "    char        base91_c;",
        "    char        base91_s;",
        "    char        base91_alt_c;",
        "    char        base91_alt_s;",
        "};",
        "",
        f"static const char kBase91TByteCS  = '{T_BYTE_CS}';",
        f"static const char kBase91TByteAlt = '{T_BYTE_ALT}';",
        "",
        "static const CourseVector kCourseVectors[] = {",
    ]
    for byte, course_deg in course_vecs:
        lines.append(f"    {{ {c_char(byte):<6}, {course_deg:>3} }},")
    lines.append("};")
    lines.append("")
    lines.append("static const SpeedVector kSpeedVectors[] = {")
    for byte, speed_kmh in speed_vecs:
        lines.append(f"    {{ {c_char(byte):<6}, {speed_kmh:>3} }},")
    lines.append("};")
    lines.append("")
    lines.append("static const Base91FixedPoint kBase91FixedPoints[] = {")
    for p in fixed_points:
        lines.append(
            "    {{ {name:<13}, {lat:>13}f, {lon:>13}f, {course:>3}, {speed:>3}, "
            "{alt_m:>4}, {alt_ft:>5}, {b_lat:<8}, {b_lon:<8}, "
            "{b_c:<6}, {b_s:<6}, {b_ac:<6}, {b_as:<6} }},".format(
                name=c_string(p["name"]),
                lat=f'{p["lat_deg"]:.6f}',
                lon=f'{p["lon_deg"]:.6f}',
                course=p["course_deg"],
                speed=p["speed_kn"],
                alt_m=p["altitude_m"],
                alt_ft=p["altitude_ft"],
                b_lat=c_string(p["base91_lat"]),
                b_lon=c_string(p["base91_lon"]),
                b_c=c_char(p["base91_c"]),
                b_s=c_char(p["base91_s"]),
                b_ac=c_char(p["base91_alt_c"]),
                b_as=c_char(p["base91_alt_s"]),
            )
        )
    lines.append("};")
    lines.append("")

    OUT.write_text("\n".join(lines))
    print(f"wrote {OUT}", file=sys.stderr)


def main() -> int:
    course_vecs = [(encode_course(c), c) for c in COURSE_CASES_DEG]
    speed_vecs = [
        (encode_speed(s), decode_speed_kmh(encode_speed(s))) for s in SPEED_CASES_KN
    ]
    fixed_points = build_fixed_point_records(FIXED_POINTS)

    failures = cross_check_course_speed(course_vecs, speed_vecs)
    failures += cross_check_fixed_points(fixed_points)
    if failures:
        print("ERROR: spec-formula vectors disagree with aprslib:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1

    emit_header(course_vecs, speed_vecs, fixed_points)
    return 0


if __name__ == "__main__":
    sys.exit(main())

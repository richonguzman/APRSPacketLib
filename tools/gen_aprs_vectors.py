#!/usr/bin/env python3
"""Generate, emit, and validate APRS test vectors.

The pipeline is:

    FIXED_POINTS / COURSE_CASES_DEG / SPEED_CASES_KN
                     │
                     │  generate          (writes JSON, byte-stable)
                     ▼
            tools/aprs_vectors.json    ◄── committed
                     │
       ┌─────────────┼───────────────────┐
       │             │                   │
   header        validate-*           (consumers)
       │             │
       ▼             ▼
test/common/   spec compliance
aprs_vectors.h checks (aprslib /
(committed)    Lean / direwolf)

Subcommands
-----------

    generate           Recompute the vectors from the spec formulas in
                       this file and (over)write tools/aprs_vectors.json.
                       Output is byte-stable across runs.

    header             Read the JSON and (over)write
                       test/common/aprs_vectors.h. Output is byte-stable.

    validate-aprslib   Decoder-against-decoder cross-check. Hand each
                       JSON-recorded byte to aprslib's parser and
                       assert its decoded values match a Python
                       re-implementation of the same spec formulas
                       (decode_lat_base91 / decode_lon_base91 /
                       decode_speed_kn) to within float-noise
                       tolerance. Encoder accuracy is *not* in scope
                       here — that is covered by validate-direwolf's
                       encode pass and validate-lean.

    validate-lean      Decode + encode cross-check. Hand the JSON's
                       fixed-point rows to the Lean validator
                       (tools/ValidatedTestVectors/Main.lean). Lean
                       re-runs encodeLat/encodeLon (byte-exact) and
                       decodeLat/decodeLon (within the formally-proven
                       1/factor bound). The Python wrapper invokes
                       `lake env lean --run Main.lean` -- the macOS-safe
                       invocation documented in
                       tools/ValidatedTestVectors/CLAUDE.md.

    validate-direwolf  Decode + encode cross-check through Direwolf.
                       Decode pass: each course×speed pair and each
                       fixed point (twice -- cs slot carrying
                       course/speed, then altitude) is piped through
                       `decode_aprs(1)` and direwolf's parsed lat/
                       lon, course, speed and altitude are compared
                       against a Python re-decode of the same byte
                       (decode_lat_base91 / decode_lon_base91 /
                       decode_course / decode_speed_kn /
                       decode_altitude_base91_m). Tolerance is
                       direwolf-print-precision only -- no encoder
                       loss enters this comparison. Encode pass:
                       each fixed point is fed to `direwolf` via
                       PBEACON COMPRESS=1, the emitted base91 lat/
                       lon bytes are captured from the monitor log
                       and compared byte-for-byte against our
                       recorded bytes (zero tolerance). Encoder-vs-
                       decoder round trip is covered separately by
                       cmd_generate's self-tests. Requires
                       `decode_aprs` and `direwolf` on PATH and an
                       audio device with at least one input channel
                       (`brew install direwolf` on macOS, plus a
                       virtual loopback like BlackHole 2ch if no
                       real input device is present).

Bit-stable regeneration
-----------------------

Both aprs_vectors.json and aprs_vectors.h are committed. Re-running
`generate` followed by `header` must produce byte-identical files to
those in git. If a regeneration produces a diff, fix the generator,
not the committed artifact.

Validation tolerances
---------------------

Tolerances are picked at the tightest spec-/arithmetic-justified value
and the rationale lives next to each constant. The validate-aprslib
and validate-direwolf comparisons are framed as Python-spec-decode vs.
third-party-decode (or our-bytes vs. direwolf-bytes for the encoder
pass), so encoder loss is structurally absent from these bounds --
they cover float noise and presentation-layer rounding only. The
encoder-vs-decoder round-trip is proven separately by
`_self_test_records` in `cmd_generate`. See `_LAT_TOL_DEG`,
`_LON_TOL_DEG`, `_SPEED_TOL_KMH`, `_ALT_TOL_M` in
`cmd_validate_aprslib` / `cmd_validate_direwolf`, and the matching
constants in `Main.lean`.
"""

from __future__ import annotations

import argparse
import json
import logging
import math
import os
import re
import shutil
import subprocess
import sys
import warnings
from pathlib import Path

logger = logging.getLogger("gen_aprs_vectors")


class _ColorFormatter(logging.Formatter):
    """Add direction emojis (📥 decode / 📤 encode), PASS/FAIL markers, and
    ANSI level colours. Colours are only emitted when the handler's stream is
    a TTY and the NO_COLOR env var is unset (per no-color.org)."""

    _LEVEL_COLOR = {
        "DEBUG":   "\x1b[2m",      # dim
        "INFO":    "\x1b[36m",     # cyan
        "WARNING": "\x1b[33m",     # yellow
        "ERROR":   "\x1b[1;31m",   # bold red
    }
    _RESET = "\x1b[0m"
    _GREEN = "\x1b[32m"
    _RED   = "\x1b[31m"

    def __init__(self, color: bool) -> None:
        super().__init__("%(levelname)s %(message)s")
        self._color = color

    def format(self, record: logging.LogRecord) -> str:
        level, _, body = super().format(record).partition(" ")
        # Direction tag on DEBUG (per-row) lines only — INFO headers and
        # summaries mention "decode"/"encode" in passing and shouldn't be
        # flagged as if they were results.
        if record.levelno == logging.DEBUG:
            if " decode " in body:
                body = "📥 " + body
            elif " encode " in body:
                body = "📤 " + body
        # Per-row PASS/FAIL markers from the validate-* subcommands.
        if self._color:
            body = body.replace("[PASS]", f"{self._GREEN}✅ PASS{self._RESET}")
            body = body.replace("[FAIL]", f"{self._RED}❌ FAIL{self._RESET}")
        else:
            body = body.replace("[PASS]", "✅ PASS")
            body = body.replace("[FAIL]", "❌ FAIL")
        # Summary lines: "OK: ..." (info) and "FAIL: ..." (warning).
        if body.startswith("OK:"):
            body = "✅ " + body
        elif body.startswith("FAIL:"):
            body = "❌ " + body
        if self._color:
            level_out = f"{self._LEVEL_COLOR.get(level, '')}{level}{self._RESET}"
        else:
            level_out = level
        return f"{level_out} {body}"

# aprslib 0.7.2 has unescaped backslashes in its regex literals,
# tripping SyntaxWarning under Python 3.12+. Filter once at import time.
warnings.filterwarnings(
    "ignore",
    category=SyntaxWarning,
    message=r"invalid escape sequence",
)


# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

REPO_ROOT        = Path(__file__).resolve().parent.parent
DEFAULT_JSON     = REPO_ROOT / "tools" / "aprs_vectors.json"
DEFAULT_HEADER   = REPO_ROOT / "test"  / "common" / "aprs_vectors.h"
DEFAULT_LEAN_DIR = REPO_ROOT / "tools" / "ValidatedTestVectors"


# ---------------------------------------------------------------------------
# Source-of-truth lists for `generate`
# ---------------------------------------------------------------------------

# To add coverage, edit here and re-run `generate`. The rest of the
# pipeline (header, validators) consumes the resulting JSON.

COURSE_CASES_DEG = [0, 88, 100, 180, 200, 356]
SPEED_CASES_KN   = [0, 30, 36, 50, 99]

# Off-grid course inputs: these are NOT multiples of 4 and so don't
# fit COURSE_CASES_DEG's `(byte, course_deg)` shape (the byte's
# decoded value differs from the input). Each entry exercises a
# specific round-half-up behaviour the L4 course-encode bug is
# symptomatic of:
#   89   — control; both round-half-up and truncation map to byte '7'
#          (88°), so this should *not* expose any divergence.
#   91   — lower-grid divergence; round-half-up gives '8' (92°), the
#          C++ truncation in the L4 bug gives '7' (88°).
#   359  — explicit L4 round-trip failing case; round-half-up gives
#          '{' (360°), C++ truncation gives 'z' (356°).
# See wip/bug_report_l4_encode_course.md.
COURSE_ROUNDTRIP_CASES_DEG = [89, 91, 359]

# (name, lat_deg, lon_deg, course_deg, speed_kn, altitude_m).
FIXED_POINTS = [
    ("Munich",     48.138630,   11.573410,   90,  30,  525),
    ("CapeTown",  -33.918861,   18.423300,  180,  10,   25),
    ("Auckland",  -36.848460,  174.762189,  270, 100,    0),
    ("NorthPole",  89.999000,    0.000000,    0,   0, 5000),
    ("Equator0",    0.000000,    0.000000,    0,   0,    0),
]

# T-byte ('compression type'). The library always emits 'G' for the
# course/speed slot and 'Q' for the altitude slot.
T_BYTE_CS  = "G"
T_BYTE_ALT = "Q"

# APRS12c §9 'Lat/Long Encoding' (p. 38). Lat range is 180° spread
# over 91^4-1 codes; lon is 360° spread over the same range (factor
# halved).
LAT_FACTOR = 380926
LON_FACTOR = 190463


# Diagnostic toggle: when True, course and speed encoders **and**
# decode_speed_kmh switch from round-to-nearest to floor (truncation).
# The default (False) is round-to-nearest -- the spec-silent-but-
# numerically-correct interpretation -- see encode_course /
# encode_speed / decode_speed_kmh docstrings for the rationale
# (direwolf parity, halved worst-case error, no downward bias).
#
# Flip to True to cross-check against implementations suspected of
# truncating: APRS12c §9 itself floors in the lat/lon worked example,
# some early codecs followed that, and the C++ library under test in
# this repo currently truncates on the decode side (see
# `wip/bug_report_decoder_unit_truncation.md`).
#
# decode_speed_kn returns the spec-formula float with no rounding, so
# this flag does not affect it; only the * 1.852 -> int conversion in
# decode_speed_kmh.
DEBUG_FLOOR_COURSE_SPEED = False


# ---------------------------------------------------------------------------
# Spec-faithful encoders / decoders
# ---------------------------------------------------------------------------

def base91_pack(value: int, width: int) -> str:
    """Pack an int into `width` base-91 ASCII chars, big-endian.
    Each digit is in [0, 90] mapped to ASCII 33..123 ('!'..'{')."""
    if value < 0:
        raise ValueError(f"base91 cannot encode negative value {value}")
    out = []
    for i in range(width - 1, -1, -1):
        digit = (value // (91 ** i)) % 91
        out.append(chr(digit + 33))
    return "".join(out)


def base91_unpack(s: str) -> int:
    """Inverse of `base91_pack`. Each byte must lie in '!'..'{' (ASCII
    33..123); behaviour outside that range is unspecified."""
    n = 0
    for ch in s:
        n = n * 91 + (ord(ch) - 33)
    return n


def encode_lat_base91(lat_deg: float) -> str:
    """APRS12c §9 'Lat/Long Encoding' (p. 38):
        'YYYY  is 380926 x (90 – latitude)   [base 91]'

    The spec writes a real-valued expression and does not state a
    rounding mode for the encoder. We read it as round-to-nearest:
    that matches direwolf (the most widely-deployed reference
    implementation) and the L12 patch's `lround`, and halves the
    worst-case encode error vs. truncation. The cost is disagreement
    with the only worked example in §9, which silently floors
    (190463 × 107.25 = 20427156.75 quoted as 20427156, not rounded
    up). See wip/round_to_nearest_migration_plan.md for the full
    rationale and wip/bug_report_aprs_spec_lat_lon_example.md for the
    longer spec-ambiguity discussion.

    `int(x + 0.5)` rather than Python's `round()`: Python's round is
    banker's (ties to even); we want ties-away-from-zero to match
    C's `lround` and direwolf. For non-negative x (LAT_FACTOR ×
    (90 − lat) is always ≥ 0 over the valid range), `int(x + 0.5)`
    gives round-half-up, which coincides with ties-away on the
    positive reals."""
    return base91_pack(int(LAT_FACTOR * (90.0 - lat_deg) + 0.5), 4)


def encode_lon_base91(lon_deg: float) -> str:
    """APRS12c §9 'Lat/Long Encoding' (p. 38):
        'XXXX  is 190463 x (180 + longitude) [base 91]'

    Same rounding-mode and `int(x + 0.5)` rationale as
    encode_lat_base91 — see that docstring. LON_FACTOR × (180 + lon)
    is non-negative over the valid range."""
    return base91_pack(int(LON_FACTOR * (180.0 + lon_deg) + 0.5), 4)


def decode_lat_base91(encoded: str) -> float:
    """Pure-formula inverse of encode_lat_base91, with no rounding.

    APRS12c §9 (p. 38) gives the decoder direction as:
        latitude = 90 - YYYY / 380926
    where YYYY is the base-91 integer unpacked from the 4 lat bytes.
    Evaluated in Python double precision, the only error vs. the
    spec real-valued formula is last-bit float noise — there is no
    encoder loss in this function because the encoded bytes are
    treated as authoritative input."""
    return 90.0 - base91_unpack(encoded) / LAT_FACTOR


def decode_lon_base91(encoded: str) -> float:
    """Pure-formula inverse of encode_lon_base91, with no rounding.

    APRS12c §9 (p. 38):
        longitude = XXXX / 190463 - 180
    Same float-noise-only guarantee as decode_lat_base91."""
    return base91_unpack(encoded) / LON_FACTOR - 180.0


def encode_course(course_deg: int) -> str:
    """APRS12c §9 'Course/Speed' (p. 38–39) gives only the decoder direction:
        'course = c x 4'
    with c constrained to numeric 0..89 — 'If the ASCII code for c is
    in the range ! to z inclusive — corresponding to numeric values in
    the range 0–89 decimal'. Encoded course is therefore in
    {0, 4, …, 356}; 360° has no defined encoding and `' '` (space) is
    a separate 'no data' sentinel.

    The spec does not write an encoder formula or state a rounding
    mode for the inversion; we round-to-nearest because it matches
    direwolf and halves the worst-case encode error vs. truncation.
    Inputs ≥ 358° round to c = 90 under round-half-up, which is *out
    of spec*: both aprslib and direwolf reject byte '{' (c = 90) on
    decode. Fold c = 90 back to c = 0 via `% 90` so input 359° emits
    '!' (decoded 0°, circular delta 1° from input) — the nearest
    representable grid point under the spec's degree-circle topology
    (360° ≡ 0°).

    `int(x + 0.5)` rather than Python's `round()`: Python's `round`
    is banker's (ties to even); we want ties-away-from-zero to match
    C's `lroundf` and direwolf. For course_deg ≥ 0 (the only legal
    input range), `int(x + 0.5)` gives round-half-up, which coincides
    with ties-away on the non-negative reals. The lat/lon encoder
    above uses the same pattern for the same reason.

    DEBUG_FLOOR_COURSE_SPEED flips the rounding step to truncation
    (see that constant's docstring); the % 90 wrap still applies."""
    val = course_deg / 4
    n = int(val) if DEBUG_FLOOR_COURSE_SPEED else int(val + 0.5)
    return chr((n % 90) + 33)


def encode_speed(speed_kn: int) -> str:
    """APRS12c §9 'Course/Speed' (p. 39) gives only the decoder direction:
        'speed = 1.08^s – 1'  (knots)
    Inverting under round-to-nearest (same rounding-mode caveat as
    encode_course) gives s = round(log_1.08(speed + 1)). Same
    `int(x + 0.5)` ties-away rationale as encode_course; direwolf's
    `s = (int)round(log(speed+1.0)/log(1.08))` in `set_comp_position`
    uses C99 `round()`, which is also ties-away.

    DEBUG_FLOOR_COURSE_SPEED flips this to truncation -- see that
    constant's docstring."""
    val = math.log(speed_kn + 1) / math.log(1.08)
    n = int(val) if DEBUG_FLOOR_COURSE_SPEED else int(val + 0.5)
    return chr(n + 33)


def encode_altitude(altitude_ft: int) -> tuple[str, str]:
    """APRS12c §9 'Altitude' (p. 40) gives only the decoder direction:
        'altitude = 1.002^cs feet'  where cs = c × 91 + s
    Inverting gives cs = log_1.002(altitude_ft), with the two base-91
    digits c = cs // 91 and s = cs % 91. The spec does not specify a
    rounding mode for the inversion; the C++ library uses int
    truncation when packing the two digits, and we match that here
    so the on-the-wire bytes line up — a behavioural match to the
    library, not a spec mandate. For altitude_ft <= 0 the library
    short-circuits to ('!','!')."""
    if altitude_ft <= 0:
        return ("!", "!")
    alt = math.log(altitude_ft) / math.log(1.002)
    alt1 = int(alt // 91)
    alt2 = int(alt) % 91
    return (chr(alt1 + 33), chr(alt2 + 33))


def decode_speed_kn(s_byte: str) -> float:
    """Spec-formula decode in knots, as a float (no rounding)."""
    return 1.08 ** (ord(s_byte) - 33) - 1


def decode_course(c_byte: str) -> int:
    """APRS12c §9 'Course/Speed' (p. 39): course = c × 4, c ∈ 0..89.

    Pure spec-formula inverse of encode_course. The caller is
    responsible for any presentation-layer remapping (e.g. aprslib's
    0 → 360 convention borrowed from Mic-E §10)."""
    return (ord(c_byte) - 33) * 4


def decode_altitude_base91_m(c_byte: str, s_byte: str) -> float:
    """APRS12c §9 'Altitude' (p. 40): altitude = 1.002^cs ft, then
    × 0.3048 to metres. Pure spec formula in Python double precision —
    no rounding on this side, so the caller can apply whichever
    rounding the oracle being compared against uses (direwolf prints
    rounded int metres)."""
    cs = (ord(c_byte) - 33) * 91 + (ord(s_byte) - 33)
    return (1.002 ** cs) * 0.3048


def decode_speed_kmh(s_byte: str) -> int:
    """Spec-formula decode in km/h, rounded to int.

    The APRS spec defines the decode in knots only; the km/h
    conversion is a presentation-layer concern with no spec-mandated
    rounding mode. We round-to-nearest (`int(x + 0.5)` for the
    non-negative speed range) because it halves worst-case error vs.
    truncation, removes the systematic downward bias truncation
    introduces (e.g. s='T' decodes to 91.96 km/h: trunc→91 erring by
    0.96, round→92 erring by 0.04), and matches direwolf's printed
    integer. See `wip/bug_report_decoder_unit_truncation.md` for the
    longer write-up.

    DEBUG_FLOOR_COURSE_SPEED flips this to truncation (the historical
    C++-library behaviour), for cross-checking against
    truncation-based implementations."""
    val = decode_speed_kn(s_byte) * 1.852
    return int(val) if DEBUG_FLOOR_COURSE_SPEED else int(val + 0.5)


# ---------------------------------------------------------------------------
# Generate mode: build records, render JSON
# ---------------------------------------------------------------------------

def _build_records():
    """Compute every byte from the spec formulas and the source-of-truth
    lists at the top of this file. Returns (course_vecs, speed_vecs,
    course_roundtrip_vecs, fixed_points) where each row is a
    tuple/dict ready for `_render_json`."""
    course_vecs = [(encode_course(c), c) for c in COURSE_CASES_DEG]
    speed_vecs = [
        (encode_speed(s), s, decode_speed_kmh(encode_speed(s)))
        for s in SPEED_CASES_KN
    ]
    course_roundtrip_vecs = [
        (c_in, encode_course(c_in), decode_course(encode_course(c_in)))
        for c_in in COURSE_ROUNDTRIP_CASES_DEG
    ]
    fixed_points = []
    for name, lat, lon, course, speed, alt_m in FIXED_POINTS:
        # Library's `int altitude` parameter is in feet; pre-convert.
        alt_ft = int(round(alt_m * 3.2808399))
        alt_c, alt_s = encode_altitude(alt_ft)
        fixed_points.append({
            "name":         name,
            "lat_deg":      lat,
            "lon_deg":      lon,
            "course_deg":   course,
            "speed_kn":     speed,
            "altitude_m":   alt_m,
            "altitude_ft":  alt_ft,
            "base91_lat":   encode_lat_base91(lat),
            "base91_lon":   encode_lon_base91(lon),
            "base91_c":     encode_course(course),
            "base91_s":     encode_speed(speed),
            "base91_alt_c": alt_c,
            "base91_alt_s": alt_s,
        })
    return course_vecs, speed_vecs, course_roundtrip_vecs, fixed_points


def _self_test_records(
    course_vecs, speed_vecs, course_roundtrip_vecs, fixed_points,
) -> None:
    """Run Python encode/decode self-tests on every row before JSON is
    written. Failure raises RuntimeError naming the offending row.

    The validate-aprslib and validate-direwolf subcommands now compare
    third-party implementations against our Python re-decode (not
    against the encoder input), so encoder loss is structurally absent
    from those tolerances. This self-test closes the loop by bounding
    Python encode→decode residuals against the spec-quantization
    limits; a failure here means the encoder/decoder pair in this file
    is internally inconsistent and the JSON should not reach disk.
    """
    # Lat/lon: round-to-nearest encoder, so the encode→decode residual
    # is at most half a ULP (≈1.3e-6 deg lat / ≈2.6e-6 deg lon). Also
    # check that decoding then re-encoding the recorded bytes
    # round-trips exactly — every value on the encoded grid must map
    # back to itself.
    LAT_RESIDUAL = 0.5 / LAT_FACTOR + 1e-12
    LON_RESIDUAL = 0.5 / LON_FACTOR + 1e-12
    for fp in fixed_points:
        enc_lat = fp["base91_lat"]
        dec_lat = decode_lat_base91(enc_lat)
        lat_residual = dec_lat - fp["lat_deg"]
        logger.debug(
            "self-test lat %s: input=%s → bytes=%r → decoded=%s, "
            "residual=%+.3e, bound=±%.3e",
            fp["name"], fp["lat_deg"], enc_lat, dec_lat,
            lat_residual, LAT_RESIDUAL,
        )
        if abs(lat_residual) > LAT_RESIDUAL:
            raise RuntimeError(
                f"self-test lat: {fp['name']} input {fp['lat_deg']} → "
                f"bytes {enc_lat!r} → decoded {dec_lat} "
                f"(residual {lat_residual:+.3e}, "
                f"bound ±{LAT_RESIDUAL:.3e})"
            )
        if encode_lat_base91(dec_lat) != enc_lat:
            raise RuntimeError(
                f"self-test lat: {fp['name']} decode→encode does not "
                f"round-trip ({enc_lat!r} → {dec_lat} → "
                f"{encode_lat_base91(dec_lat)!r})"
            )
        enc_lon = fp["base91_lon"]
        dec_lon = decode_lon_base91(enc_lon)
        lon_residual = dec_lon - fp["lon_deg"]
        logger.debug(
            "self-test lon %s: input=%s → bytes=%r → decoded=%s, "
            "residual=%+.3e, bound=±%.3e",
            fp["name"], fp["lon_deg"], enc_lon, dec_lon,
            lon_residual, LON_RESIDUAL,
        )
        if abs(lon_residual) > LON_RESIDUAL:
            raise RuntimeError(
                f"self-test lon: {fp['name']} input {fp['lon_deg']} → "
                f"bytes {enc_lon!r} → decoded {dec_lon} "
                f"(residual {lon_residual:+.3e}, "
                f"bound ±{LON_RESIDUAL:.3e})"
            )
        if encode_lon_base91(dec_lon) != enc_lon:
            raise RuntimeError(
                f"self-test lon: {fp['name']} decode→encode does not "
                f"round-trip ({enc_lon!r} → {dec_lon} → "
                f"{encode_lon_base91(dec_lon)!r})"
            )

    # Course: every COURSE_CASES_DEG entry is a multiple of 4, so both
    # directions of the round trip are exact.
    for byte, c_deg in course_vecs:
        logger.debug(
            "self-test course: input=%d° → byte=%r → decoded=%d°",
            c_deg, byte, decode_course(byte),
        )
        if encode_course(c_deg) != byte:
            raise RuntimeError(
                f"self-test course: encode_course({c_deg}) = "
                f"{encode_course(c_deg)!r}, expected {byte!r}"
            )
        if decode_course(byte) != c_deg:
            raise RuntimeError(
                f"self-test course: decode_course({byte!r}) = "
                f"{decode_course(byte)}, expected {c_deg}"
            )

    # Off-grid course round-trips: each input rounds to the nearest
    # 4°-grid point under round-half-up, so encode→decode produces a
    # value within 2° of the input in *circular* (mod 360) distance,
    # not linear distance. Inputs near 360° wrap to 0° via encode_
    # course's % 90 step, so linear |c_out - c_in| ≈ 359° while the
    # circular delta is 1°. The recorded `decoded_deg` is the byte's
    # spec-formula decode.
    for c_in, byte, c_out in course_roundtrip_vecs:
        linear_delta = abs(c_out - c_in)
        circular_delta = min(linear_delta, 360 - linear_delta)
        logger.debug(
            "self-test course (roundtrip): input=%d° → byte=%r → "
            "decoded=%d°, circular delta=%d°",
            c_in, byte, c_out, circular_delta,
        )
        if encode_course(c_in) != byte:
            raise RuntimeError(
                f"self-test course (roundtrip): encode_course({c_in}) "
                f"= {encode_course(c_in)!r}, expected {byte!r}"
            )
        if decode_course(byte) != c_out:
            raise RuntimeError(
                f"self-test course (roundtrip): decode_course({byte!r}) "
                f"= {decode_course(byte)}, expected {c_out}"
            )
        if circular_delta > 2:
            raise RuntimeError(
                f"self-test course (roundtrip): input {c_in}° → byte "
                f"{byte!r} → decoded {c_out}° (circular delta "
                f"{circular_delta}°) exceeds 2° half-LSB bound"
            )

    # Speed: round-to-nearest encoder in log_1.08 space, so the
    # decoded knots sit within a multiplicative √1.08 of (v_in + 1).
    # Symmetric bound: |v_out − v_in| ≤ (√1.08 − 1)(v_in + 1) + ε.
    HALF_ULP_FACTOR = math.sqrt(1.08) - 1.0    # ≈ 0.03923
    for byte, v_in, _kmh in speed_vecs:
        v_out = decode_speed_kn(byte)
        bound = HALF_ULP_FACTOR * (v_in + 1) + 1e-9
        speed_residual = v_out - v_in
        logger.debug(
            "self-test speed: input=%d kn → byte=%r → decoded=%.6f kn, "
            "residual=%+.3e, bound=±%.3e",
            v_in, byte, v_out, speed_residual, bound,
        )
        if abs(speed_residual) > bound:
            raise RuntimeError(
                f"self-test speed: input {v_in} kn → byte {byte!r} → "
                f"decoded {v_out:.6f} kn (residual {speed_residual:+.3e}, "
                f"bound ±{bound:.3e})"
            )

    # Altitude: truncating encoder in log_1.002 space, so decoded ft
    # sits in [ft_in / 1.002, ft_in]. After × 0.3048 metres and the
    # m_in → ft_in rounding done in _build_records (±0.5 ft ≈ ±0.15
    # m), the combined bound is |m_out − m_in| ≤ m_in × 0.002 + 1 + ε.
    # (For m_in = 0 the encoder short-circuits to ('!','!'), decoded
    # as 1.002^0 × 0.3048 = 0.3048 m — well inside the 1 m floor.)
    for fp in fixed_points:
        m_in = fp["altitude_m"]
        m_out = decode_altitude_base91_m(
            fp["base91_alt_c"], fp["base91_alt_s"]
        )
        bound = m_in * 0.002 + 1.0 + 1e-9
        alt_residual = m_out - m_in
        logger.debug(
            "self-test altitude %s: input=%d m → bytes=(%r, %r) → "
            "decoded=%.3f m, residual=%+.3f, bound=±%.3f",
            fp["name"], m_in, fp["base91_alt_c"], fp["base91_alt_s"],
            m_out, alt_residual, bound,
        )
        if abs(alt_residual) > bound:
            raise RuntimeError(
                f"self-test altitude: {fp['name']} input {m_in} m → "
                f"bytes ({fp['base91_alt_c']!r}, {fp['base91_alt_s']!r}) "
                f"→ decoded {m_out:.3f} m "
                f"(residual {alt_residual:+.3f}, bound ±{bound:.3f})"
            )

    # Fixed-point course/speed: same shape as the cartesian-product
    # course_vecs / speed_vecs checks above, but applied to the c/s
    # bytes a fixed point carries (which can be off-grid — Munich's
    # course=90 is the canonical example, rounding to byte '8' which
    # decodes to 92°, not 90°). Catches encoder regressions on
    # round-half-up inputs in the fixed-point set itself.
    for fp in fixed_points:
        c_byte = fp["base91_c"]
        c_in = fp["course_deg"]
        if encode_course(c_in) != c_byte:
            raise RuntimeError(
                f"self-test course (fixed): {fp['name']} "
                f"encode_course({c_in}) = {encode_course(c_in)!r}, "
                f"expected {c_byte!r}"
            )
        c_out = decode_course(c_byte)
        c_bound = 2 + 1e-9    # half a 4°-ULP, ties round away from zero
        c_residual = c_out - c_in
        logger.debug(
            "self-test course (fixed) %s: input=%d° → byte=%r → "
            "decoded=%d°, residual=%+d°, bound=±2°",
            fp["name"], c_in, c_byte, c_out, c_residual,
        )
        if abs(c_residual) > c_bound:
            raise RuntimeError(
                f"self-test course (fixed): {fp['name']} input {c_in}° "
                f"→ byte {c_byte!r} → decoded {c_out}° "
                f"(residual {c_residual:+d}°, bound ±2°)"
            )

        s_byte = fp["base91_s"]
        v_in_kn = fp["speed_kn"]
        if encode_speed(v_in_kn) != s_byte:
            raise RuntimeError(
                f"self-test speed (fixed): {fp['name']} "
                f"encode_speed({v_in_kn}) = {encode_speed(v_in_kn)!r}, "
                f"expected {s_byte!r}"
            )
        v_out_kn = decode_speed_kn(s_byte)
        s_bound = HALF_ULP_FACTOR * (v_in_kn + 1) + 1e-9
        s_residual = v_out_kn - v_in_kn
        logger.debug(
            "self-test speed (fixed) %s: input=%d kn → byte=%r → "
            "decoded=%.6f kn, residual=%+.3e, bound=±%.3e",
            fp["name"], v_in_kn, s_byte, v_out_kn, s_residual, s_bound,
        )
        if abs(s_residual) > s_bound:
            raise RuntimeError(
                f"self-test speed (fixed): {fp['name']} input {v_in_kn} kn "
                f"→ byte {s_byte!r} → decoded {v_out_kn:.6f} kn "
                f"(residual {s_residual:+.3e}, bound ±{s_bound:.3e})"
            )

    logger.info(
        "self-test: %d fixed points (lat/lon/alt/c/s) + %d courses + "
        "%d speeds + %d off-grid course round-trips within "
        "spec-quantization bounds",
        len(fixed_points), len(course_vecs), len(speed_vecs),
        len(course_roundtrip_vecs),
    )


def _json_string(s: str) -> str:
    """Quote a Python string as a JSON string literal. Inputs are 7-bit
    ASCII (base-91 alphabet is 33..123); only `"` and `\\` need
    escaping."""
    out = ['"']
    for ch in s:
        if   ch == '"':  out.append('\\"')
        elif ch == '\\': out.append('\\\\')
        else:            out.append(ch)
    out.append('"')
    return "".join(out)


def _fmt_coord(v: float) -> str:
    """Render a coordinate as a JSON number with exactly 6 decimal
    places (~0.1 m, well below the encoder's 1/380926-deg ULP).

    json.dumps(48.138630) yields '48.13863' (drops trailing zero) so
    cannot be used. f'{v:.6f}' is byte-stable: for any float with at
    most 6 fractional digits of intent, the output is the canonical
    representation regardless of float repr quirks."""
    return f"{v:.6f}"


def _render_json(
    course_vecs, speed_vecs, course_roundtrip_vecs, fixed_points,
) -> str:
    """Hand-format the JSON. Templated rather than json.dumps()'d so
    that float formatting and key ordering are pinned by source code,
    not by the json module's internal heuristics. Produces stable bytes
    across Python versions and platforms."""
    L: list[str] = []
    L.append("{")
    L.append('  "_generator": "tools/gen_aprs_vectors.py generate",')
    L.append('  "_spec": "APRS Protocol Reference v1.2 section 9 (Compressed Position)",')
    L.append(f'  "_lat_factor": {LAT_FACTOR},')
    L.append(f'  "_lon_factor": {LON_FACTOR},')
    L.append(f'  "_t_byte_cs": {_json_string(T_BYTE_CS)},')
    L.append(f'  "_t_byte_alt": {_json_string(T_BYTE_ALT)},')

    L.append('  "course_vectors": [')
    for i, (encoded, course_deg) in enumerate(course_vecs):
        sep = "," if i < len(course_vecs) - 1 else ""
        L.append(
            f'    {{"course_deg": {course_deg}, '
            f'"encoded": {_json_string(encoded)}}}{sep}'
        )
    L.append('  ],')

    L.append('  "speed_vectors": [')
    for i, (encoded, kn_input, kmh_decoded) in enumerate(speed_vecs):
        sep = "," if i < len(speed_vecs) - 1 else ""
        L.append(
            f'    {{"speed_kn_input": {kn_input}, '
            f'"encoded": {_json_string(encoded)}, '
            f'"speed_kmh_decoded": {kmh_decoded}}}{sep}'
        )
    L.append('  ],')

    # Off-grid course inputs whose encoded byte's decoded value
    # differs from the input. Distinct from course_vectors so the
    # (byte, course_deg) shape there stays a clean identity.
    L.append('  "course_roundtrip_vectors": [')
    for i, (input_deg, encoded, decoded_deg) in enumerate(
        course_roundtrip_vecs
    ):
        sep = "," if i < len(course_roundtrip_vecs) - 1 else ""
        L.append(
            f'    {{"input_deg": {input_deg}, '
            f'"encoded": {_json_string(encoded)}, '
            f'"decoded_deg": {decoded_deg}}}{sep}'
        )
    L.append('  ],')

    L.append('  "fixed_points": [')
    for i, p in enumerate(fixed_points):
        sep = "," if i < len(fixed_points) - 1 else ""
        L.append(
            "    {"
            f'"name": {_json_string(p["name"])}, '
            f'"lat_deg": {_fmt_coord(p["lat_deg"])}, '
            f'"lon_deg": {_fmt_coord(p["lon_deg"])}, '
            f'"course_deg": {p["course_deg"]}, '
            f'"speed_kn": {p["speed_kn"]}, '
            f'"altitude_m": {p["altitude_m"]}, '
            f'"altitude_ft": {p["altitude_ft"]}, '
            f'"base91_lat": {_json_string(p["base91_lat"])}, '
            f'"base91_lon": {_json_string(p["base91_lon"])}, '
            f'"base91_c": {_json_string(p["base91_c"])}, '
            f'"base91_s": {_json_string(p["base91_s"])}, '
            f'"base91_alt_c": {_json_string(p["base91_alt_c"])}, '
            f'"base91_alt_s": {_json_string(p["base91_alt_s"])}'
            "}" + sep
        )
    L.append('  ]')
    L.append('}')
    return "\n".join(L) + "\n"


def cmd_generate(args: argparse.Namespace) -> int:
    course_vecs, speed_vecs, course_roundtrip_vecs, fixed_points = (
        _build_records()
    )
    _self_test_records(
        course_vecs, speed_vecs, course_roundtrip_vecs, fixed_points,
    )
    args.json.write_text(_render_json(
        course_vecs, speed_vecs, course_roundtrip_vecs, fixed_points,
    ))
    logger.info("wrote %s", args.json)
    return 0


# ---------------------------------------------------------------------------
# Header mode: read JSON, render C header
# ---------------------------------------------------------------------------

def _c_string(s: str) -> str:
    out = ['"']
    for ch in s:
        if   ch == '"':  out.append('\\"')
        elif ch == '\\': out.append('\\\\')
        else:            out.append(ch)
    out.append('"')
    return "".join(out)


def _c_char(c: str) -> str:
    assert len(c) == 1
    if c == "'":  return "'\\''"
    if c == "\\": return "'\\\\'"
    return f"'{c}'"


def _render_header(data: dict) -> str:
    course_vecs  = data["course_vectors"]
    speed_vecs   = data["speed_vectors"]
    fixed_points = data["fixed_points"]

    L = [
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
        f"static const char kBase91TByteCS  = {_c_char(data['_t_byte_cs'])};",
        f"static const char kBase91TByteAlt = {_c_char(data['_t_byte_alt'])};",
        "",
        "static const CourseVector kCourseVectors[] = {",
    ]
    for v in course_vecs:
        L.append(f"    {{ {_c_char(v['encoded']):<6}, {v['course_deg']:>3} }},")
    L.append("};")
    L.append("")
    L.append("static const SpeedVector kSpeedVectors[] = {")
    for v in speed_vecs:
        L.append(f"    {{ {_c_char(v['encoded']):<6}, {v['speed_kmh_decoded']:>3} }},")
    L.append("};")
    L.append("")
    L.append("static const Base91FixedPoint kBase91FixedPoints[] = {")
    for p in fixed_points:
        L.append(
            "    {{ {name:<13}, {lat:>13}f, {lon:>13}f, {course:>3}, {speed:>3}, "
            "{alt_m:>4}, {alt_ft:>5}, {b_lat:<8}, {b_lon:<8}, "
            "{b_c:<6}, {b_s:<6}, {b_ac:<6}, {b_as:<6} }},".format(
                name=_c_string(p["name"]),
                lat=f"{p['lat_deg']:.6f}",
                lon=f"{p['lon_deg']:.6f}",
                course=p["course_deg"],
                speed=p["speed_kn"],
                alt_m=p["altitude_m"],
                alt_ft=p["altitude_ft"],
                b_lat=_c_string(p["base91_lat"]),
                b_lon=_c_string(p["base91_lon"]),
                b_c=_c_char(p["base91_c"]),
                b_s=_c_char(p["base91_s"]),
                b_ac=_c_char(p["base91_alt_c"]),
                b_as=_c_char(p["base91_alt_s"]),
            )
        )
    L.append("};")
    L.append("")
    return "\n".join(L)


def cmd_header(args: argparse.Namespace) -> int:
    data = json.loads(args.json.read_text())
    args.header.write_text(_render_header(data))
    logger.info("wrote %s", args.header)
    return 0


# ---------------------------------------------------------------------------
# validate-aprslib: decode-only cross-check
# ---------------------------------------------------------------------------

def cmd_validate_aprslib(args: argparse.Namespace) -> int:
    try:
        import aprslib  # type: ignore[import-not-found]
    except ImportError:
        logger.error(
            "aprslib not installed. Install with:\n"
            "  uv pip install --python tools/.venv/bin/python aprslib"
        )
        return 2

    data = json.loads(args.json.read_text())
    lat_factor = data["_lat_factor"]
    lon_factor = data["_lon_factor"]
    t_byte_cs  = data["_t_byte_cs"]

    logger.info(
        "validate-aprslib: aprslib's parser decodes each JSON-recorded "
        "byte; we re-decode the same byte in Python and compare."
    )
    logger.info("  Course: ours = decode_course(c_byte) — spec formula "
                "(ord(c)-33)*4 applied to the JSON's recorded byte, with "
                "0 remapped to 360 to match aprslib's 0=unknown/360=due-north "
                "convention from §10 Mic-E.")
    logger.info("  Speed:  ours = decode_speed_kn(s_byte) * 1.852 — spec "
                "formula 1.08^(s-33)-1 kn converted to km/h, as Python float.")
    logger.info("  Lat:    ours = decode_lat_base91(p['base91_lat']) — spec "
                "formula 90 - N/380926 applied to the JSON's recorded bytes.")
    logger.info("  Lon:    ours = decode_lon_base91(p['base91_lon']) — spec "
                "formula N/190463 - 180 applied to the JSON's recorded bytes.")
    logger.info("  Compared against aprslib.parse(...)'s 'course' / 'speed' / "
                "'latitude' / 'longitude' fields. Tolerance is float-noise "
                "only; encoder accuracy is delegated to validate-direwolf and "
                "validate-lean.")

    # Tolerance rationale (kept here, not at module top, so it sits
    # next to the comparison it governs):
    #
    # _LAT_TOL_DEG / _LON_TOL_DEG: this validator compares aprslib's
    # decode of the recorded base-91 bytes against decode_lat_base91 /
    # decode_lon_base91 -- our Python re-implementation of the same
    # spec formula. Both evaluate `90 - N/factor` (or `N/factor - 180`
    # for lon) in Python double precision from the same integer N, so
    # the residual is purely last-bit float noise (~1e-14 worst case
    # for values up to ±90/±180); 1e-12 gives ~100× margin and is a
    # clean round number. Encoder loss (up to 0.5/factor) is *not*
    # in this bound -- it's covered by validate-direwolf's encode
    # pass and by validate-lean.
    #
    # _SPEED_TOL_KMH: same pattern -- the spec speed decoder formula
    # is `1.08^(s-33) - 1` knots (§9 p. 39), converted to km/h via
    # the standard 1.852 factor. We compare aprslib against
    # decode_speed_kn(s) * 1.852, both Python float. The chain is
    # four ops deep (one being a transcendental `pow` worth ~1 ULP),
    # so worst-case float noise is ~few ULPs of the max speed value
    # ≈ 1e-13. 1e-12 gives ~10× margin and matches the lat/lon bound
    # so there's a single "float noise" tolerance across the
    # validator. Observed residuals are 0.0 (aprslib uses identical
    # operations in identical order); the bound is for defensiveness
    # against future intermediate-precision changes on either side.
    _LAT_TOL_DEG   = 1e-12
    _LON_TOL_DEG   = 1e-12
    _SPEED_TOL_KMH = 1e-12

    failures: list[str] = []

    # Course/speed slot: cartesian product so each c-byte pairs with
    # every s-byte (parallels the legacy double-loop coverage).
    for cv in data["course_vectors"]:
        for sv in data["speed_vectors"]:
            c_byte = cv["encoded"]
            s_byte = sv["encoded"]
            packet = f"TEST>APRS:=/<<<<<<<<>{c_byte}{s_byte}{t_byte_cs}"
            parsed = aprslib.parse(packet)

            # §9 'Course/Speed' (p. 39) defines course = c × 4 with no
            # special-casing of 0; the compressed-format section itself
            # has no 0=unknown/360=north convention (the only "no data"
            # mechanism for the c byte is c = ˽ space, described on the
            # preceding page). But Chapter 7 'Course and Speed' (p. 27)
            # defines course as '001-360' clockwise from north for the
            # uncompressed format, and Chapter 10 'Speed and Course
            # Encoding' (p. 49, Mic-E) spells out that '0 degrees
            # represents an unknown or indefinite course, and 360
            # degrees represents due north'. aprslib applies that
            # convention when decoding compressed course bytes too
            # (c = 0 → reported course 360), so we match it here to
            # make the cross-check pass. This is observed aprslib
            # behaviour, not a §9 mandate. Run the spec formula on the
            # recorded byte (rather than reading cv["course_deg"] back
            # from the JSON) so the framing is "Python re-decode vs
            # aprslib decode" and the test survives future c-bytes
            # that aren't exact multiples of 4.
            spec_course = decode_course(c_byte)
            expected_course = 360 if spec_course == 0 else spec_course
            got_course = parsed.get("course")
            course_ok = got_course == expected_course
            logger.debug(
                "aprslib decode course byte %r: ours=%s°, aprslib=%s° [%s]",
                c_byte, expected_course, got_course,
                "PASS" if course_ok else "FAIL",
            )
            if not course_ok:
                failures.append(
                    f"course byte {c_byte!r}: expected {expected_course}°, "
                    f"aprslib got {got_course}°"
                )

            spec_kmh = decode_speed_kn(s_byte) * 1.852
            got_speed = parsed.get("speed")
            speed_ok = got_speed is not None and abs(got_speed - spec_kmh) <= _SPEED_TOL_KMH
            speed_residual = (
                "n/a" if got_speed is None else f"{got_speed - spec_kmh:+.3e}"
            )
            logger.debug(
                "aprslib decode speed byte %r: ours=%.6f km/h, aprslib=%s km/h, "
                "residual=%s, tol=%.1e [%s]",
                s_byte, spec_kmh, got_speed, speed_residual, _SPEED_TOL_KMH,
                "PASS" if speed_ok else "FAIL",
            )
            if not speed_ok:
                failures.append(
                    f"speed byte {s_byte!r}: spec {spec_kmh} km/h, "
                    f"aprslib {got_speed} km/h "
                    f"(tolerance {_SPEED_TOL_KMH:.1e})"
                )

    # Off-grid course round-trips: each row carries (input_deg,
    # encoded byte, decoded_deg). aprslib's decode of the byte is
    # compared against decoded_deg (= decode_course(byte) with
    # aprslib's 0 → 360 remap applied). Catches whether aprslib
    # agrees with our Python re-decode about what each off-grid
    # input rounds to under round-half-up — i.e. third-party
    # evidence for wip/bug_report_l4_encode_course.md.
    for rt in data["course_roundtrip_vectors"]:
        c_byte = rt["encoded"]
        spec_decoded = rt["decoded_deg"]
        expected_course = 360 if spec_decoded == 0 else spec_decoded
        packet = f"TEST>APRS:=/<<<<<<<<>{c_byte}!{t_byte_cs}"
        try:
            parsed = aprslib.parse(packet)
        except Exception as exc:  # pragma: no cover - diagnostic only
            failures.append(
                f"course roundtrip input={rt['input_deg']}° byte={c_byte!r}:"
                f" aprslib raised {exc!r}"
            )
            continue
        got_course = parsed.get("course")
        ok = got_course == expected_course
        logger.debug(
            "aprslib decode course (roundtrip) input=%d° byte=%r: "
            "ours=%s°, aprslib=%s° [%s]",
            rt["input_deg"], c_byte, expected_course, got_course,
            "PASS" if ok else "FAIL",
        )
        if not ok:
            failures.append(
                f"course roundtrip input={rt['input_deg']}° byte={c_byte!r}:"
                f" expected {expected_course}°, aprslib got {got_course}°"
            )

    # Fixed points: aprslib's decode of the recorded base-91 bytes is
    # compared against decode_lat_base91 / decode_lon_base91 -- our
    # pure-formula re-implementation of the spec decoder -- so the
    # residual is float-noise only. The original p["lat_deg"] / lon_deg
    # input is *not* used here; encoder loss is a separate concern
    # covered by validate-direwolf and validate-lean.
    for p in data["fixed_points"]:
        info = (
            "=/" + p["base91_lat"] + p["base91_lon"]
            + ">" + p["base91_c"] + p["base91_s"] + t_byte_cs
        )
        packet = f"TEST>APRS:{info}"
        try:
            parsed = aprslib.parse(packet)
        except Exception as exc:  # pragma: no cover - diagnostic only
            failures.append(f"{p['name']}: aprslib raised {exc!r}")
            continue

        for field, expected, tol in (
            ("latitude",  decode_lat_base91(p["base91_lat"]),  _LAT_TOL_DEG),
            ("longitude", decode_lon_base91(p["base91_lon"]), _LON_TOL_DEG),
        ):
            got = parsed.get(field)
            ok = got is not None and abs(got - expected) < tol
            residual = "n/a" if got is None else f"{got - expected:+.3e}"
            logger.debug(
                "aprslib decode %s %s: ours=%s, aprslib=%s, residual=%s, "
                "tol=%.3e [%s]",
                p["name"], field, expected, got, residual, tol,
                "PASS" if ok else "FAIL",
            )
            if not ok:
                residual_val = None if got is None else got - expected
                failures.append(
                    f"{p['name']}: {field} expected {expected}, "
                    f"aprslib decoded {got} (residual {residual_val!r}, "
                    f"tolerance {tol:.3e} deg)"
                )

        # Fixed-point course/speed: the c/s bytes a fixed point
        # carries may be off-grid (Munich's course=90 → '8' decoding
        # to 92° is the canonical example). Compare aprslib's parsed
        # values against decode_course(c) / decode_speed_kn(s)*1.852
        # so any cross-impl disagreement on what the fixed-point
        # bytes mean surfaces here too — not just in the cartesian
        # course_vectors × speed_vectors loop above.
        spec_fp_course = decode_course(p["base91_c"])
        expected_fp_course = 360 if spec_fp_course == 0 else spec_fp_course
        got_fp_course = parsed.get("course")
        fp_course_ok = got_fp_course == expected_fp_course
        logger.debug(
            "aprslib decode %s course byte %r: ours=%s°, aprslib=%s° [%s]",
            p["name"], p["base91_c"], expected_fp_course, got_fp_course,
            "PASS" if fp_course_ok else "FAIL",
        )
        if not fp_course_ok:
            failures.append(
                f"{p['name']}: course byte {p['base91_c']!r} expected "
                f"{expected_fp_course}°, aprslib got {got_fp_course}°"
            )

        spec_fp_kmh = decode_speed_kn(p["base91_s"]) * 1.852
        got_fp_speed = parsed.get("speed")
        fp_speed_ok = (got_fp_speed is not None
                       and abs(got_fp_speed - spec_fp_kmh) <= _SPEED_TOL_KMH)
        fp_speed_residual = (
            "n/a" if got_fp_speed is None
            else f"{got_fp_speed - spec_fp_kmh:+.3e}"
        )
        logger.debug(
            "aprslib decode %s speed byte %r: ours=%.6f km/h, "
            "aprslib=%s km/h, residual=%s, tol=%.1e [%s]",
            p["name"], p["base91_s"], spec_fp_kmh, got_fp_speed,
            fp_speed_residual, _SPEED_TOL_KMH,
            "PASS" if fp_speed_ok else "FAIL",
        )
        if not fp_speed_ok:
            failures.append(
                f"{p['name']}: speed byte {p['base91_s']!r} expected "
                f"{spec_fp_kmh} km/h, aprslib got {got_fp_speed} km/h "
                f"(tolerance {_SPEED_TOL_KMH:.1e})"
            )

    if failures:
        logger.warning("FAIL: aprslib disagrees with JSON-recorded vectors:")
        for f in failures:
            logger.warning("  - %s", f)
        return 1
    logger.info(
        "OK: %d course × %d speed pairs and %d fixed points decoded by aprslib "
        "match our spec-formula re-implementation within float noise",
        len(data['course_vectors']),
        len(data['speed_vectors']),
        len(data['fixed_points']),
    )
    return 0


# ---------------------------------------------------------------------------
# validate-lean: encode + decode against the formally-proven Lean impl
# ---------------------------------------------------------------------------

def cmd_validate_lean(args: argparse.Namespace) -> int:
    data = json.loads(args.json.read_text())
    fps = data["fixed_points"]

    logger.info(
        "validate-lean: encode + decode against the formally-proven Lean "
        "implementation in tools/ValidatedTestVectors/."
    )
    logger.info("Each fixed-point row sent to Lean carries:")
    logger.info("  name        — diagnostic label.")
    logger.info("  lat_deg / lon_deg — the encoder INPUT (FIXED_POINTS).")
    logger.info("  base91_lat / base91_lon — our spec-formula encoded bytes.")
    logger.info(
        "Lean re-runs encodeLat/encodeLon (expected byte-exact agreement with "
        "ours) and decodeLat/decodeLon on our bytes (expected within the "
        "formally-proven 1/factor bound of the input). Per-row pass/fail and "
        "residuals are printed by the Lean program itself; the Python wrapper "
        "passes that output through verbatim."
    )

    # Pipe-delimited input: name | lat_deg | lon_deg | base91_lat | base91_lon
    # `|` (ASCII 124) and `\n` (ASCII 10) lie outside the base-91 alphabet
    # (33..123), so neither needs escaping for any field carried below.
    rows = []
    for p in fps:
        rows.append(
            f"{p['name']}|{p['lat_deg']:.6f}|{p['lon_deg']:.6f}|"
            f"{p['base91_lat']}|{p['base91_lon']}"
        )
    stdin = "\n".join(rows) + "\n"

    logger.debug("validate-lean: sending %d rows to Lean validator", len(rows))
    for row in rows:
        logger.debug("  -> %s", row)

    if not args.lean_cwd.exists():
        logger.error("--lean-cwd %s not found", args.lean_cwd)
        return 2

    # `lake env lean --run Main.lean` is the macOS-safe invocation
    # documented in tools/ValidatedTestVectors/CLAUDE.md (the linked
    # exe target fails to link on macOS due to argv length limits, but
    # --run uses the .olean files directly).
    cmd = ["lake", "env", "lean", "--run", "Main.lean"]
    try:
        proc = subprocess.run(
            cmd,
            cwd=args.lean_cwd,
            input=stdin,
            capture_output=True,
            text=True,
            check=False,
        )
    except FileNotFoundError:
        logger.error(
            "`lake` not found on PATH. Install elan and Lean's lake:\n"
            "  curl https://raw.githubusercontent.com/leanprover/elan/master/elan-init.sh -sSf | sh"
        )
        return 2

    # Lean does its own per-row comparisons and prints them; surface its
    # output verbatim so its formatting (and pass/fail reporting) is
    # preserved. We don't re-parse it -- the return code is authoritative.
    sys.stdout.write(proc.stdout)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    return proc.returncode


# ---------------------------------------------------------------------------
# validate-direwolf: decode + encode cross-checks through Direwolf
# ---------------------------------------------------------------------------

# decode_aprs colours its output with ANSI escapes; strip them so the
# regexes below see plain text.
_ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")

# `N|S DD MM.mmmm, E|W DDD MM.mmmm` — the position line direwolf emits
# for every successfully parsed compressed-position frame.
_LATLON_RE = re.compile(
    r"([NS])\s+(\d+)\s+(\d+\.\d+),\s+([EW])\s+(\d+)\s+(\d+\.\d+)"
)
# `<kmh> km/h (<mph> MPH), course <deg>` — only present when the T-byte
# selects course/speed and the cs slot is not the no-data sentinel.
_CS_RE = re.compile(r"(\d+)\s+km/h\s+\(\d+\s+MPH\),\s+course\s+(\d+)")
# `alt <m> m (<ft> ft)` — only present when the T-byte selects altitude.
_ALT_RE = re.compile(r"alt\s+(-?\d+)\s+m")

# Locations decode_aprs probes for tocalls.yaml, plus the Homebrew path
# it does *not* probe but which is the canonical macOS install dir. If
# any candidate has tocalls.yaml we cd there so the relative-path lookup
# inside decode_aprs succeeds and the noisy warning disappears from
# stdout.
_DIREWOLF_DATA_CANDIDATES = (
    Path("/usr/local/share/direwolf"),
    Path("/usr/share/direwolf"),
    Path("/opt/local/share/direwolf"),
    Path("/opt/homebrew/share/direwolf"),
)


def _find_direwolf_data_dir() -> Path | None:
    for cand in _DIREWOLF_DATA_CANDIDATES:
        if (cand / "tocalls.yaml").is_file():
            return cand
    return None


def _decode_via_direwolf(packet: str, exe: str, cwd: Path | None) -> dict:
    """Run `decode_aprs` on one monitor-format packet and return the
    parsed fields. Keys (subset of): `latitude`, `longitude`, `course`,
    `speed_kmh`, `altitude_m`, plus `_raw` (ANSI-stripped stdout) for
    failure diagnostics."""
    proc = subprocess.run(
        [exe],
        input=packet + "\n",
        capture_output=True,
        text=True,
        check=False,
        cwd=str(cwd) if cwd else None,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"decode_aprs exited {proc.returncode}: {proc.stderr.strip()!r}"
        )
    clean = _ANSI_RE.sub("", proc.stdout)
    parsed: dict = {"_raw": clean}
    m = _LATLON_RE.search(clean)
    if m:
        ns, lat_d, lat_m, ew, lon_d, lon_m = m.groups()
        lat = int(lat_d) + float(lat_m) / 60.0
        if ns == "S":
            lat = -lat
        lon = int(lon_d) + float(lon_m) / 60.0
        if ew == "W":
            lon = -lon
        parsed["latitude"]  = lat
        parsed["longitude"] = lon
    m = _CS_RE.search(clean)
    if m:
        parsed["speed_kmh"] = int(m.group(1))
        parsed["course"]    = int(m.group(2))
    m = _ALT_RE.search(clean)
    if m:
        parsed["altitude_m"] = int(m.group(1))
    return parsed


# ---- encode side: drive `direwolf` to emit PBEACONs and capture the bytes ----

# Beacon monitor line: `[<chan>] <SRC>>... :!/<lat4><lon4><sym>...`.
# Direwolf prints these regardless of audio outcome -- we never decode
# the modulated audio, we just scrape the textual log.
_BEACON_LINE_RE = re.compile(
    r"^\[\s*[0-9.]+\s*\]\s+(?P<src>[A-Z0-9-]+)>[^:]+:!/(?P<lat>.{4})(?P<lon>.{4})"
)
# Audio-device dump produced by direwolf at startup; we use it to pick a
# device when the user hasn't named one and direwolf's default lookup
# fails (common on macOS). Each block looks like:
#
#     Name        = "BlackHole 2ch"
#     Host API    = Core Audio
#     Max inputs  = 2
#     Max outputs = 2
_DEVICE_BLOCK_RE = re.compile(
    r'Name\s*=\s*"(?P<name>[^"]+)"\s*\n'
    r'[^\n]*\n'
    r'\s*Max inputs\s*=\s*(?P<inputs>\d+)'
)


def _direwolf_run(conf: str, exe: str, cwd: Path | None, timeout: float) -> str:
    """Run direwolf with `conf` as the configuration file contents. We
    write a tempfile because direwolf reads its config via fopen, not
    stdin -- the `-c -` form is not supported. Returns the combined
    stdout/stderr with ANSI colour codes stripped."""
    import tempfile
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".conf", delete=False
    ) as f:
        f.write(conf)
        conf_path = f.name
    try:
        try:
            proc = subprocess.run(
                [exe, "-c", conf_path, "-t", "0", "-q", "hd"],
                capture_output=True,
                text=True,
                check=False,
                timeout=timeout,
                cwd=str(cwd) if cwd else None,
            )
            out = proc.stdout + proc.stderr
        except subprocess.TimeoutExpired as exc:
            out = (exc.stdout.decode() if isinstance(exc.stdout, bytes) else (exc.stdout or "")) \
                + (exc.stderr.decode() if isinstance(exc.stderr, bytes) else (exc.stderr or ""))
    finally:
        Path(conf_path).unlink(missing_ok=True)
    return _ANSI_RE.sub("", out)


def _direwolf_pick_audio_device(exe: str, cwd: Path | None) -> str | None:
    """Run direwolf with a stub config that has no ADEVICE; parse the
    enumerated device list it prints during startup, and pick the first
    device with at least one input channel. Returns the device name to
    pass via ADEVICE, or None if no suitable device exists."""
    stub = "ACHANNELS 1\nCHANNEL 0\nMYCALL TEST\nMODEM 1200\n"
    out = _direwolf_run(stub, exe, cwd, timeout=8.0)
    for m in _DEVICE_BLOCK_RE.finditer(out):
        if int(m.group("inputs")) > 0:
            return m.group("name")
    return None


def _encode_via_direwolf(
    points: list[dict],
    exe: str,
    audio_device: str,
    cwd: Path | None,
) -> dict[str, tuple[str, str]]:
    """Run direwolf with one staggered PBEACON per point and return
    {source_callsign: (base91_lat, base91_lon)}. Each point's beacon
    uses SENDTO=R0 (simulated reception) so direwolf never has to
    actually modulate audio -- the monitor line still prints."""
    lines = [
        f'ADEVICE "{audio_device}" "{audio_device}"',
        "ACHANNELS 1",
        "CHANNEL 0",
        "MYCALL TEST",
        "MODEM 1200",
    ]
    sources: list[str] = []
    for i, p in enumerate(points):
        src = f"TEST{i + 1}"
        sources.append(src)
        # DELAY=0:0N means N seconds; stagger so beacons don't collide.
        lines.append(
            f"PBEACON DELAY=0:0{i + 1} EVERY=10:00 SENDTO=R0 "
            f"SOURCE={src} LAT={p['lat_deg']:.6f} LONG={p['lon_deg']:.6f} "
            f"SYMBOL=/> COMPRESS=1"
        )
    conf = "\n".join(lines) + "\n"

    # Allow ~3s startup + 1s per beacon + a small safety margin.
    out = _direwolf_run(conf, exe, cwd, timeout=5.0 + len(points))

    captured: dict[str, tuple[str, str]] = {}
    for line in out.splitlines():
        m = _BEACON_LINE_RE.match(line)
        if m and m.group("src") in sources:
            captured[m.group("src")] = (m.group("lat"), m.group("lon"))
    return captured


def cmd_validate_direwolf(args: argparse.Namespace) -> int:
    exe = shutil.which("decode_aprs")
    if not exe:
        logger.error(
            "`decode_aprs` not found on PATH. Install Direwolf:\n"
            "  brew install direwolf      # macOS\n"
            "  apt-get install direwolf   # Debian/Ubuntu"
        )
        return 2

    data       = json.loads(args.json.read_text())
    lat_factor = data["_lat_factor"]
    lon_factor = data["_lon_factor"]
    t_byte_cs  = data["_t_byte_cs"]
    t_byte_alt = data["_t_byte_alt"]
    cwd        = _find_direwolf_data_dir()

    logger.info(
        "validate-direwolf: two passes through direwolf — decode "
        "(decode_aprs(1)) and encode (direwolf PBEACON COMPRESS=1)."
    )
    logger.info("Decode pass — we hand direwolf each JSON-recorded byte and "
                "read its parsed values back from its text output. Every "
                "'ours' value below is a Python re-decode of the same byte "
                "the input handed to direwolf, never the encoder input — so "
                "the residual is direwolf-vs-Python decoder disagreement "
                "only, with no encoder loss baked in:")
    logger.info("  Course: ours = decode_course(c_byte) — spec formula "
                "(ord(c)-33)*4 applied to the recorded byte. Compared "
                "against decode_aprs's 'course=' field (exact integer match).")
    logger.info("  Speed:  ours = decode_speed_kn(s_byte) * 1.852 — spec "
                "float in km/h. Compared against decode_aprs's printed km/h "
                "integer to within ±0.5 (half-LSB of direwolf's rounding).")
    logger.info("  Lat:    ours = decode_lat_base91(p['base91_lat']) — spec "
                "formula 90 - N/380926. Compared against decode_aprs's "
                "DD MM.mmmm reconstruction; tolerance covers print "
                "half-LSB (~8.3e-7 deg) only.")
    logger.info("  Lon:    ours = decode_lon_base91(p['base91_lon']) — same "
                "shape as lat.")
    logger.info("  Alt:    ours = decode_altitude_base91_m(c, s) — spec "
                "formula 1.002^cs × 0.3048. Compared against decode_aprs's "
                "printed metre integer to within ±0.5 (half-LSB of direwolf's "
                "rounding).")
    logger.info(
        "Encode pass — we feed FIXED_POINTS lat/lon as PBEACON SOURCE/LAT/LONG "
        "to direwolf and capture the base91 bytes it emits on the air "
        "(direwolf's PBEACON is stationary, so the cs slot is the 'no-data' "
        "space and altitude is written as /A=NNNNNN — only lat/lon bytes "
        "are exercised):"
    )
    logger.info("  Bytes:  ours = p['base91_lat']/p['base91_lon'] — our "
                "spec-formula encoded bytes. Compared byte-for-byte against "
                "direwolf's emitted bytes (zero tolerance — both we and "
                "direwolf round to nearest, so any divergence is a real "
                "cross-impl encoder disagreement). Encoder-vs-decoder round "
                "trip is proven by cmd_generate's self-tests, not here.")

    # Tolerance rationale (kept here, not at module top, so it sits
    # next to the comparison it governs):
    #
    # Each 'ours' value in this decode pass is a Python re-decode of
    # the same byte handed to direwolf (not the encoder input). So the
    # only source of disagreement is the gap between direwolf's
    # presentation layer (DD MM.mmmm for lat/lon, rounded integer
    # km/h, rounded integer m) and our Python float decode — encoder
    # quantization is structurally absent from these bounds.
    #
    # _LAT_TOL_DEG / _LON_TOL_DEG: decode_aprs prints `DD MM.mmmm`,
    # so the readback adds half-of-last-digit ≈ 0.5 × 1e-4 / 60 ≈
    # 8.3e-7 deg. 1e-6 gives ~20% margin over the theoretical floor
    # and a round-number constant; widen to 2e-6 if banker's-rounding
    # on the last digit makes residuals borderline.
    #
    # _SPEED_TOL_KMH: direwolf prints km/h as a rounded integer.
    # Comparing direwolf's integer against the closed-form spec float
    # therefore has a worst-case gap of 0.5 km/h (half-LSB from
    # rounding); the +1e-9 sliver covers float noise so the bound is
    # not literally 0.5 in edge cases.
    #
    # _ALT_TOL_M: direwolf prints metres as a rounded integer.
    # Comparing that integer against decode_altitude_base91_m's
    # closed-form float has a worst-case gap of 0.5 m (half-LSB);
    # +1e-9 covers float noise. Same shape as the speed bound.
    _LAT_TOL_DEG = 1e-6
    _LON_TOL_DEG = 1e-6
    _SPEED_TOL_KMH = 0.5 + 1e-9
    _ALT_TOL_M = 0.5 + 1e-9

    failures: list[str] = []

    # Course/speed slot: cartesian product so each c-byte pairs with
    # every s-byte (parallels validate-aprslib's coverage).
    for cv in data["course_vectors"]:
        for sv in data["speed_vectors"]:
            c_byte = cv["encoded"]
            s_byte = sv["encoded"]
            packet = f"TEST>APRS:=/<<<<<<<<>{c_byte}{s_byte}{t_byte_cs}"
            try:
                got = _decode_via_direwolf(packet, exe, cwd)
            except RuntimeError as exc:
                failures.append(f"{packet!r}: {exc}")
                continue

            # decode_aprs prints encoded-course 0 as 0 (not 360, the
            # uncompressed/Mic-E 0=unknown/360=due-north convention
            # aprslib applies — see cmd_validate_aprslib for the spec
            # citations). Compare against the Python spec re-decode of
            # the same byte; framing the test as Python-vs-direwolf
            # (not JSON-input-vs-direwolf) keeps the validator's job
            # to "cross-impl decoder comparison" only.
            spec_course = decode_course(c_byte)
            got_course = got.get("course")
            course_ok = got_course == spec_course
            logger.debug(
                "direwolf decode course byte %r: ours=%s°, direwolf=%s° [%s]",
                c_byte, spec_course, got_course,
                "PASS" if course_ok else "FAIL",
            )
            if not course_ok:
                failures.append(
                    f"course byte {c_byte!r}: expected {spec_course}°, "
                    f"direwolf got {got_course}°"
                )

            # Compare direwolf's rounded integer against the spec
            # float (not the JSON's already-rounded
            # speed_kmh_decoded) so the ±0.5 km/h half-LSB bound
            # below applies cleanly and the validator stays
            # independent of decode_speed_kmh's rounding mode.
            spec_kmh = decode_speed_kn(s_byte) * 1.852
            got_kmh = got.get("speed_kmh")
            speed_ok = got_kmh is not None and abs(got_kmh - spec_kmh) <= _SPEED_TOL_KMH
            speed_residual = "n/a" if got_kmh is None else f"{got_kmh - spec_kmh:+.3f}"
            logger.debug(
                "direwolf decode speed byte %r: ours=%.3f km/h, direwolf=%s km/h, "
                "residual=%s, tol=±%s km/h [%s]",
                s_byte, spec_kmh, got_kmh, speed_residual, _SPEED_TOL_KMH,
                "PASS" if speed_ok else "FAIL",
            )
            if not speed_ok:
                failures.append(
                    f"speed byte {s_byte!r}: spec {spec_kmh:.3f} km/h, "
                    f"direwolf got {got_kmh} km/h "
                    f"(tolerance ±{_SPEED_TOL_KMH} km/h)"
                )

    # Off-grid course round-trips: same shape as the aprslib block —
    # confirm direwolf agrees with our Python re-decode on what each
    # off-grid input rounds to under round-half-up. Pair the c-byte
    # with the speed-no-data sentinel '!' so direwolf prints the
    # course unambiguously. Provides direwolf-side evidence for
    # wip/bug_report_l4_encode_course.md.
    for rt in data["course_roundtrip_vectors"]:
        c_byte = rt["encoded"]
        spec_decoded = rt["decoded_deg"]
        packet = f"TEST>APRS:=/<<<<<<<<>{c_byte}!{t_byte_cs}"
        try:
            got = _decode_via_direwolf(packet, exe, cwd)
        except RuntimeError as exc:
            failures.append(
                f"course roundtrip input={rt['input_deg']}° byte={c_byte!r}:"
                f" {exc}"
            )
            continue
        got_course = got.get("course")
        ok = got_course == spec_decoded
        logger.debug(
            "direwolf decode course (roundtrip) input=%d° byte=%r: "
            "ours=%s°, direwolf=%s° [%s]",
            rt["input_deg"], c_byte, spec_decoded, got_course,
            "PASS" if ok else "FAIL",
        )
        if not ok:
            failures.append(
                f"course roundtrip input={rt['input_deg']}° byte={c_byte!r}:"
                f" expected {spec_decoded}°, direwolf got {got_course}°"
            )

    # Fixed points: lat/lon from the recorded base-91 bytes (cs slot
    # carries course/speed for one packet, altitude for the other so
    # we exercise both T-byte branches).
    for p in data["fixed_points"]:
        packet_cs = (
            "TEST>APRS:=/"
            f"{p['base91_lat']}{p['base91_lon']}>"
            f"{p['base91_c']}{p['base91_s']}{t_byte_cs}"
        )
        try:
            got = _decode_via_direwolf(packet_cs, exe, cwd)
        except RuntimeError as exc:
            failures.append(f"{p['name']}: {exc}")
            continue

        for field, expected, tol in (
            ("latitude",  decode_lat_base91(p["base91_lat"]), _LAT_TOL_DEG),
            ("longitude", decode_lon_base91(p["base91_lon"]), _LON_TOL_DEG),
        ):
            v = got.get(field)
            ok = v is not None and abs(v - expected) <= tol
            residual = "n/a" if v is None else f"{v - expected:+.3e}"
            logger.debug(
                "direwolf decode %s %s: ours=%s, direwolf=%s, residual=%s, "
                "tol=%.3e [%s]",
                p["name"], field, expected, v, residual, tol,
                "PASS" if ok else "FAIL",
            )
            if not ok:
                residual_val = None if v is None else v - expected
                failures.append(
                    f"{p['name']}: {field} expected {expected}, "
                    f"direwolf got {v} (residual {residual_val!r}, "
                    f"tolerance {tol:.3e} deg)"
                )

        # Fixed-point course/speed: same shape as the cartesian
        # product loop above, but applied to the c/s bytes a fixed
        # point carries. Munich's c=90 → '8' decoding to 92° is the
        # canonical off-grid case the L4 course-encode bug
        # (wip/bug_report_l4_encode_course.md) is about; without this
        # block, direwolf never sees the fixed-point c/s bytes for
        # comparison.
        spec_fp_course = decode_course(p["base91_c"])
        got_fp_course = got.get("course")
        fp_course_ok = got_fp_course == spec_fp_course
        logger.debug(
            "direwolf decode %s course byte %r: ours=%s°, direwolf=%s° [%s]",
            p["name"], p["base91_c"], spec_fp_course, got_fp_course,
            "PASS" if fp_course_ok else "FAIL",
        )
        if not fp_course_ok:
            failures.append(
                f"{p['name']}: course byte {p['base91_c']!r} expected "
                f"{spec_fp_course}°, direwolf got {got_fp_course}°"
            )

        spec_fp_kmh = decode_speed_kn(p["base91_s"]) * 1.852
        got_fp_kmh = got.get("speed_kmh")
        fp_speed_ok = (got_fp_kmh is not None
                       and abs(got_fp_kmh - spec_fp_kmh) <= _SPEED_TOL_KMH)
        fp_speed_residual = (
            "n/a" if got_fp_kmh is None
            else f"{got_fp_kmh - spec_fp_kmh:+.3f}"
        )
        logger.debug(
            "direwolf decode %s speed byte %r: ours=%.3f km/h, "
            "direwolf=%s km/h, residual=%s, tol=±%s km/h [%s]",
            p["name"], p["base91_s"], spec_fp_kmh, got_fp_kmh,
            fp_speed_residual, _SPEED_TOL_KMH,
            "PASS" if fp_speed_ok else "FAIL",
        )
        if not fp_speed_ok:
            failures.append(
                f"{p['name']}: speed byte {p['base91_s']!r} expected "
                f"{spec_fp_kmh:.3f} km/h, direwolf got {got_fp_kmh} km/h "
                f"(tolerance ±{_SPEED_TOL_KMH} km/h)"
            )

        packet_alt = (
            "TEST>APRS:=/"
            f"{p['base91_lat']}{p['base91_lon']}>"
            f"{p['base91_alt_c']}{p['base91_alt_s']}{t_byte_alt}"
        )
        try:
            got = _decode_via_direwolf(packet_alt, exe, cwd)
        except RuntimeError as exc:
            failures.append(f"{p['name']} (alt): {exc}")
            continue

        v = got.get("altitude_m")
        expected_alt_m = decode_altitude_base91_m(
            p["base91_alt_c"], p["base91_alt_s"]
        )
        alt_ok = v is not None and abs(v - expected_alt_m) <= _ALT_TOL_M
        alt_residual = "n/a" if v is None else f"{v - expected_alt_m:+.3f}"
        logger.debug(
            "direwolf decode %s altitude_m: ours=%.3f, direwolf=%s, "
            "residual=%s m, tol=±%.3f m [%s]",
            p["name"], expected_alt_m, v, alt_residual, _ALT_TOL_M,
            "PASS" if alt_ok else "FAIL",
        )
        if not alt_ok:
            failures.append(
                f"{p['name']}: altitude_m expected {expected_alt_m:.3f}, "
                f"direwolf got {v} (tolerance ±{_ALT_TOL_M} m)"
            )

    # ---- encode pass: drive direwolf's compressed-position encoder ----
    #
    # Coverage is narrower than the decode side: direwolf's PBEACON
    # command is for stationary stations, so it never emits base91
    # course/speed bytes (the cs slot is the two-space "no-data"
    # sentinel) and writes altitude as the comment-style
    # `/A=NNNNNN` block rather than packing it into cs with T='Q'.
    # That leaves lat/lon as the only base91 fields direwolf produces.
    #
    # This pass is pure encoder-vs-encoder: zero tolerance, byte-exact
    # equality only. The encoder→decoder round-trip property (encode
    # → decode within encoder loss) lives in cmd_generate's self-test
    # so it runs unconditionally on every regeneration, not just when
    # direwolf happens to be installed.
    encode_direwolf = shutil.which("direwolf")
    if encode_direwolf:
        audio_device = _direwolf_pick_audio_device(encode_direwolf, cwd)
        if not audio_device:
            failures.append(
                "encode: no audio-input device found via direwolf; "
                "the encode oracle requires one device with Max inputs > 0 "
                "(install BlackHole 2ch on macOS, or any ALSA capture device on Linux)"
            )
        else:
            captured = _encode_via_direwolf(
                data["fixed_points"], encode_direwolf, audio_device, cwd
            )
            for i, p in enumerate(data["fixed_points"]):
                src = f"TEST{i + 1}"
                bytes_pair = captured.get(src)
                if bytes_pair is None:
                    failures.append(
                        f"encode {p['name']}: no PBEACON line captured from direwolf"
                    )
                    continue
                lat_bytes, lon_bytes = bytes_pair
                for field, expected_bytes, got_bytes in (
                    ("lat", p["base91_lat"], lat_bytes),
                    ("lon", p["base91_lon"], lon_bytes),
                ):
                    bytes_ok = got_bytes == expected_bytes
                    logger.debug(
                        "direwolf encode %s %s bytes: ours=%r, direwolf=%r [%s]",
                        p["name"], field, expected_bytes, got_bytes,
                        "PASS" if bytes_ok else "FAIL",
                    )
                    if not bytes_ok:
                        failures.append(
                            f"encode {p['name']}: {field} bytes expected "
                            f"{expected_bytes!r}, direwolf emitted "
                            f"{got_bytes!r} (zero tolerance — both we and "
                            f"direwolf round to nearest)"
                        )
    else:
        # decode_aprs is present (checked above) but the daemon binary
        # is missing -- unusual; just record and continue.
        failures.append(
            "encode: `direwolf` binary not on PATH; "
            "encode pass skipped (decode pass still ran)"
        )

    if failures:
        logger.warning("FAIL: direwolf disagrees with JSON-recorded vectors:")
        for f in failures:
            logger.warning("  - %s", f)
        return 1
    logger.info(
        "OK: %d course × %d speed pairs and %d fixed points (×2 cs/alt) decode "
        "matched direwolf within print-precision tolerance; %d fixed-point "
        "lat/lon byte pairs matched direwolf's encoded bytes exactly",
        len(data['course_vectors']),
        len(data['speed_vectors']),
        len(data['fixed_points']),
        len(data['fixed_points']),
    )
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="gen_aprs_vectors.py",
        description="Generate, emit, and validate APRS test vectors. "
                    "See module docstring for the full pipeline.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument(
        "-v", "--verbose",
        action="count",
        default=0,
        help="increase log verbosity (-v for DEBUG: per-comparison detail "
             "in validate-* subcommands)",
    )
    p.add_argument(
        "-q", "--quiet",
        action="store_true",
        help="suppress INFO-level output; only warnings and errors are shown",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    g = sub.add_parser("generate", help="recompute and write the canonical JSON")
    g.add_argument("--json", type=Path, default=DEFAULT_JSON)
    g.set_defaults(func=cmd_generate)

    h = sub.add_parser("header",   help="emit the C header from the JSON")
    h.add_argument("--json",   type=Path, default=DEFAULT_JSON)
    h.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    h.set_defaults(func=cmd_header)

    a = sub.add_parser("validate-aprslib", help="decode the JSON's bytes through aprslib")
    a.add_argument("--json", type=Path, default=DEFAULT_JSON)
    a.set_defaults(func=cmd_validate_aprslib)

    l = sub.add_parser("validate-lean",
                       help="encode + decode the JSON's rows through the Lean validator")
    l.add_argument("--json",     type=Path, default=DEFAULT_JSON)
    l.add_argument("--lean-cwd", type=Path, default=DEFAULT_LEAN_DIR)
    l.set_defaults(func=cmd_validate_lean)

    d = sub.add_parser("validate-direwolf",
                       help="decode + encode cross-check through Direwolf")
    d.add_argument("--json", type=Path, default=DEFAULT_JSON)
    d.set_defaults(func=cmd_validate_direwolf)

    return p


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.quiet:
        level = logging.WARNING
    elif args.verbose >= 1:
        level = logging.DEBUG
    else:
        level = logging.INFO
    # Configure the root handler at WARNING so noisy third-party
    # libraries (aprslib emits DEBUG-per-parse-step) stay quiet, and
    # bump only our own logger to the requested level. Levels are
    # filtered at the originating logger before propagation, so our
    # DEBUG/INFO records still reach the root handler.
    handler = logging.StreamHandler(sys.stderr)
    use_color = sys.stderr.isatty() and "NO_COLOR" not in os.environ
    handler.setFormatter(_ColorFormatter(use_color))
    logging.basicConfig(level=logging.WARNING, handlers=[handler])
    logger.setLevel(level)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())

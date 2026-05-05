# AGENTS.md

This file provides guidance to coding assistant agents when working with code in this repository.

## Repository purpose

APRSPacketLib is an Arduino/PlatformIO C++ library that encodes and decodes APRS packet formats for LoRa APRS firmware (e.g. the LoRa_APRS_Tracker project). It is consumed as a dependency, not run standalone.

- Library metadata: `library.json` (PlatformIO). Frameworks: `arduino`. Platforms: `espressif32`, `nordicnrf52`.
- Public header: `include/APRSPacketLib.h`. Implementation: `src/APRSPacketLib.cpp`. Both live under the single `APRSPacketLib` namespace.
- Examples are `.ino` sketches in `examples/` — illustrative, not part of any test harness.
- Note licensing inconsistency: `library.json` says `MIT`, the `LICENSE` file and `README.md` say GPL-3.0. Don't "fix" this without asking.

## Build / verify

There is **no test suite, no host-side build, and no CI** in this repo. The code depends on `Arduino.h` and the Arduino `String` class, so it cannot be compiled with a plain host C++ compiler.

To verify changes, the realistic options are:

- Build one of the example sketches against a target board via PlatformIO/Arduino IDE in a downstream project that includes this library.
- Sanity-check by reading the diff and the two source files; the surface area is small (one header, one .cpp).

When asked to "run tests" or "verify", say explicitly that no automated tests exist in this repo rather than claiming success.

## Architecture notes (the parts that aren't obvious from one file)

### Single-namespace, free-function API

All functionality is exposed as free functions in `namespace APRSPacketLib` (declared in `include/APRSPacketLib.h`). There are no classes. Internal helpers (e.g. `applyAmbiguity`, `buildDigiPacket`, `decodeBase91Encoded*`, the Mic-E `encode*`/`decode*` family) live in the same `.cpp` but are not in the header — they are implementation details. When extending the public API, declare in the header *and* keep helpers unexported.

### Decoding pipeline

`processReceivedPacket(const String& receivedPacket, int rssi, float snr, int freqError)` is the single entry point for inbound packets. It returns a populated `APRSPacket` struct and is dispatched by scanning for marker substrings in the received text:

| Marker found in payload | `type` | Meaning |
|---|---|---|
| `:=`, `:!`, `:@` | 0 | GPS position (Base91 *or* degrees-decimal-minutes — disambiguated by the symbol-table char at offset +12) |
| `::` | 1 | Message |
| `:>` | 2 | Status |
| `:T#` | 3 | Telemetry |
| `` :` `` or `:'` | 4 | Mic-E |
| `:;` | 5 | Object |

Two cross-cutting concerns to preserve when editing the dispatcher:

1. **Third-party packets** (header `}`-prefix). Detected up front; the original header is stashed in `aprsPacket.header` and `temp0` is rewritten to the inner packet so the rest of the dispatch logic doesn't have to special-case it.
2. **Field clearing.** After the type-specific branch runs, the function explicitly zeroes fields that don't apply (`symbol`, `overlay`, `latitude`, `longitude`, `course`, `speed`, `altitude` for non-GPS/non-Mic-E; `addressee` unless message; `miceType` unless Mic-E). New packet types must follow the same pattern or callers will see stale data.

### GPS position has two on-the-wire encodings

Inside type 0, the symbol-table indicator at `payloadOffset + 12` selects the decoder:

- `G`, `Q`, `[`, `H`, `X`, `T` → **Base91** compressed format. `Q` carries altitude in the cs bytes; `T`/`Q` with a space at offset +10 mean stationary (course/speed/altitude all 0); `G`/`[` carry course+speed.
- Anything else → **degrees and decimal minutes**, with optional `course/speed` and an optional `/A=NNNNNN` altitude block scanned by substring search.

When changing GPS decoding, change both branches together or update the dispatch char list to match.

### Mic-E encoding is multi-stage

`generateMiceGPSBeaconPacket` composes the packet from several lower-level encoders:

`gpsDecimalToDegreesMiceLatitude` / `…Longitude` → `gpsLatitudeStruct` / `gpsLongitudeStruct` (degrees/minutes/hundredths/N-S-E-W as `uint8_t`s) → `encodeMiceDestinationField` (which folds the Mic-E message-type bits *into* the destination callsign) → `encodeMiceLongitude`, `encodeMiceCourseSpeed`, `encodeMiceAltitude` build the information field byte-by-byte into `uint8_t` buffers that are then cast to `String`.

Two non-obvious gotchas in this path:

- The Mic-E **message type** (`miceMsgType`, three chars of `'0'`/`'1'`) is not transmitted as data; it is encoded by setting the high bit (`+ 0x20`) on the corresponding destination-callsign characters. Validate inputs with `validateMicE` before calling generators — only the eight `"000".."111"` strings are legal.
- Several routines build a `String` from a `uint8_t*` buffer that is null-terminated *manually* (`buf[N] = 0x00`). If you change buffer sizes, the terminator index must move with them.

### Coordinate ambiguity

`applyAmbiguity(coord, level)` rounds lat/lon to 3, 2, or 1 decimal places (~110 m / 1.1 km / 11 km). It is applied in both `encodeGPSIntoBase91` and `generateMiceGPSBeaconPacket` before encoding. Any new beacon generator should apply ambiguity at the same point in the pipeline (before integer/byte encoding) for parity.

**This is not the APRS101 spec mechanism.** The spec defines ambiguity for the uncompressed `DDMM.hh` format as character-blanking (trailing chars replaced with spaces, both lat and lon) and for Mic-E as `Z`-char masking in the destination address (A.1.1.6). The compressed Base91 format has no defined ambiguity mechanism. Rounded-decimal output decodes as an exact (off-grid) position with no on-wire indicator that ambiguity was requested. Additionally, `level == 4` falls into the `default:` branch and returns the input unchanged — `level` validation should treat 4 as a real spec-defined level (mask tens of minute) once a fix lands.

### Known decoder hazards

`decodeBase91EncodedCourse(const String&)` and `decodeBase91EncodedSpeed(const String&)` call `course.toInt()` / `speed.toInt()` on the single-char input. Arduino's `String::toInt()` returns `0` whenever the leading char is non-numeric, which is the case for nearly every legal encoded byte (range `'!'..'{'`, ASCII 33..123 — only `'0'..'9'` parse). The resulting decoded values are `(0 - 33) * 4 = -132` for course and `~0` for speed for almost all real packets. The intended formulas are `((int)str[0] - 33) * 4` and `pow(1.08, (int)str[0] - 33) - 1`. `decodeBase91EncodedAltitude` and `decodeBase91EncodedLatitude` / `…Longitude` already use the correct `(int)char[0] - 33` form.

This affects `processReceivedPacket` for any compressed-format packet routed into the `'G'`/`'['` (course+speed) branch. The `LoRa_APRS_Tracker` consumer doesn't notice because it transmits but never decodes its own beacons; gateways and RX-side users do.

### Digipeating

`generateDigipeatedPacket` returns the literal string `"X"` to signal "not eligible to repeat" (path doesn't contain the requested hop). Callers rely on this sentinel — don't change it to empty string or a thrown error without auditing usage.

### Units on the wire vs. in the struct

APRS transmits speed in knots and altitude in feet; the decoders convert to km/h (×1.852) and meters (×0.3048) before populating `APRSPacket`. The most recent commit (`km-h to knots y meters to feet`) reworked these conversions, so be careful when touching `decodeSpeed`, `decodeAltitude`, `decodeBase91EncodedSpeed`, `decodeBase91EncodedAltitude`, or the Mic-E speed/altitude decoders — keep encode/decode units symmetric.

## Style observations from the existing code

- All public string I/O uses Arduino `String`, not `std::string` or `char*`. Don't introduce STL containers — they bloat on embedded targets.
- Functions return `String` by value freely; the existing code does not optimize for copies, and consistency matters more than micro-optimization here.
- Spanish-language comments (`// por repetidor?`, `// comportamiento seguro`) appear in the source. Leave them as-is unless asked to translate.

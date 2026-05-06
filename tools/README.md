# tools/

Helper scripts. Set up with `uv`:

```sh
uv venv tools/.venv --python 3.13
uv pip install --python tools/.venv/bin/python aprslib
```

## gen_aprs_vectors.py

Generates `test/common/aprs_vectors.h` — committed test vectors for
the compressed-position (Base91) format, derived from APRS Protocol
Reference v1.2 §9. The header contains:

- `kCourseVectors` / `kSpeedVectors` — single-byte (encoded, expected
  decode) pairs for the c/s slot.
- `kBase91FixedPoints` — five hand-picked sites (Munich, Cape Town,
  Auckland, North Pole, Equator-0) with the full set of base-91
  fields (lat, lon, c, s, altitude bytes) computed from the spec
  formulas.

Each row is cross-checked against aprslib's `parse()` before emission;
the script aborts loudly if aprslib disagrees with the spec formulas
(meaning the generator itself is wrong).

Unity tests under `test/test_base91_*/` consume the generated header.
Re-run after editing the case lists at the top of the script:

```sh
tools/.venv/bin/python tools/gen_aprs_vectors.py
```

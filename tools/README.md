# tools/

Helper scripts. Set up with `uv`:

```sh
uv venv tools/.venv --python 3.13
uv pip install --python tools/.venv/bin/python aprslib
```

## gen_decode_vectors.py

Generates `test/common/decode_vectors.h` — committed (encoded-byte,
expected-decode) pairs for the compressed-position c/s slot, derived
from APRS Protocol Reference v1.2 §9. Each row is cross-checked
against aprslib's `parse()` before emission; the script aborts loudly
if aprslib disagrees with the spec formula.

The Unity test in `test/test_base91_decode_course_speed/` consumes
the generated header. Re-run after editing the case lists at the top
of the script:

```sh
tools/.venv/bin/python tools/gen_decode_vectors.py
```

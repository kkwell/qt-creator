# Device adapter package v2

`embed-labs.device-adapter/v2` is a fail-closed package format. It does not
authorize device writes. Manual-control authorization, exact safe values,
timing, and fallback selection remain project-owned.

Every object has a closed set of required keys. Exact rational numbers use
canonical decimal strings:

```json
{"numerator":"-1","denominator":"1000"}
```

The numerator has no plus sign, leading zero, or negative zero. The denominator
is positive, has no leading zero, and the fraction is reduced. Engineering
transforms declare an explicit rounding mode. Exact action literals and
parameter defaults use a `{kind,value}` object; absent values are JSON `null`.

Set-like arrays must already be sorted and unique. Signal, profile, action, and
parameter collections are sorted by their complete identifier. Module profiles
are sorted by `(moduleIdent,id)`. Numeric PDO lists and signal bindings are
sorted by their numeric canonical keys. Action `steps` are the only ordered
business sequence and are never sorted by the loader.

The v2 `contentSha256` is:

```text
SHA-256("embed-labs.device-adapter/v2" || NUL || canonical-json(document))
```

Canonical JSON is UTF-8 without whitespace. Object keys are sorted by their
UTF-8 bytes, arrays retain input order, strings use the JSON escapes implemented
by the loader, and JSON numbers must be exactly representable integers. Values
requiring the signed or unsigned 64-bit domain are encoded as canonical decimal
strings. This makes object-key order and whitespace irrelevant while retaining
all semantic array ordering.

The bundled XB6 and SV630N packages remain Candidate packages. They contain no
manufacturer-unverified output safe value, no enabled action, and no real
hardware permission. `raw-drive-unit` deliberately does not imply rpm, encoder
counts, or rated-torque scaling.

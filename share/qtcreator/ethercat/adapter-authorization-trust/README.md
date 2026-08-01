# Device-adapter authorization roots

This directory is intentionally empty of production keys. A root must be an
exact raw 32-byte Ed25519 public key named `<sha256-of-raw-key>.pub`. Symbolic
links, duplicate key IDs, PEM, hexadecimal text, and files with trailing bytes
are rejected. These roots authorize only adapter-authorization policies; ECPKG
production trust keys are a separate trust domain and are never reused here.


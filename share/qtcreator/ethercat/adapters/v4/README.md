# Device Adapter manifests v4

The v4 manifests add signed project parameter definitions to the v3 Adapter
contract. Each definition binds its engineering constraint, configured
projection, observed source, and definition SHA-256. Authorization V2 closes
the ordered definition set into the Adapter authorization binding.

The bundled SV630N v0.4.0 manifest comes from the immutable API-075 production
handoff produced by commit
`707ee7b5f7edc61302cbb121c929054ea1883b95`. Its file SHA-256 is
`ec6c8d484672fd3b7fc4df5dee4ef51ad0446b8bb89768c3747eee892276a37e`,
and its canonical content SHA-256 is
`4524bb0a9b796f580c6012a95c1c77257f5afa31c0851cb65595295bf1f2c3bc`.
The matching Authorization V2 JSON and detached signature are installed under
`ethercat/adapter-authorizations`.

API-075 deliberately leaves all SV630N motion actions disabled and
unqualified. It proves only the production V4 identity, authorization, and
seven-parameter definition closure. Runtime motion remains unavailable until a
later signed production version also closes the compiler projection, actual
device parameter evidence, engineering-unit conversion, controller-owned
timed action, and Controlled Stop proof.

The whole `share/qtcreator/ethercat` directory is copied and installed
recursively by both `share/qtcreator/CMakeLists.txt` and `share/share.qbs`, so
this data-only addition requires no build-system source-list change.

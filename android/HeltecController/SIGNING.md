# Android release signing identity

All direct-distribution releases of `com.unkl3errl.helteccontroller` use the
same permanent signing identity:

| Field | Value |
| --- | --- |
| Alias | `heltec-controller` |
| Store format | PKCS#12 |
| Certificate subject | `CN=HeltecController, O=Unkl3Errl` |
| Key and signature | RSA 4096 / SHA-256 with RSA |
| Validity | 2026-07-30 through 2056-07-22 |
| Certificate SHA-256 | `15:17:B9:22:56:7D:55:7E:9E:71:B5:4A:14:1C:48:56:27:FD:27:50:CF:BC:F8:D1:40:75:C6:D0:AF:37:7C:A4` |

The private keystore and its passwords must remain outside Git and CI logs.
They must be backed up in separate secure locations. Losing the private key
prevents future APKs from upgrading installations signed with this identity.

The build reads signing material only from the four `HELTEC_RELEASE_*`
environment variables documented in [`README.md`](README.md). Builds without
all four variables remain unsigned; a partial signing configuration fails
immediately instead of silently producing the wrong artifact.

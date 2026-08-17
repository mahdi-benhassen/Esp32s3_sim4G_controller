# Production Security Profile

This document defines the security boundary between the repository’s safe development configuration and a provisioned field device. The default `sdkconfig.defaults` intentionally keeps Secure Boot and flash encryption disabled so ordinary CI builds remain reproducible and do not require a private signing key. A production image must instead be built with `sdkconfig.production-secure.defaults` and a signing key that is held outside the repository.

> Secure Boot v2 and release-mode flash encryption are device-provisioning operations, not ordinary firmware feature switches. They require a controlled manufacturing procedure, a recovery plan, and a board-specific acceptance record.

## Security layers

| Layer | Repository implementation | Production requirement |
|---|---|---|
| Settings and secrets | NVS encryption initialized from the dedicated `nvs_keys` partition and HMAC eFuse slot 0 | Provision and verify the HMAC key on every device; never reuse a device key across a fleet unless the threat model explicitly permits it |
| HTTPS private key | Loaded from the encrypted `b2tls` NVS namespace | Import with the controlled commissioning workflow; do not leave `server.key` on removable media |
| Firmware authenticity | `sdkconfig.production-secure.defaults` enables Secure Boot v2 signing | Protect the RSA signing key in an offline or controlled signing service; record key fingerprint and image digest |
| Flash confidentiality | Production profile enables release-mode flash encryption | Burn and verify eFuses only after the signed image, rollback path, and field recovery procedure have passed acceptance |
| Network control plane | State-changing HTTP endpoints require TLS plus a Bearer token; plain HTTP is diagnostics-only | Use a per-device token, rotate through an authenticated service procedure, and isolate the management network |
| Update safety | HTTPS OTA with rollback confirmation | Verify signed-image policy, rollback behavior, and power-loss recovery on the exact carrier revision |

## Build and signing workflow

Generate a new RSA signing key outside the repository and store it in a controlled manufacturing workspace. The expected path in the profile is `keys/secure_boot_signing_key.pem`, but `.gitignore` prevents that material from being committed. The CI workflow must not receive this private key unless a dedicated protected signing environment is deliberately introduced.

Use the production profile only after reviewing the target’s eFuse state and backup/recovery plan:

```bash
cd Esp32s3_sim4G_controller
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.production-secure.defaults"
export SECURE_BOOT_SIGNING_KEY="$PWD/keys/secure_boot_signing_key.pem"
idf.py reconfigure
idf.py build
```

The build artifact, signing-key fingerprint, ESP-IDF version, partition table, and Git commit must be recorded in the manufacturing release record. Do not flash a production image onto a development unit until the bootloader output and eFuse state have been independently reviewed.

## eFuse and first-boot gates

The exact `espefuse.py` commands depend on the ESP32-S3 module and the manufacturing fixture. The operator must inspect the device before writing eFuses, confirm that the flash size and voltage configuration are correct, and capture the pre-provisioning report. Secure Boot and release flash encryption must be enabled only through the documented ESP-IDF provisioning flow for the installed ESP-IDF version.

The acceptance record must contain the device identity, module revision, firmware digest, Secure Boot key digest, flash-encryption mode, NVS HMAC slot, and a signed statement that UART download-mode decryption and plaintext encryption paths are disabled. A successful first boot must be followed by a power-cycle test, an OTA update test, an intentional invalid-image rollback test, and a configuration-preservation test.

## TLS credential migration

At boot, the firmware first looks for the certificate and private key in encrypted NVS. If the encrypted namespace is empty and the SD card contains the legacy `server.crt` and `server.key` files, the firmware validates and imports them into the encrypted `b2tls` namespace and then attempts to delete the plaintext files. HTTPS is not started if the credentials are malformed. Plain HTTP may still expose read-only diagnostics, but it never exposes relay writes, rule writes, event export, self-test, or reboot.

The migration is intentionally one-way. If a legacy file cannot be removed because the SD card is read-only, the firmware logs a warning and the operator must physically remove the card or securely erase the files before deployment. The SD card must not be treated as a trust anchor after migration.

## Release checklist

Before a device is accepted for field deployment, the manufacturing engineer must verify the signed-image digest, Secure Boot status, flash-encryption status, NVS key state, HTTPS startup, authenticated rule CRUD, relay fail-safe behavior, OTA rollback, and the hardware acceptance checklist in `docs/hardware-integration.md`. Items requiring the actual carrier, modem variant, antenna, relay load, or EMC equipment remain open until measured and signed off on hardware.

## References

[1]: https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/security/secure-boot-v2.html "Espressif ESP-IDF v5.3.2 Secure Boot v2"
[2]: https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/security/flash-encryption.html "Espressif ESP-IDF v5.3.2 Flash Encryption"
[3]: https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/api-reference/storage/nvs_encryption.html "Espressif ESP-IDF v5.3.2 NVS Encryption"

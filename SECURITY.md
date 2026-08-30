# Security

## Reporting a problem

Please report security problems to Rafael Marques through a private channel.
Do not open a public issue with credentials, private endpoints, device names,
or details that would help someone reach a deployed unit.

Include the affected revision, the impact you observed, and steps to reproduce
the problem without real credentials. I will confirm receipt and coordinate a
fix before any public disclosure.

## Credential handling

AirSense reads deployment settings from `include/secrets.h`. Git ignores this
file. Start from `include/secrets.example.h`, keep the real file local, and do
not paste it into issues, logs, screenshots, or build artifacts.

Use a separate InfluxDB token for each deployment. Give it write access only to
the AirSense bucket. Rotate the token and the WiFiManager setup password if
either one may have been disclosed. Removing a value from Git does not revoke
it, so rotate any credential that has ever been committed.

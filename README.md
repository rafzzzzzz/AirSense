# AirSense

I built AirSense for my Computer Engineering degree project, without AI. I
wanted one small device that could compare indoor environmental readings,
show them locally, and preserve them for later analysis instead of leaving the
results on a serial console.

The work led to a peer-reviewed publication. The paper is available at
https://doi.org/10.1007/978-3-031-66635-3_23.

## What it does

AirSense runs on an ESP32. It reads four environmental sensor paths:

- A BME680 measures temperature, relative humidity, and pressure.
- A BME280 provides a second temperature, humidity, and pressure reading.
- An SCD41 provides a third temperature and humidity reading plus CO2.
- A PMS5003 sends particulate matter frames over UART. The firmware records
  PM1.0, PM2.5, PM10, and particle counts by size threshold.

The ESP32 displays current readings on a 128 by 160 ST7735 TFT. A rotary
encoder selects the upload interval. Three LEDs indicate humidity bands, and a
buzzer signals startup and connection states.

Each update becomes an InfluxDB point with a configured device tag and system
tag. InfluxDB stores the time series. I used Grafana to inspect and compare the
measurements, but the dashboard and hosted services are not part of this
repository.

The source also contains library references for an ENS160 path. That code is
disabled, so I do not describe it as a working sensor in the current build.

## Hardware validation

I physically tested the AirSense degree-project prototype as an integrated
device. The current repository does not include the lab notes or test logs
needed to tie every listed component to a specific test run. This cleanup has
not repeated the tests on hardware, and there are no automated
hardware-in-the-loop tests here.

## Build and flash

You need PlatformIO and an ESP32 development board compatible with the
`esp32doit-devkit-v1` environment.

1. Copy the example configuration:

   ```sh
   cp include/secrets.example.h include/secrets.h
   ```

2. Edit `include/secrets.h` with your own deployment settings. PlatformIO stops
   with a direct setup error if this file is missing.
3. Build the normal firmware:

   ```sh
   pio run -e esp32doit-devkit-v1
   ```

4. Connect the board and flash it:

   ```sh
   pio run -e esp32doit-devkit-v1 --target upload
   ```

5. On first boot, join the configured AirSense setup access point. Use the
   password from `WIFI_MANAGER_AP_PASSWORD`, then enter the target Wi-Fi
   network and the system name in the provisioning page.

Serial output is off in the normal build. For local diagnosis, build and flash
the `esp32doit-devkit-v1-debug` environment and monitor at 115200 baud:

```sh
pio run -e esp32doit-devkit-v1-debug
pio device monitor -b 115200
```

Debug output includes measurements and connection errors. Treat captured logs
as private deployment data.

## Configuration and credential safety

`include/secrets.h` holds the InfluxDB URL, token, organization, bucket,
measurement name, device name, default system name, and WiFiManager setup
credentials. Git ignores it. Commit only `include/secrets.example.h`, which has
placeholders.

Create a dedicated InfluxDB token with write access only to the bucket used by
this device. Do not use an all-access account token. Use a unique setup
password of at least eight characters for the WiFiManager access point.

The repository includes a Gitleaks configuration for checking the working
tree:

```sh
gitleaks dir . --config .gitleaks.toml --redact
```

If a real credential reaches Git, revoke or rotate it. Deleting the text from
the current source does not make an exposed credential safe again.

## Known limitations

- Wi-Fi provisioning blocks for up to 120 seconds during startup.
- The firmware restarts after three hours without Wi-Fi, but it does not queue
  readings while offline.
- Sensor initialization results are not checked, so a disconnected sensor may
  produce invalid readings rather than a clear on-screen fault.
- The BME680 gas-resistance and software IAQ path is disabled.
- Pin assignments and TFT driver settings are fixed in `src/main.cpp` and
  `platformio.ini`. Check them against your own build before powering hardware.
- Grafana dashboards, enclosure files, calibration records, and wiring
  diagrams are not included.

## License

AirSense is available under the MIT License. See `LICENSE`.

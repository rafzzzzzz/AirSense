#pragma once

// Copy this file to secrets.h, then replace every placeholder locally.
// Never commit secrets.h.
#define INFLUXDB_URL "https://your-influxdb-host.example"
#define INFLUXDB_TOKEN "replace-with-a-write-only-token"
#define INFLUXDB_ORG "replace-with-your-organization"
#define INFLUXDB_BUCKET "replace-with-your-bucket"
#define INFLUXDB_MEASUREMENT "air_quality"
#define DEVICE_ID "replace-with-a-device-name"
#define SYSTEM_NAME_DEFAULT "replace-location"

// WiFiManager requires an access-point password of at least eight characters.
#define WIFI_MANAGER_AP_NAME "AirSense Setup"
#define WIFI_MANAGER_AP_PASSWORD "replace-with-a-strong-password"

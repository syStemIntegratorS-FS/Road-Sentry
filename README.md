# River Monitoring System - DRM Dashboard

## ESP32-S3 Super Mini Status LED Guide

The built-in RGB LED provides instant visual feedback on the device's connection status. No need for a computer or phone - just look at the LED.

### LED Color Codes

| Status | Color | Pattern | Meaning |
|--------|-------|---------|---------|
| **Booting / Connecting** | 🟣 Purple | Solid | System starting up or establishing connection |
| **WiFi Failed** | 🔴 Red | Solid | No network connection - Check WiFi credentials/range |
| **MQTT Failed** | 🔴 Red | Blinking | Broker unreachable - Check MQTT server |
| **Weak Signal** | 🟡 Yellow | Solid | RSSI < -70dBm - Poor connection quality |
| **All Connected** | 🟢 Green | Solid | System operational - Normal operation |

### Quick Troubleshooting

| LED State | Action Required |
|-----------|-----------------|
| 🟣 Purple (stuck) | Device is still connecting - Wait or reboot |
| 🔴 Red Solid | Check WiFi credentials and router power |
| 🔴 Red Blinking | Verify MQTT broker address and credentials |
| 🟡 Yellow | Reposition device for better signal or check for interference |
| 🟢 Green | ✅ System is healthy - No action needed |

### Hardware Notes

- **RGB LED Type:** WS2812B (Addressable)
- **GPIO Pin:** 48
- **Color Order:** RGB

---

*Note: The RGB LED indicates ONLY connection/communication status. Flood alerts are handled by external MOSFET indicators (Pins 4, 5, 6) and siren (Pin 7).*

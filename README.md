# ESP32 MQTT Telemetry

Static web app for reading temperature, acceleration, latitude and longitude data from MQTT.

## Start

Open `index.html` in the browser. No dependencies need to be installed.

The browser cannot connect to plain MQTT TCP on port `1883`: you need a broker with MQTT over WebSocket, for example:

- `wss://broker.emqx.io:8084/mqtt`
- Mosquitto configured with a WebSocket listener
- HiveMQ, EMQX or a local broker with a WebSocket port enabled

## Expected Payload

Publish a JSON payload like this on the configured topic:

```json
{
  "temperature": 24.7,
  "acceleration": {
    "x": 0.01,
    "y": -0.03,
    "z": 0.98
  },
  "latitude": 53.349805,
  "longitude": -6.26031,
  "city": "Dublin",
  "street": "Dame Street",
  "photo": "data:image/jpeg;base64,/9j/4AAQSkZJRgABAQ..."
}
```

Common aliases are also accepted:

- temperature: `temperature`, `temp`, `t`
- acceleration: `acceleration.x/y/z`, `acc.x/y/z`, `accX`, `accY`, `accZ`, `ax`, `ay`, `az`
- coordinates: `latitude`, `lat`, `longitude`, `lng`, `lon`
- text location: `address`, `place`, `locationName`, or `street`/`road`/`via` + `city`/`town`/`village`
- OV2640 photo: `photo`, `image`, `frame`, `jpeg`, `jpg`, or `camera.image`

If the payload only contains latitude and longitude, the app tries to resolve the city and street with OpenStreetMap/Nominatim reverse geocoding. If the service is unavailable, the coordinates remain visible.

The photo can be:

- a full data URL: `data:image/jpeg;base64,...`
- a raw base64 JPEG starting with `/9j/`
- an `http://...` / `https://...` URL pointing to the image

When a new photo arrives, it replaces the previous one. If only sensor data arrives, the latest photo stays on screen.

Each new photo is also added to the in-page **Photo history**, with a thumbnail and reception time. The history keeps the latest 24 shots and is cleared if you close or reload the page, or by pressing **Clear history**.

## ESP32 Example

```cpp
client.publish("esp32/telemetry",
  "{\"temperature\":24.7,\"acceleration\":{\"x\":0.01,\"y\":-0.03,\"z\":0.98},\"latitude\":53.349805,\"longitude\":-6.26031}");
```

For images over MQTT, use moderate JPEG resolution and quality because the base64 payload is larger than the original binary.

# ESP32 MQTT Telemetria

App web statica per leggere da MQTT i dati di temperatura, accelerazione, latitudine e longitudine.

## Avvio

Apri `index.html` nel browser. Non serve installare dipendenze.

Il browser non può collegarsi a MQTT TCP puro sulla porta `1883`: serve un broker con MQTT over WebSocket, per esempio:

- `wss://broker.emqx.io:8084/mqtt`
- Mosquitto configurato con listener WebSocket
- HiveMQ, EMQX o broker locale con porta WebSocket abilitata

## Payload atteso

Pubblica sul topic configurato un JSON simile:

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
  "city": "Dublino",
  "street": "Dame Street",
  "photo": "data:image/jpeg;base64,/9j/4AAQSkZJRgABAQ..."
}
```

Sono accettati anche alias comuni:

- temperatura: `temperature`, `temp`, `t`
- accelerazione: `acceleration.x/y/z`, `acc.x/y/z`, `accX`, `accY`, `accZ`, `ax`, `ay`, `az`
- coordinate: `latitude`, `lat`, `longitude`, `lng`, `lon`
- posizione testuale: `address`, `place`, `locationName`, oppure `street`/`road`/`via` + `city`/`town`/`village`
- foto OV2640: `photo`, `image`, `frame`, `jpeg`, `jpg`, oppure `camera.image`

Se il payload contiene solo latitudine e longitudine, l'app prova a ricavare città e via con reverse geocoding OpenStreetMap/Nominatim. Se il servizio non è raggiungibile, restano visibili le coordinate.

La foto può essere:

- una data URL completa: `data:image/jpeg;base64,...`
- un JPEG base64 puro che inizia con `/9j/`
- un URL `http://...` / `https://...` verso l'immagine

Quando arriva una nuova foto sostituisce quella precedente. Se arrivano solo dati sensore, l'ultima foto resta a schermo.

Ogni nuova foto viene anche aggiunta allo **Storico foto** in memoria della pagina, con una miniatura e l'ora di ricezione. Lo storico mantiene gli ultimi 24 scatti e si svuota se chiudi o ricarichi la pagina, oppure premendo **Svuota storico**.

## Esempio ESP32

```cpp
client.publish("esp32/telemetry",
  "{\"temperature\":24.7,\"acceleration\":{\"x\":0.01,\"y\":-0.03,\"z\":0.98},\"latitude\":53.349805,\"longitude\":-6.26031}");
```

Per le immagini via MQTT conviene usare risoluzione e qualità JPEG moderate, perché il payload base64 è più grande del binario originale.

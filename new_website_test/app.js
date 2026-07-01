const els = {
  brokerUrl: document.querySelector("#brokerUrl"),
  topic: document.querySelector("#topic"),
  clientId: document.querySelector("#clientId"),
  connectBtn: document.querySelector("#connectBtn"),
  clearLogBtn: document.querySelector("#clearLogBtn"),
  clearPhotosBtn: document.querySelector("#clearPhotosBtn"),
  connectionStatus: document.querySelector("#connectionStatus"),
  temperatureValue: document.querySelector("#temperatureValue"),
  temperatureGauge: document.querySelector("#temperatureGauge"),
  temperatureHint: document.querySelector("#temperatureHint"),
  accX: document.querySelector("#accX"),
  accY: document.querySelector("#accY"),
  accZ: document.querySelector("#accZ"),
  barX: document.querySelector("#barX"),
  barY: document.querySelector("#barY"),
  barZ: document.querySelector("#barZ"),
  latitudeValue: document.querySelector("#latitudeValue"),
  longitudeValue: document.querySelector("#longitudeValue"),
  positionAddress: document.querySelector("#positionAddress"),
  mapsLink: document.querySelector("#mapsLink"),
  mapLabel: document.querySelector("#mapLabel"),
  mapAddress: document.querySelector("#mapAddress"),
  mapMarker: document.querySelector("#mapMarker"),
  lastUpdate: document.querySelector("#lastUpdate"),
  cameraImage: document.querySelector("#cameraImage"),
  cameraEmpty: document.querySelector("#cameraEmpty"),
  cameraTime: document.querySelector("#cameraTime"),
  cameraFormat: document.querySelector("#cameraFormat"),
  photoCount: document.querySelector("#photoCount"),
  photoStrip: document.querySelector("#photoStrip"),
  payloadTable: document.querySelector("#payloadTable"),
};

let mqttClient = null;
let messages = [];
let photoHistory = [];
let selectedPhotoId = null;
let reverseGeocodeTimer = null;
let lastReverseGeocodeKey = "";
let lastReverseGeocodeAt = 0;

els.clientId.value = `web-esp32-${Math.random().toString(16).slice(2, 8)}`;
initCursorGlow();

function setStatus(text, mode = "") {
  els.connectionStatus.className = `status-pill ${mode}`.trim();
  els.connectionStatus.innerHTML = `<span class="status-dot"></span>${text}`;
}

function initCursorGlow() {
  if (window.matchMedia("(prefers-reduced-motion: reduce)").matches) return;

  const surfaces = document.querySelectorAll(".topbar, .connection-panel, .metric-card, .payload-section, .camera-section, .photo-history");
  surfaces.forEach((surface) => {
    surface.classList.add("glow-surface");
    surface.addEventListener("pointerenter", () => {
      surface.style.setProperty("--cursor-glow", "1");
    });
    surface.addEventListener("pointerleave", () => {
      surface.style.setProperty("--cursor-glow", "0");
    });
  });
}

function parseBrokerUrl(rawUrl) {
  const url = new URL(rawUrl.trim());
  const path = `${url.pathname || "/mqtt"}${url.search || ""}`;
  return {
    host: url.hostname,
    port: Number(url.port || (url.protocol === "wss:" ? 443 : 80)),
    path,
    ssl: url.protocol === "wss:",
  };
}

function connectMqtt() {
  if (!window.Paho?.MQTT?.Client) {
    setStatus("Libreria MQTT non caricata", "error");
    return;
  }

  if (mqttClient?.isConnected()) {
    mqttClient.disconnect();
    mqttClient = null;
    els.connectBtn.lastChild.textContent = " Connect";
    setStatus("Disconnected");
    return;
  }

  try {
    const { host, port, path, ssl } = parseBrokerUrl(els.brokerUrl.value);
    mqttClient = new Paho.MQTT.Client(host, port, path, els.clientId.value.trim());

    mqttClient.onConnectionLost = (response) => {
      const message = response.errorMessage ? `Connection lost: ${response.errorMessage}` : "Disconnected";
      setStatus(message, response.errorMessage ? "error" : "");
      els.connectBtn.lastChild.textContent = " Connect";
    };

    mqttClient.onMessageArrived = (message) => {
      handlePayload(message.payloadString, message.destinationName);
    };

    setStatus("Connecting...");
    mqttClient.connect({
      useSSL: ssl,
      timeout: 8,
      onSuccess: () => {
        mqttClient.subscribe(els.topic.value.trim());
        setStatus("Connected", "connected");
        els.connectBtn.lastChild.textContent = " Disconnect";
      },
      onFailure: (error) => {
        setStatus(`Error: ${error.errorMessage || "connection failed"}`, "error");
      },
    });
  } catch (error) {
    setStatus(`Invalid broker URL`, "error");
  }
}

function handlePayload(rawPayload, topic) {
  const data = normalizePayload(rawPayload);
  const receivedAt = new Date();

  updateMetrics(data, receivedAt);
  messages.unshift({ rawPayload: compactPayload(rawPayload), topic, receivedAt, data });
  messages = messages.slice(0, 30);
  renderLog();
}

function normalizePayload(rawPayload) {
  let parsed = {};
  try {
    parsed = JSON.parse(rawPayload);
  } catch {
    parsed = {};
  }

  const acceleration = parsed.acceleration || parsed.acc || {};
  const photo = imageFromPayload(parsed, rawPayload);
  const address = addressFromPayload(parsed);

  return {
    temperature: numberFrom(parsed.temperature, parsed.temp, parsed.t),
    accX: numberFrom(acceleration.x, parsed.accX, parsed.ax),
    accY: numberFrom(acceleration.y, parsed.accY, parsed.ay),
    accZ: numberFrom(acceleration.z, parsed.accZ, parsed.az),
    latitude: numberFrom(parsed.latitude, parsed.lat),
    longitude: numberFrom(parsed.longitude, parsed.lng, parsed.lon),
    address,
    photo: photo?.src || null,
    photoFormat: photo?.format || null,
  };
}

function numberFrom(...values) {
  for (const value of values) {
    const number = Number(value);
    if (Number.isFinite(number)) return number;
  }
  return null;
}

function updateMetrics(data, receivedAt) {
  if (data.temperature !== null) {
    els.temperatureValue.textContent = data.temperature.toFixed(1);
    els.temperatureGauge.style.width = `${Math.max(0, Math.min(100, ((data.temperature + 10) / 60) * 100))}%`;
    els.temperatureHint.textContent = temperatureHint(data.temperature);
  }

  updateAxis(els.accX, els.barX, data.accX);
  updateAxis(els.accY, els.barY, data.accY);
  updateAxis(els.accZ, els.barZ, data.accZ);

  if (data.latitude !== null && data.longitude !== null) {
    els.latitudeValue.textContent = data.latitude.toFixed(6);
    els.longitudeValue.textContent = data.longitude.toFixed(6);
    els.mapLabel.textContent = `${data.latitude.toFixed(4)}, ${data.longitude.toFixed(4)}`;
    els.mapsLink.href = `https://www.openstreetmap.org/?mlat=${data.latitude}&mlon=${data.longitude}#map=16/${data.latitude}/${data.longitude}`;
    els.mapsLink.classList.add("ready");
    moveMarker(data.latitude, data.longitude);
    updateAddress(data.address || "Searching location...", data.address ? "" : "loading");
    if (!data.address) scheduleReverseGeocode(data.latitude, data.longitude);
  }

  if (data.photo) {
    const photo = savePhoto(data.photo, data.photoFormat, receivedAt);
    showPhoto(photo);
    renderPhotoHistory();
  }

  els.lastUpdate.textContent = receivedAt.toLocaleTimeString("en-GB");
}

function updateAddress(text, mode = "") {
  els.positionAddress.textContent = text;
  els.mapAddress.textContent = text;
  els.positionAddress.className = `address-value ${mode}`.trim();
}

function addressFromPayload(parsed) {
  const gps = parsed.gps || parsed.location || parsed.position || {};
  const direct = firstText(parsed.address, parsed.place, parsed.locationName, gps.address, gps.place, gps.name);
  if (direct) return direct;

  const street = firstText(parsed.street, parsed.road, parsed.via, gps.street, gps.road, gps.via);
  const houseNumber = firstText(parsed.houseNumber, parsed.house_number, gps.houseNumber, gps.house_number);
  const city = firstText(parsed.city, parsed.town, parsed.village, parsed.municipality, gps.city, gps.town, gps.village, gps.municipality);
  const postcode = firstText(parsed.postcode, parsed.zip, gps.postcode, gps.zip);

  const streetLine = [street, houseNumber].filter(Boolean).join(" ");
  return [streetLine, city, postcode].filter(Boolean).join(", ") || null;
}

function scheduleReverseGeocode(latitude, longitude) {
  const key = `${latitude.toFixed(4)},${longitude.toFixed(4)}`;
  const now = Date.now();

  if (key === lastReverseGeocodeKey && now - lastReverseGeocodeAt < 60000) return;
  clearTimeout(reverseGeocodeTimer);
  reverseGeocodeTimer = setTimeout(() => reverseGeocode(latitude, longitude, key), 900);
}

async function reverseGeocode(latitude, longitude, key) {
  lastReverseGeocodeKey = key;
  lastReverseGeocodeAt = Date.now();

  try {
    const params = new URLSearchParams({
      format: "jsonv2",
      lat: String(latitude),
      lon: String(longitude),
      zoom: "18",
      addressdetails: "1",
      "accept-language": "it",
    });
    const response = await fetch(`https://nominatim.openstreetmap.org/reverse?${params.toString()}`);
    if (!response.ok) throw new Error("reverse geocoding failed");

    const result = await response.json();
    const label = formatReverseAddress(result.address, result.display_name);
    updateAddress(label || "Location not found", label ? "" : "muted");
  } catch {
    updateAddress("Location unavailable", "muted");
  }
}

function formatReverseAddress(address = {}, fallback = "") {
  const street = firstText(address.road, address.pedestrian, address.footway, address.cycleway, address.path);
  const houseNumber = firstText(address.house_number);
  const city = firstText(address.city, address.town, address.village, address.municipality, address.county);
  const postcode = firstText(address.postcode);
  const streetLine = [street, houseNumber].filter(Boolean).join(" ");
  const compact = [streetLine, city, postcode].filter(Boolean).join(", ");

  if (compact) return compact;
  return fallback.split(",").slice(0, 3).join(", ").trim();
}

function savePhoto(src, format, receivedAt) {
  const photo = {
    id: crypto.randomUUID?.() || `${receivedAt.getTime()}-${Math.random().toString(16).slice(2)}`,
    src,
    format: format || "Immagine",
    receivedAt,
  };
  photoHistory.unshift(photo);
  photoHistory = photoHistory.slice(0, 24);
  selectedPhotoId = photo.id;
  return photo;
}

function showPhoto(photo) {
  selectedPhotoId = photo.id;
  els.cameraImage.src = photo.src;
  els.cameraImage.classList.add("ready");
  els.cameraEmpty.hidden = true;
  els.cameraTime.textContent = photo.receivedAt.toLocaleTimeString("en-GB");
  els.cameraFormat.textContent = photo.format;
}

function renderPhotoHistory() {
  const count = photoHistory.length;
  els.photoCount.textContent = count === 0
    ? "No photos saved in memory"
    : `${count} ${count === 1 ? "photo saved" : "photos saved"} in memory`;

  if (!count) {
    els.photoStrip.innerHTML = `<div class="photo-empty">Received photos will appear here.</div>`;
    return;
  }

  els.photoStrip.replaceChildren(...photoHistory.map((photo) => {
    const button = document.createElement("button");
    button.className = `photo-thumb ${photo.id === selectedPhotoId ? "selected" : ""}`.trim();
    button.dataset.photoId = photo.id;
    button.type = "button";

    const img = document.createElement("img");
    img.src = photo.src;
    img.alt = `OV2640 photo received at ${photo.receivedAt.toLocaleTimeString("en-GB")}`;

    const time = document.createElement("span");
    time.textContent = photo.receivedAt.toLocaleTimeString("en-GB");

    button.append(img, time);
    return button;
  }));
}

function imageFromPayload(parsed, rawPayload) {
  const camera = parsed.camera || {};
  const candidate = firstText(
    parsed.photo,
    parsed.image,
    parsed.frame,
    parsed.jpeg,
    parsed.jpg,
    camera.photo,
    camera.image,
    camera.frame,
    camera.jpeg
  );

  if (candidate) return normalizeImage(candidate);

  const raw = rawPayload.trim();
  if (/^data:image\//.test(raw) || looksLikeBase64Jpeg(raw)) {
    return normalizeImage(raw);
  }

  return null;
}

function firstText(...values) {
  return values.find((value) => typeof value === "string" && value.trim().length > 0)?.trim();
}

function normalizeImage(value) {
  if (/^data:image\//.test(value)) {
    const format = value.slice(5, value.indexOf(";") > -1 ? value.indexOf(";") : value.indexOf(","));
    return { src: value, format };
  }

  if (/^https?:\/\//.test(value) || /^blob:/.test(value)) {
    return { src: value, format: "URL" };
  }

  if (looksLikeBase64Jpeg(value)) {
    return { src: `data:image/jpeg;base64,${value}`, format: "JPEG" };
  }

  return null;
}

function looksLikeBase64Jpeg(value) {
  const compact = value.replaceAll(/\s/g, "");
  return compact.length > 80 && compact.startsWith("/9j/");
}

function compactPayload(rawPayload) {
  if (looksLikeBase64Jpeg(rawPayload.trim())) {
    return "[base64 JPEG image]";
  }

  try {
    const parsed = JSON.parse(rawPayload);
    const clone = structuredClone(parsed);
    compactImageFields(clone);
    return JSON.stringify(clone);
  } catch {
    return rawPayload;
  }
}

function compactImageFields(value) {
  if (!value || typeof value !== "object") return;

  for (const key of Object.keys(value)) {
    const normalized = key.toLowerCase();
    if (["photo", "image", "frame", "jpeg", "jpg"].includes(normalized) && typeof value[key] === "string") {
      value[key] = `[image ${Math.round(value[key].length / 1024)} KB]`;
    } else {
      compactImageFields(value[key]);
    }
  }
}

function temperatureHint(value) {
  if (value < 5) return "Low temperature";
  if (value > 35) return "High temperature";
  return "Value within operating range";
}

function updateAxis(label, bar, value) {
  if (value === null) return;

  const clamped = Math.max(-2, Math.min(2, value));
  const width = Math.abs(clamped) * 25;
  label.textContent = value.toFixed(2);
  bar.style.width = `${width}%`;
  bar.style.left = clamped < 0 ? `${50 - width}%` : "50%";
}

function moveMarker(latitude, longitude) {
  const left = 10 + (((longitude + 180) % 360) / 360) * 80;
  const top = 10 + ((90 - latitude) / 180) * 80;
  els.mapMarker.style.left = `${Math.max(10, Math.min(90, left))}%`;
  els.mapMarker.style.top = `${Math.max(12, Math.min(88, top))}%`;
}

function renderLog() {
  if (!messages.length) {
    els.payloadTable.innerHTML = `<tr class="empty-row"><td colspan="7">No messages received.</td></tr>`;
    return;
  }

  els.payloadTable.innerHTML = messages.map((message) => {
    const { data } = message;
    const temp = data.temperature === null ? "--" : `${data.temperature.toFixed(1)} °C`;
    const acc = [data.accX, data.accY, data.accZ]
      .map((value) => (value === null ? "--" : value.toFixed(2)))
      .join(" / ");
    const gps = data.latitude === null || data.longitude === null
      ? "--"
      : `${data.latitude.toFixed(5)}, ${data.longitude.toFixed(5)}`;
    const photo = data.photo ? data.photoFormat || "si" : "--";

    return `
      <tr>
        <td>${message.receivedAt.toLocaleTimeString("en-GB")}</td>
        <td>${escapeHtml(message.topic)}</td>
        <td>${temp}</td>
        <td>${acc}</td>
        <td>${gps}</td>
        <td>${escapeHtml(photo)}</td>
        <td><code title="${escapeHtml(message.rawPayload)}">${escapeHtml(message.rawPayload)}</code></td>
      </tr>
    `;
  }).join("");
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

els.connectBtn.addEventListener("click", connectMqtt);
els.clearLogBtn.addEventListener("click", () => {
  messages = [];
  renderLog();
});
els.clearPhotosBtn.addEventListener("click", () => {
  photoHistory = [];
  selectedPhotoId = null;
  els.cameraImage.removeAttribute("src");
  els.cameraImage.classList.remove("ready");
  els.cameraEmpty.hidden = false;
  els.cameraTime.textContent = "--";
  els.cameraFormat.textContent = "--";
  renderPhotoHistory();
});
els.photoStrip.addEventListener("click", (event) => {
  const button = event.target.closest("[data-photo-id]");
  if (!button) return;

  const photo = photoHistory.find((item) => item.id === button.dataset.photoId);
  if (!photo) return;

  showPhoto(photo);
  renderPhotoHistory();
});

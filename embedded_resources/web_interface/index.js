function $(s) {
  return document.querySelector(s);
}
const IS_DEV = window.location.host === "127.0.0.1:8080";
const T = {
  master: $("#t"),
  fileRow: function () {
    const tmp = document.createElement("template");
    tmp.innerHTML =
      this.master.content.querySelector("table tr.file-row").outerHTML;
    return tmp.content;
  },
  pathRow: function () {
    const tmp = document.createElement("template");
    tmp.innerHTML =
      this.master.content.querySelector("table tr.path-row").outerHTML;
    return tmp.content;
  },
  uploadLoading: function () {
    const tmp = document.createElement("template");
    tmp.innerHTML =
      this.master.content.querySelector(".upload-loading").outerHTML;
    return tmp.content;
  },
};

const EXECUTABLE = {
  ir: "ir tx_from_file",
  sub: "subghz tx_from_file",
  js: "js run_from_file",
  bjs: "js run_from_file",
  txt: "badusb run_from_file",
  mp3: "play",
  wav: "play",
};

const Dialog = {
  _bg: function (show) {
    let bg = $(".dialog-background");
    let dialogs = document.querySelectorAll(".dialog");
    dialogs.forEach((dialog) => {
      if (!dialog.classList.contains("hidden")) dialog.classList.add("hidden");
    });
    if (show) {
      bg.classList.remove("hidden");
    } else {
      bg.classList.add("hidden");
    }
  },
  show: function (dialogName) {
    this._bg(true);
    let dialog = $(".dialog." + dialogName);
    dialog.classList.remove("hidden");
  },
  hide: function () {
    this._bg(false);
    this.loading.hide();

    if (currentDrive && currentPath) {
      updateURL(currentDrive, currentPath, null);
    }
  },
  loading: {
    show: function (message) {
      $(".loading-area").classList.remove("hidden");
      $(".loading-area .text").textContent = message || "Loading...";
    },
    hide: function () {
      $(".loading-area").classList.add("hidden");
    },
  },
  showOneInput: function (name, inputVal, data) {
    const dbForm = {
      renameFolder: {
        title: "Rename Folder: " + inputVal,
        label: `New Folder Name:`,
        action: "Rename",
      },
      renameFile: {
        title: "Rename File: " + inputVal,
        label: `New File Name:`,
        action: "Rename",
      },
      createFolder: {
        title: "Create Folder",
        label: `Folder Name:`,
        action: "Create Folder",
      },
      createFile: {
        title: "Create File",
        label: `File Name:`,
        action: "Create File",
      },
      serial: {
        title: "Serial Command",
        label: `Command:`,
        action: "Run",
      },
    };

    let config = dbForm[name];
    if (!config) {
      alert("Invalid dialog name: " + name);
      console.error("Dialog.showOneInput: Invalid dialog name", name);
      return;
    }

    let dialog = $(".dialog.oinput");
    dialog.setAttribute("data-cache", data);
    dialog.querySelector(".oinput-title").textContent = config.title;
    dialog.querySelector(".oinput-label").textContent = config.label;
    dialog.querySelector("#oinput-input").value = inputVal;
    dialog.querySelector(".act-save-oinput-file").textContent = config.action;
    this.show("oinput");
    dialog.querySelector("#oinput-input").select();
    return dialog;
  },
};

function handleAuthError() {
  if (
    confirm(
      "Session expired or unauthorized. Would you like to go to the login page?",
    )
  ) {
    window.location.href = "/";
  } else {
    Dialog.loading.hide();
  }
}

async function requestGet(url, data) {
  return new Promise((resolve, reject) => {
    let req = new XMLHttpRequest();
    let realUrl = url;
    if (IS_DEV) realUrl = "/bruce" + url;
    if (data) {
      let urlParams = new URLSearchParams(data);
      realUrl += "?" + urlParams.toString();
    }
    req.open("GET", realUrl, true);
    req.onload = () => {
      if (req.status >= 200 && req.status < 300) {
        resolve(req.responseText);
      } else if (req.status === 401) {
        handleAuthError();
        reject(new Error(`Unauthorized access (401)`));
      } else {
        let message = `Request failed with status ${req.status}`;
        try {
          const response = JSON.parse(req.responseText);
          if (response.error) message = response.error;
        } catch (_) {}
        reject(new Error(message));
      }
    };
    req.onerror = () => {
      reject(new Error("Network error"));
    };
    req.send();
  });
}

async function requestPost(url, data) {
  return new Promise((resolve, reject) => {
    let fd = new FormData();
    for (let key in data) {
      if (data.hasOwnProperty(key)) fd.append(key, data[key]);
    }

    let realUrl = url;
    if (IS_DEV) realUrl = "/bruce" + url;
    let req = new XMLHttpRequest();
    req.open("POST", realUrl, true);
    req.onload = () => {
      if (req.status >= 200 && req.status < 300) {
        resolve(req.responseText);
      } else if (req.status === 401) {
        handleAuthError();
        reject(new Error(`Unauthorized access (401)`));
      } else {
        let message = `Request failed with status ${req.status}`;
        try {
          const response = JSON.parse(req.responseText);
          if (response.error) message = response.error;
        } catch (_) {}
        reject(new Error(message));
      }
    };
    req.onerror = () => reject(new Error("Network error"));
    req.send(fd);
  });
}

function stringToId(str) {
  let hash = 0,
    i,
    chr;
  if (str.length === 0) return hash.toString();
  for (i = 0; i < str.length; i++) {
    chr = str.charCodeAt(i);
    hash = (hash << 5) - hash + chr;
    hash |= 0; // Convert to 32bit integer
  }
  return "id_" + Math.abs(hash);
}

const _queueUpload = [];
let _runningUpload = false;
function appendFileToQueue(files) {
  Dialog.show("upload");
  let d = $(".dialog.upload");
  for (let i = 0; i < files.length; i++) {
    let file = files[i];
    let filename = file.webkitRelativePath || file.name;
    let fileId = stringToId(filename);
    let progressBar = T.uploadLoading();
    progressBar.querySelector(".upload-name").textContent = filename;
    progressBar
      .querySelector(".upload-loading .bar")
      .setAttribute("id", fileId);

    d.querySelector(".dialog-body").appendChild(progressBar);
  }
}
async function appendDroppedFiles(entry) {
  return new Promise((resolve, reject) => {
    if (entry.isFile) {
      entry.file((file) => {
        let fileWithPath = new File([file], entry.fullPath.substring(1), {
          type: file.type,
        });
        appendFileToQueue([fileWithPath]);
        _queueUpload.push(fileWithPath);
        resolve();
      });
    } else if (entry.isDirectory) {
      let proms = [];
      let reader = entry.createReader();
      reader.readEntries((entries) => {
        for (let e of entries) proms.push(appendDroppedFiles(e));
      });

      Promise.all(proms).then(resolve);
    }
  });
}
async function uploadFile() {
  if (_queueUpload.length === 0) {
    _runningUpload = false;
    $(".dialog.upload .dialog-body").innerHTML = "";
    fetchSystemInfo();
    fetchFiles(currentDrive, currentPath);
    Dialog.hide();
    return;
  }

  return new Promise((resolve, reject) => {
    _runningUpload = true;
    let file = _queueUpload.shift();
    let fd = new FormData();
    let filename = file.webkitRelativePath || file.name;
    let fileId = stringToId(filename);
    fd.append("file", file, filename);
    fd.append("folder", currentPath);
    fd.append("fs", currentDrive);

    let realUrl = `/upload`;
    if (IS_DEV) realUrl = "/bruce" + realUrl;
    let req = new XMLHttpRequest();
    req.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        var percent = (e.loaded / e.total) * 100;
        $("#" + fileId).style.width = Math.round(percent) + "%";
      }
    };
    req.onload = () => {
      uploadFile();
      if (req.status >= 200 && req.status < 300) {
        resolve(req.responseText);
      } else {
        reject();
      }
    };
    req.onabort = () => reject();
    req.onerror = () => reject();
    req.open("POST", realUrl, true);
    req.send(fd);
  });
}

async function runCommand(cmd) {
  Dialog.loading.show("Running command...");
  try {
    await requestPost("/cm", { cmnd: cmd });
  } catch (error) {
    alert("Failed to run command: " + error.message);
  } finally {
    Dialog.loading.hide();
  }
}

let heltecGpsActive = false;
let heltecLoraActive = false;
let heltecLoraStatus = {};
let heltecFieldLogActive = false;
let heltecFieldOptionsInitialized = false;
let heltecStatusTimer = null;
let heltecWifiGeneration = -1;

function setHeltecText(id, value) {
  const element = $(id);
  if (element) element.textContent = value;
}

function formatHeltecNumber(value, digits = 1) {
  return Number.isFinite(Number(value)) ? Number(value).toFixed(digits) : "--";
}

function formatHeltecAge(milliseconds) {
  const seconds = Math.max(0, Math.floor((Number(milliseconds) || 0) / 1000));
  if (seconds < 60) return `${seconds}s ago`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m ago`;
  return `${Math.floor(minutes / 60)}h ago`;
}

function formatHeltecDuration(milliseconds) {
  let seconds = Math.max(0, Math.floor((Number(milliseconds) || 0) / 1000));
  const days = Math.floor(seconds / 86400);
  seconds %= 86400;
  const hours = Math.floor(seconds / 3600);
  seconds %= 3600;
  const minutes = Math.floor(seconds / 60);
  seconds %= 60;
  if (days) return `${days}d ${hours}h ${minutes}m`;
  if (hours) return `${hours}h ${minutes}m`;
  if (minutes) return `${minutes}m ${seconds}s`;
  return `${seconds}s`;
}

function formatHeltecBytes(bytes) {
  let value = Math.max(0, Number(bytes) || 0);
  const units = ["B", "KiB", "MiB", "GiB"];
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function downloadHeltecFile(contents, mimeType, prefix, extension) {
  const blob = new Blob([contents], { type: mimeType });
  const link = document.createElement("a");
  const objectUrl = URL.createObjectURL(blob);
  link.href = objectUrl;
  link.download = `${prefix}-${new Date().toISOString().replace(/[:.]/g, "-")}.${extension}`;
  link.click();
  setTimeout(() => URL.revokeObjectURL(objectUrl), 0);
}

function heltecGpsTrackToGpx(track) {
  const points = Array.isArray(track.points) ? track.points : [];
  const trackPoints = points
    .filter(
      (point) =>
        Number.isFinite(Number(point.latitude)) && Number.isFinite(Number(point.longitude)),
    )
    .map((point) => {
      const details = [];
      if (point.altitudeMeters !== undefined) {
        details.push(`<ele>${Number(point.altitudeMeters).toFixed(2)}</ele>`);
      }
      if (point.satellites) details.push(`<sat>${Number(point.satellites)}</sat>`);
      if (point.hdop !== undefined) details.push(`<hdop>${Number(point.hdop).toFixed(2)}</hdop>`);
      if (/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{2}Z$/.test(point.utc || "")) {
        details.push(`<time>${point.utc}</time>`);
      }
      const speed =
        point.speedKmph !== undefined ? `; speed ${Number(point.speedKmph).toFixed(2)} km/h` : "";
      details.push(`<desc>Captured ${Number(point.capturedAtMs) || 0} ms after boot${speed}</desc>`);
      return `      <trkpt lat="${Number(point.latitude).toFixed(7)}" lon="${Number(point.longitude).toFixed(7)}">${details.join("")}</trkpt>`;
    })
    .join("\n");
  return `<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="Bruce Heltec V4" xmlns="http://www.topografix.com/GPX/1/1">
  <trk>
    <name>Bruce Heltec V4 GPS track</name>
    <trkseg>
${trackPoints}
    </trkseg>
  </trk>
</gpx>
`;
}

function renderHeltecBoardStatus(status) {
  setHeltecText("#heltec-board-name", status.board || "Heltec WiFi LoRa 32 V4");
  const firmware = status.firmware || {};
  const firmwareLabel = firmware.commit
    ? `${firmware.version || "unknown"} (${firmware.commit})`
    : firmware.version || "unknown";
  setHeltecText("#heltec-firmware", firmwareLabel);

  const system = status.system || {};
  const heap = system.heap || {};
  const psram = system.psram || {};
  setHeltecText("#heltec-uptime", formatHeltecDuration(system.uptimeMs));
  setHeltecText(
    "#heltec-heap",
    `${formatHeltecBytes(heap.freeBytes)} free / ${formatHeltecBytes(heap.totalBytes)}`,
  );
  setHeltecText(
    "#heltec-psram",
    psram.present
      ? `${formatHeltecBytes(psram.freeBytes)} free / ${formatHeltecBytes(psram.totalBytes)}`
      : "not detected",
  );

  const network = status.network || {};
  const clientCount = Number(network.connectedClients) || 0;
  setHeltecText(
    "#heltec-network",
    network.apActive
      ? `${network.ssid || "AP"} | ${network.ip || "--"} | ch ${network.channel || "--"} | ${clientCount} ${clientCount === 1 ? "client" : "clients"}`
      : network.mode || "off",
  );

  const battery = status.battery || {};
  setHeltecText(
    "#heltec-battery",
    battery.present ? `${battery.millivolts} mV (${battery.percent}%)` : "not detected",
  );

  const sx1262 = status.sx1262 || {};
  setHeltecText("#heltec-radio-boot", sx1262.ok ? "OK" : `error ${sx1262.receiveCode}`);

  const gps = status.gps || {};
  setHeltecText(
    "#heltec-gps-boot",
    gps.nmeaSentences > 0 ? `${gps.nmeaSentences} NMEA${gps.fix ? ", fix" : ""}` : "no data",
  );
  const live = gps.live || {};
  const state = gps.monitorState || "off";
  heltecGpsActive = state === "running" || state === "starting";
  setHeltecText("#heltec-gps-state", state.charAt(0).toUpperCase() + state.slice(1));

  if (live.fix && Number.isFinite(Number(live.latitude))) {
    setHeltecText(
      "#heltec-gps-position",
      `${Number(live.latitude).toFixed(6)}, ${Number(live.longitude).toFixed(6)}`,
    );
    setHeltecText(
      "#heltec-gps-detail",
      `${live.satellites || 0} satellites | HDOP ${formatHeltecNumber(live.hdop)} | ${formatHeltecNumber(live.altitudeMeters)} m`,
    );
  } else if (heltecGpsActive) {
    setHeltecText("#heltec-gps-position", "Waiting for position fix");
    setHeltecText(
      "#heltec-gps-detail",
      `${live.satellites || 0} satellites | ${live.sentences || 0} sentences | ${live.bytes || 0} bytes`,
    );
  } else {
    setHeltecText("#heltec-gps-position", state === "exclusive" ? "GPS is used by another feature" : "GPS power is off");
    setHeltecText("#heltec-gps-detail", "Start monitoring to read live NMEA data.");
  }

  const gpsButton = $("#heltec-gps-toggle");
  gpsButton.textContent = heltecGpsActive ? "Stop GPS" : "Start GPS";
  gpsButton.disabled = state === "stopping" || state === "exclusive";

  const ble = status.ble || {};
  setHeltecText(
    "#heltec-ble-api",
    ble.apiEnabled
      ? `${ble.advertising ? "advertising" : "enabled"} | ${ble.connectedClients || 0} connected | ${ble.connectionCount || 0} total`
      : "off",
  );
}

function renderHeltecFieldLog(status) {
  heltecFieldLogActive = Boolean(status.active);
  const gps = status.gps || {};
  const ble = status.ble || {};
  const wifi = status.wifi || {};
  const storage = status.storage || {};
  setHeltecText("#heltec-fieldlog-state", heltecFieldLogActive ? "Recording" : "Stopped");
  setHeltecText(
    "#heltec-fieldlog-value",
    status.sessionId
      ? `Session ${status.sessionId}, segment ${status.segment || 0} | ${formatHeltecBytes(storage.sessionBytes)}`
      : "No field-log session has been started",
  );
  const details = [
    `${gps.fixes || 0} GPS fixes`,
    `${ble.observations || 0} BLE observations`,
    `${ble.uniqueDevices || 0} unique BLE devices`,
    `${wifi.observations || 0} Wi-Fi observations`,
    `${wifi.uniqueNetworks || 0} unique Wi-Fi networks`,
  ];
  if (ble.enabled && heltecFieldLogActive) details.push(ble.scanning ? "BLE scan active" : "BLE scan paused");
  if (wifi.enabled && heltecFieldLogActive) details.push(wifi.scanning ? "Wi-Fi scan active" : "Wi-Fi scan paused");
  if (status.resumeCount) details.push(`${status.resumeCount} reset ${status.resumeCount === 1 ? "resume" : "resumes"}`);
  if (status.recoveredSegments) details.push(`${status.recoveredSegments} recovered tail ${status.recoveredSegments === 1 ? "segment" : "segments"}`);
  if (status.lastError) details.push(`Error: ${status.lastError}`);
  setHeltecText("#heltec-fieldlog-detail", details.join(" | "));

  const gpsOption = $("#heltec-fieldlog-gps");
  const bleOption = $("#heltec-fieldlog-ble");
  const wifiOption = $("#heltec-fieldlog-wifi");
  const resumeOption = $("#heltec-fieldlog-resume");
  if (!heltecFieldOptionsInitialized || heltecFieldLogActive) {
    gpsOption.checked = Boolean(gps.enabled);
    bleOption.checked = Boolean(ble.enabled);
    wifiOption.checked = Boolean(wifi.enabled);
    resumeOption.checked = Boolean(status.autoResume);
    heltecFieldOptionsInitialized = true;
  }
  gpsOption.disabled = heltecFieldLogActive;
  bleOption.disabled = heltecFieldLogActive;
  wifiOption.disabled = heltecFieldLogActive;
  resumeOption.disabled = heltecFieldLogActive;
  setHeltecText(
    "#heltec-fieldlog-toggle",
    heltecFieldLogActive ? "Stop field log" : "Start field log",
  );
}

function downloadHeltecFieldLog(fileName) {
  if (
    !confirm(
      "Field logs can contain precise coordinates, Wi-Fi BSSIDs/SSIDs, BLE addresses, and broadcast device names. Download this file?",
    )
  ) {
    return;
  }
  const link = document.createElement("a");
  link.href = `/api/heltec/fieldlog/download?name=${encodeURIComponent(fileName)}`;
  link.download = fileName;
  link.click();
}

function renderHeltecWifiStatus(status) {
  const busy = Boolean(status.scanning || status.pending);
  setHeltecText("#heltec-wifi-state", status.scanning ? "Scanning" : status.pending ? "Queued" : "Idle");
  setHeltecText(
    "#heltec-wifi-value",
    status.scansCompleted
      ? `${status.networkCount || 0} networks retained from scan ${status.scansCompleted}`
      : "No survey has completed",
  );
  const details = [
    "Passive receive only",
    `${status.lastScanDurationMs || 0} ms last scan`,
    status.fieldLoggerEnabled ? "field logging enabled" : "field logging off",
  ];
  if (status.droppedNetworks) details.push(`${status.droppedNetworks} over capacity`);
  if (status.lastError) details.push(`Error: ${status.lastError}`);
  setHeltecText("#heltec-wifi-detail", details.join(" | "));
  $("#heltec-wifi-scan").disabled = busy;
  $("#heltec-wifi-clear").disabled = busy;
}

function renderHeltecWifiResults(result) {
  const list = $("#heltec-wifi-results");
  const networks = Array.isArray(result.networks) ? result.networks : [];
  list.replaceChildren();
  setHeltecText(
    "#heltec-wifi-summary",
    `${networks.length} of ${result.capacity || networks.length} retained${result.droppedNetworks ? ` | ${result.droppedNetworks} omitted` : ""}`,
  );
  networks.forEach((network) => {
    const row = document.createElement("div");
    row.className = "hardware-history-row";
    const identity = document.createElement("div");
    identity.className = "hardware-history-message";
    identity.textContent = `${network.ssid || "(hidden)"} | ${network.bssid || "--"}`;
    const metrics = document.createElement("div");
    metrics.className = "hardware-history-metrics";
    metrics.textContent = `ch ${network.channel || "?"} | ${network.rssiDbm ?? "?"} dBm | ${network.authentication || "UNKNOWN"}`;
    row.append(identity, metrics);
    list.appendChild(row);
  });
}

async function refreshHeltecWifi() {
  try {
    const status = JSON.parse(await requestGet("/api/heltec/wifi"));
    renderHeltecWifiStatus(status);
    if (status.generation !== heltecWifiGeneration) {
      const results = JSON.parse(await requestGet("/api/heltec/wifi/results"));
      heltecWifiGeneration = results.generation;
      renderHeltecWifiResults(results);
    }
  } catch (error) {
    setHeltecText("#heltec-wifi-state", "Unavailable");
  }
}

function renderHeltecFieldLogFiles(result) {
  const list = $("#heltec-fieldlog-files");
  list.replaceChildren();
  const files = Array.isArray(result.files) ? result.files : [];
  setHeltecText(
    "#heltec-fieldlog-files-summary",
    files.length
      ? `${result.count || files.length} files | ${formatHeltecBytes(result.usedBytes)} of ${formatHeltecBytes(result.totalBytes)} used${result.truncated ? " | list truncated" : ""}`
      : `No saved logs | ${formatHeltecBytes(result.usedBytes)} of ${formatHeltecBytes(result.totalBytes)} used`,
  );
  files
    .slice()
    .reverse()
    .forEach((file) => {
      const row = document.createElement("div");
      row.className = "hardware-history-row";
      const name = document.createElement("div");
      name.className = "hardware-history-message";
      name.textContent = `${file.name}${file.active ? " (active)" : ""}${file.tailComplete === false ? " (interrupted tail)" : ""}`;
      const controls = document.createElement("div");
      controls.className = "hardware-history-metrics";
      const button = document.createElement("button");
      button.className = "btn-action";
      button.textContent = `Download ${formatHeltecBytes(file.sizeBytes)}`;
      button.addEventListener("click", () => downloadHeltecFieldLog(file.name));
      controls.appendChild(button);
      row.append(name, controls);
      list.appendChild(row);
    });
}

async function refreshHeltecFieldLogFiles() {
  try {
    renderHeltecFieldLogFiles(JSON.parse(await requestGet("/api/heltec/fieldlog/files")));
  } catch (error) {
    setHeltecText("#heltec-fieldlog-files-summary", "Saved field logs unavailable");
  }
}

function renderHeltecLoraStatus(status) {
  heltecLoraStatus = status;
  heltecLoraActive = Boolean(status.listening);
  setHeltecText("#heltec-lora-state", heltecLoraActive ? "Listening" : "Stopped");
  const input = $("#heltec-lora-frequency");
  if (status.minimumFrequencyMHz !== undefined) input.min = status.minimumFrequencyMHz;
  if (status.maximumFrequencyMHz !== undefined) input.max = status.maximumFrequencyMHz;
  if (document.activeElement !== input && !heltecLoraActive && status.frequencyMHz) {
    input.value = Number(status.frequencyMHz).toFixed(3);
  }
  input.disabled = heltecLoraActive;

  let detail = `${status.packetsReceived || 0} packets`;
  if (status.rssiDbm !== undefined) detail += ` | RSSI ${formatHeltecNumber(status.rssiDbm)} dBm`;
  if (status.snrDb !== undefined) detail += ` | SNR ${formatHeltecNumber(status.snrDb)} dB`;
  if (status.transmittedPackets) detail += ` | ${status.transmittedPackets} sent`;
  setHeltecText("#heltec-lora-detail", detail);
  setHeltecText("#heltec-lora-message", status.lastMessage || "No packets received");
  setHeltecText("#heltec-lora-toggle", heltecLoraActive ? "Stop receiver" : "Start receiver");

  const transmitPanel = $("#heltec-lora-transmit");
  transmitPanel.classList.toggle("hidden", !status.transmitAvailable);
  if (status.transmitAvailable) {
    const cooldownMs = Number(status.transmitCooldownRemainingMs) || 0;
    const transmitButton = $("#heltec-lora-transmit-button");
    transmitButton.disabled = !heltecLoraActive || cooldownMs > 0;
    const constraints = `Fixed ${formatHeltecNumber(status.transmitPowerDbm, 0)} dBm | ${status.maximumTransmitBytes || 64} ASCII bytes`;
    setHeltecText(
      "#heltec-lora-transmit-detail",
      cooldownMs > 0
        ? `${constraints} | wait ${Math.ceil(cooldownMs / 1000)}s`
        : `${constraints} | explicit confirmation required`,
    );
  }
}

function renderHeltecGpsTrack(track) {
  const list = $("#heltec-gps-track-list");
  const points = Array.isArray(track.points) ? track.points : [];
  list.replaceChildren();
  const state = track.recording ? "recording valid fixes" : "GPS monitor stopped";
  setHeltecText(
    "#heltec-gps-track-summary",
    points.length
      ? `${points.length} of ${track.capacity || points.length} retained; ${state}`
      : track.recording
        ? "Waiting for a valid GPS fix"
        : "Start GPS monitoring to record valid fixes.",
  );
  $("#heltec-gps-track-gpx").disabled = points.length === 0;

  points
    .slice()
    .reverse()
    .forEach((point) => {
      const row = document.createElement("div");
      row.className = "hardware-history-row";

      const position = document.createElement("div");
      position.className = "hardware-history-message";
      position.textContent = `#${point.sequence || "?"} ${Number(point.latitude).toFixed(6)}, ${Number(point.longitude).toFixed(6)}`;

      const metrics = document.createElement("div");
      metrics.className = "hardware-history-metrics";
      const detail = [`${point.satellites || 0} sat`];
      if (point.hdop !== undefined) detail.push(`HDOP ${formatHeltecNumber(point.hdop)}`);
      if (/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{2}Z$/.test(point.utc || "")) {
        detail.push(`${point.utc.slice(11, 19)} UTC`);
      }
      detail.push(formatHeltecAge(point.ageMs));
      metrics.textContent = detail.join(" | ");

      row.append(position, metrics);
      list.appendChild(row);
    });
}

function renderHeltecLoraHistory(history) {
  const list = $("#heltec-lora-history-list");
  const packets = Array.isArray(history.packets) ? history.packets : [];
  list.replaceChildren();
  setHeltecText(
    "#heltec-lora-history-summary",
    packets.length
      ? `${packets.length} of ${history.capacity || packets.length} retained; newest first`
      : "No packets recorded",
  );

  packets
    .slice()
    .reverse()
    .forEach((packet) => {
      const row = document.createElement("div");
      row.className = "hardware-history-row";

      const message = document.createElement("div");
      message.className = "hardware-history-message";
      message.textContent = `#${packet.sequence || "?"} ${packet.message || "(empty)"}`;

      const metrics = document.createElement("div");
      metrics.className = "hardware-history-metrics";
      const signal = [];
      if (packet.rssiDbm !== undefined) signal.push(`${formatHeltecNumber(packet.rssiDbm)} dBm`);
      if (packet.snrDb !== undefined) signal.push(`SNR ${formatHeltecNumber(packet.snrDb)}`);
      signal.push(formatHeltecAge(packet.ageMs));
      metrics.textContent = signal.join(" | ");

      row.append(message, metrics);
      list.appendChild(row);
    });
}

async function refreshHeltecStatus() {
  try {
    const boardStatus = JSON.parse(await requestGet("/api/heltec/status"));
    $("#heltec-hardware").classList.remove("hidden");
    renderHeltecBoardStatus(boardStatus);
  } catch (error) {
    $("#heltec-hardware").classList.add("hidden");
    return false;
  }

  try {
    const gpsTrack = JSON.parse(await requestGet("/api/heltec/gps/history"));
    renderHeltecGpsTrack(gpsTrack);
  } catch (error) {
    setHeltecText("#heltec-gps-track-summary", "GPS track unavailable");
  }
  try {
    const loraStatus = JSON.parse(await requestGet("/api/heltec/lora"));
    renderHeltecLoraStatus(loraStatus);
  } catch (error) {
    setHeltecText("#heltec-lora-state", "Busy");
  }
  try {
    const history = JSON.parse(await requestGet("/api/heltec/lora/history"));
    renderHeltecLoraHistory(history);
  } catch (error) {
    setHeltecText("#heltec-lora-history-summary", "History unavailable");
  }
  try {
    renderHeltecFieldLog(JSON.parse(await requestGet("/api/heltec/fieldlog")));
  } catch (error) {
    setHeltecText("#heltec-fieldlog-state", "Unavailable");
  }
  await refreshHeltecWifi();
  return true;
}

async function initHeltecPanel() {
  if (!(await refreshHeltecStatus())) return;
  await refreshHeltecFieldLogFiles();
  if (!heltecStatusTimer) heltecStatusTimer = setInterval(refreshHeltecStatus, 2000);
}

$("#heltec-refresh").addEventListener("click", refreshHeltecStatus);

$("#heltec-diagnostics-download").addEventListener("click", async (event) => {
  if (
    !confirm(
      "The diagnostics file can include the AP MAC, retained GPS coordinates, and received LoRa payloads. Download it?",
    )
  ) {
    return;
  }
  event.currentTarget.disabled = true;
  try {
    const [hardware, capabilities, gpsTrack, loraStatus, loraHistory, wifiStatus, wifiResults, fieldLog, fieldLogFiles] = await Promise.all([
      requestGet("/api/heltec/status"),
      requestGet("/api/heltec/capabilities"),
      requestGet("/api/heltec/gps/history"),
      requestGet("/api/heltec/lora"),
      requestGet("/api/heltec/lora/history"),
      requestGet("/api/heltec/wifi"),
      requestGet("/api/heltec/wifi/results"),
      requestGet("/api/heltec/fieldlog"),
      requestGet("/api/heltec/fieldlog/files"),
    ]);
    const snapshot = {
      formatVersion: 1,
      exportedAt: new Date().toISOString(),
      hardware: JSON.parse(hardware),
      capabilities: JSON.parse(capabilities),
      gpsTrack: JSON.parse(gpsTrack),
      loraStatus: JSON.parse(loraStatus),
      loraHistory: JSON.parse(loraHistory),
      wifiStatus: JSON.parse(wifiStatus),
      wifiResults: JSON.parse(wifiResults),
      fieldLog: JSON.parse(fieldLog),
      fieldLogFiles: JSON.parse(fieldLogFiles),
    };
    downloadHeltecFile(
      JSON.stringify(snapshot, null, 2),
      "application/json",
      "heltec-diagnostics",
      "json",
    );
  } catch (error) {
    alert("Diagnostics download failed: " + error.message);
  } finally {
    event.currentTarget.disabled = false;
  }
});

$("#heltec-fieldlog-toggle").addEventListener("click", async (event) => {
  const gps = $("#heltec-fieldlog-gps").checked;
  const ble = $("#heltec-fieldlog-ble").checked;
  const wifi = $("#heltec-fieldlog-wifi").checked;
  if (!heltecFieldLogActive && !gps && !ble && !wifi) {
    alert("Select GPS fixes, passive BLE observations, passive Wi-Fi, or a combination.");
    return;
  }
  event.currentTarget.disabled = true;
  try {
    const response = await requestPost("/api/heltec/fieldlog", {
      action: heltecFieldLogActive ? "stop" : "start",
      gps: gps ? "true" : "false",
      ble: ble ? "true" : "false",
      wifi: wifi ? "true" : "false",
      autoResume: $("#heltec-fieldlog-resume").checked ? "true" : "false",
    });
    renderHeltecFieldLog(JSON.parse(response));
    await refreshHeltecFieldLogFiles();
  } catch (error) {
    alert("Field-log control failed: " + error.message);
  } finally {
    event.currentTarget.disabled = false;
  }
});

$("#heltec-fieldlog-files-refresh").addEventListener("click", refreshHeltecFieldLogFiles);

$("#heltec-wifi-scan").addEventListener("click", async (event) => {
  event.currentTarget.disabled = true;
  try {
    renderHeltecWifiStatus(JSON.parse(await requestPost("/api/heltec/wifi", { action: "scan" })));
  } catch (error) {
    alert("Wi-Fi survey failed: " + error.message);
  }
  await refreshHeltecWifi();
});

$("#heltec-wifi-clear").addEventListener("click", async (event) => {
  event.currentTarget.disabled = true;
  try {
    renderHeltecWifiStatus(JSON.parse(await requestPost("/api/heltec/wifi", { action: "clear" })));
    heltecWifiGeneration = -1;
  } catch (error) {
    alert("Wi-Fi results could not be cleared: " + error.message);
  }
  await refreshHeltecWifi();
});

$("#heltec-gps-toggle").addEventListener("click", async (event) => {
  event.currentTarget.disabled = true;
  try {
    await requestPost("/api/heltec/gps", { action: heltecGpsActive ? "stop" : "start" });
  } catch (error) {
    alert("GPS control failed: " + error.message);
  }
  await refreshHeltecStatus();
});

$("#heltec-gps-track-json").addEventListener("click", async (event) => {
  event.currentTarget.disabled = true;
  try {
    const track = JSON.parse(await requestGet("/api/heltec/gps/history"));
    downloadHeltecFile(JSON.stringify(track, null, 2), "application/json", "heltec-gps-track", "json");
  } catch (error) {
    alert("GPS track download failed: " + error.message);
  } finally {
    event.currentTarget.disabled = false;
  }
});

$("#heltec-gps-track-gpx").addEventListener("click", async (event) => {
  event.currentTarget.disabled = true;
  try {
    const track = JSON.parse(await requestGet("/api/heltec/gps/history"));
    if (!Array.isArray(track.points) || track.points.length === 0) {
      alert("No valid GPS fixes are available for GPX export.");
      return;
    }
    downloadHeltecFile(heltecGpsTrackToGpx(track), "application/gpx+xml", "heltec-gps-track", "gpx");
  } catch (error) {
    alert("GPS track download failed: " + error.message);
  } finally {
    event.currentTarget.disabled = false;
  }
});

$("#heltec-gps-track-clear").addEventListener("click", async (event) => {
  if (!confirm("Clear the in-memory GPS fix track?")) return;
  event.currentTarget.disabled = true;
  try {
    const track = JSON.parse(await requestPost("/api/heltec/gps/history", { action: "clear" }));
    renderHeltecGpsTrack(track);
  } catch (error) {
    alert("GPS track clear failed: " + error.message);
  } finally {
    event.currentTarget.disabled = false;
  }
});

$("#heltec-lora-toggle").addEventListener("click", async (event) => {
  event.currentTarget.disabled = true;
  const frequencyInput = $("#heltec-lora-frequency");
  const frequencyMHz = Number(frequencyInput.value);
  if (!frequencyInput.checkValidity() || !Number.isFinite(frequencyMHz)) {
    alert(`Choose a frequency from ${frequencyInput.min} to ${frequencyInput.max} MHz.`);
    event.currentTarget.disabled = false;
    return;
  }
  try {
    await requestPost("/api/heltec/lora", {
      action: heltecLoraActive ? "stop" : "start",
      frequencyMHz: frequencyMHz.toString(),
    });
  } catch (error) {
    alert("LoRa control failed: " + error.message);
  }
  await refreshHeltecStatus();
});

$("#heltec-lora-transmit-button").addEventListener("click", async (event) => {
  const payloadInput = $("#heltec-lora-payload");
  const payload = payloadInput.value;
  const maximumBytes = Number(heltecLoraStatus.maximumTransmitBytes) || 64;
  if (!heltecLoraActive) {
    alert("Start the LoRa receiver first to establish the transmit frequency.");
    return;
  }
  if (!payload || payload.length > maximumBytes || !/^[\x20-\x7e]+$/.test(payload)) {
    alert(`Enter 1-${maximumBytes} printable ASCII characters.`);
    return;
  }
  const frequencyMHz = Number(heltecLoraStatus.frequencyMHz).toFixed(3);
  const powerDbm = formatHeltecNumber(heltecLoraStatus.transmitPowerDbm, 0);
  if (!confirm(`Transmit ${payload.length} bytes at ${frequencyMHz} MHz and ${powerDbm} dBm?`)) return;

  event.currentTarget.disabled = true;
  try {
    await requestPost("/api/heltec/lora/transmit", {
      payload,
      confirm: "TRANSMIT",
    });
    payloadInput.value = "";
    alert("LoRa packet transmitted and receiver restored.");
  } catch (error) {
    alert("LoRa transmission failed: " + error.message);
  }
  await refreshHeltecStatus();
});

$("#heltec-lora-history-download").addEventListener("click", async (event) => {
  event.currentTarget.disabled = true;
  try {
    const history = JSON.parse(await requestGet("/api/heltec/lora/history"));
    downloadHeltecFile(
      JSON.stringify(history, null, 2),
      "application/json",
      "heltec-lora-history",
      "json",
    );
  } catch (error) {
    alert("LoRa history download failed: " + error.message);
  } finally {
    event.currentTarget.disabled = false;
  }
});

$("#heltec-lora-history-clear").addEventListener("click", async (event) => {
  if (!confirm("Clear the in-memory LoRa receive history? The lifetime packet counter is preserved.")) return;
  event.currentTarget.disabled = true;
  try {
    const history = JSON.parse(
      await requestPost("/api/heltec/lora/history", { action: "clear" }),
    );
    renderHeltecLoraHistory(history);
  } catch (error) {
    alert("LoRa history clear failed: " + error.message);
  } finally {
    event.currentTarget.disabled = false;
  }
});

function getSerialCommand(fileName) {
  let extension = fileName.split(".");
  if (extension.length > 1) {
    extension = extension[extension.length - 1].toLowerCase();
    return EXECUTABLE[extension];
  }

  return undefined;
}

function calcHash(str) {
  let hash = 5381;
  str = str.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
  for (let i = 0; i < str.length; i++) {
    hash = ((hash << 5) + hash) ^ str.charCodeAt(i); // djb2 xor variant
    hash = hash >>> 0; // force unsigned 32-bit
  }

  return hash.toString(16).padStart(8, "0");
}

// Line numbers functionality
function updateLineNumbers() {
  const textarea = $(".dialog.editor .file-content");
  const lineNumbers = $(".dialog.editor .line-numbers");

  if (!textarea || !lineNumbers) return;

  const lines = textarea.value.split("\n");
  const lineCount = lines.length;

  // Generate line numbers
  let lineNumbersHTML = "";
  for (let i = 1; i <= lineCount; i++) {
    lineNumbersHTML += i + "\n";
  }

  lineNumbers.textContent = lineNumbersHTML;
}

function syncScrolling() {
  const textarea = $(".dialog.editor .file-content");
  const lineNumbers = $(".dialog.editor .line-numbers");

  if (!textarea || !lineNumbers) return;

  lineNumbers.scrollTop = textarea.scrollTop;
}

function renderFileRow(fileList) {
  $("table.explorer tbody").innerHTML = "";
  fileList
    .split("\n")
    .sort((a, b) => {
      let [aFirst, ...aRest] = a.split(":");
      let [bFirst, ...bRest] = b.split(":");

      if (aFirst !== bFirst) {
        return bFirst.localeCompare(aFirst);
      }

      let aRestStr = aRest.join(":").toLowerCase();
      let bRestStr = bRest.join(":").toLowerCase();
      return aRestStr.localeCompare(bRestStr);
    })
    .forEach((line) => {
      let e;
      let [type, name, size] = line.split(":");
      if (size === undefined) return;
      let dPath = (
        (currentPath.endsWith("/") ? currentPath : currentPath + "/") + name
      ).replace(/\/\//g, "/");
      if (type === "pa") {
        if (dPath === "/") return;
        e = T.pathRow();
        let preFolder = currentPath.substring(0, currentPath.lastIndexOf("/"));
        if (preFolder === "") preFolder = "/";
        e.querySelector(".path-row").setAttribute("data-path", preFolder);
        e.querySelector(".path-row td").classList.add("act-browse");
      } else if (type === "Fi") {
        e = T.fileRow();
        e.querySelector(".file-row").setAttribute("data-file", dPath);
        e.querySelector(".act-rename").setAttribute(
          "data-action",
          "renameFile",
        );
        e.querySelector(".col-name").classList.add("act-edit-file");
        e.querySelector(".col-name").textContent = name;
        e.querySelector(".col-name").setAttribute("title", name);
        e.querySelector(".col-size").textContent = size;
        e.querySelector(".col-action").classList.add("type-file");

        let downloadUrl = `/file?fs=${currentDrive}&name=${encodeURIComponent(dPath)}&action=download`;
        if (IS_DEV) downloadUrl = "/bruce" + downloadUrl;
        e.querySelector(".act-download").setAttribute("download", name);
        e.querySelector(".act-download").setAttribute("href", downloadUrl);

        let serialCmd = getSerialCommand(name);
        if (serialCmd) {
          e.querySelector(".act-play").setAttribute(
            "data-cmd",
            serialCmd + ' "' + dPath + '"',
          );
          e.querySelector(".col-action").classList.add("executable");
        }
      } else if (type === "Fo") {
        e = T.fileRow();
        e.querySelector(".col-name").classList.add("act-browse");
        e.querySelector(".file-row").setAttribute("data-path", dPath);
        e.querySelector(".act-rename").setAttribute(
          "data-action",
          "renameFolder",
        );
        e.querySelector(".col-name").textContent = name;
        e.querySelector(".col-name").setAttribute("title", name);
        e.querySelector(".col-action").classList.add("type-folder");
      }
      $("table.explorer tbody").appendChild(e);
    });
}

let sdCardAvailable = false;
let currentDrive;
let currentPath;
const btnRefreshFolder = $("#refresh-folder");

// URL state management
function updateURL(drive, path, editFile = null) {
  const params = new URLSearchParams();
  if (drive) params.set("drive", drive);
  if (path && path !== "/") params.set("path", path);
  if (editFile) params.set("edit", editFile);

  const newURL =
    window.location.pathname +
    (params.toString() ? "?" + params.toString() : "");
  window.history.replaceState({ drive, path, editFile }, "", newURL);
}

function getURLParams() {
  const params = new URLSearchParams(window.location.search);
  return {
    drive: params.get("drive"),
    path: params.get("path") || "/",
    editFile: params.get("edit"),
  };
}

async function fetchFiles(drive, path) {
  btnRefreshFolder.classList.add("reloading");
  $("table.explorer tbody").innerHTML =
    '<tr><td colspan="3" style="text-align:center">Loading...</td></tr>';
  currentDrive = drive;
  currentPath = path;

  // Update URL state (preserving edit file if still valid)
  const urlParams = getURLParams();
  updateURL(drive, path, urlParams.editFile);

  $(`.act-browse.active`)?.classList.remove("active");
  $(`.act-browse[data-drive='${drive}']`).classList.add("active");
  $(".current-path").textContent = drive + ":/" + path;
  let req = await requestGet("/listfiles", {
    fs: drive,
    folder: path,
  });
  renderFileRow(req);
  btnRefreshFolder.classList.remove("reloading");
}

async function fetchSystemInfo() {
  Dialog.loading.show("Fetching system info...");
  let req = await requestGet("/systeminfo");
  let info = JSON.parse(req);
  $(".bruce-version").textContent = info.BRUCE_VERSION;
  $(".free-space .free-sd span").innerHTML =
    `${info.SD.used} / ${info.SD.total}`;
  $(".free-space .free-fs span").innerHTML =
    `${info.LittleFS.used} / ${info.LittleFS.total}`;
  sdCardAvailable = info.SD.total != "0 B";
  Dialog.loading.hide();
}

async function saveEditorFile(runFile = false) {
  Dialog.loading.show("Saving...");
  let editor = $(".dialog.editor .file-content");
  let filename = $(".dialog.editor .editor-file-name").textContent.trim();
  if (isModified(editor)) {
    $(".act-save-edit-file").disabled = true;
    editor.setAttribute("data-hash", calcHash(editor.value));
    await requestPost("/edit", {
      fs: currentDrive,
      name: filename,
      content: editor.value,
    });
  }

  if (runFile) {
    let serial = getSerialCommand(filename);
    if (serial !== undefined) {
      await runCommand(serial + ' "' + filename + '"');
    }
  }
  Dialog.loading.hide();
}

function isModified(target) {
  let oldHash = target.getAttribute("data-hash");
  let newHash = calcHash(target.value);
  return oldHash !== newHash;
}

async function openNavigator() {
  Dialog.show("navigator");
  await reloadScreen();
  autoReloadScreen();
}

let SCREEN_NAVIGATING = false;
async function runNavigation(direction) {
  if (SCREEN_NAVIGATING) return;
  SCREEN_NAVIGATING = true;
  try {
    drawCanvasLoading();
    await requestPost("/cm", { cmnd: `nav ${direction.toLowerCase()}` });
    await reloadScreen();
  } catch (error) {
    alert("Failed to run command: " + error.message);
    console.error(error);
  } finally {
    SCREEN_NAVIGATING = false;
  }
}

const btnForceReload = $("#force-reload");
let SCREEN_RELOAD = false;
async function reloadScreen() {
  if (SCREEN_RELOAD) return;
  SCREEN_RELOAD = true;
  btnForceReload.classList.add("reloading");
  try {
    let binResponse = await fetch((IS_DEV ? "/bruce" : "") + "/getscreen");
    let arrayBuffer = await binResponse.arrayBuffer();
    let screenData = new Uint8Array(arrayBuffer);
    await renderTFT(screenData);
  } catch (error) {
    console.error("Failed to reload screen:", error);
    alert("Failed to reload screen: " + error.message);
  } finally {
    btnForceReload.classList.remove("reloading");
    SCREEN_RELOAD = false;
  }
}

const eConfigAutoReload = $("#navigator-auto-reload");
let AUTO_RELOAD_SCREEN = null;
async function taskReloader() {
  let timer = parseInt(eConfigAutoReload.value);
  let navigatorOpen = $(".dialog.navigator:not(.hidden)");
  if (timer <= 0 || !navigatorOpen) {
    if (AUTO_RELOAD_SCREEN) {
      clearTimeout(AUTO_RELOAD_SCREEN);
      AUTO_RELOAD_SCREEN = null;
    }

    return;
  }

  await reloadScreen();
  setTimeout(taskReloader, timer);
  // better use setTimeout instead of setInterval to avoid overlapping calls
}
async function autoReloadScreen() {
  let timer = parseInt(eConfigAutoReload.value);

  if (AUTO_RELOAD_SCREEN) {
    clearTimeout(AUTO_RELOAD_SCREEN);
    AUTO_RELOAD_SCREEN = null;
  }

  if (timer > 0) taskReloader();
}

/// TFT RENDER
let loadingDrawn = false;
const imageCache = {}; // global
async function renderTFT(data) {
  loadingDrawn = false;
  const canvas = $("#navigator-screen");
  const ctx = canvas.getContext("2d");

  const loadImage = async (url) => {
    if (imageCache[url]) return imageCache[url];
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => {
        imageCache[url] = img;
        resolve(img);
      };
      img.onerror = (err) => reject(err);
      img.src = url;
    });
  };

  const drawImageCached = async (img_url, input) => {
    if (IS_DEV) img_url = "/bruce" + img_url;
    let img = await loadImage(img_url);
    let drawX = input.x;
    let drawY = input.y;

    if (input.center === 1) {
      drawX += (canvas.width - img.width) / 2;
      drawY += (canvas.height - img.height) / 2;
    }
    ctx.drawImage(img, drawX, drawY);
  };

  const color565toCSS = (color565) => {
    const r = (((color565 >> 11) & 0x1f) * 255) / 31;
    const g = (((color565 >> 5) & 0x3f) * 255) / 63;
    const b = ((color565 & 0x1f) * 255) / 31;
    return `rgb(${r},${g},${b})`;
  };

  const drawRoundRect = (ctx, input, fill) => {
    const { x, y, w, h, r } = input;
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
    if (fill) ctx.fill();
    else ctx.stroke();
  };

  let startData = 0;
  const getByteValue = (dataType) => {
    if (dataType === "int8") {
      return data[startData++];
    } else if (dataType === "int16") {
      let value = (data[startData] << 8) | data[startData + 1];
      startData += 2;
      return value;
    } else if (dataType.startsWith("s")) {
      let strLength = parseInt(dataType.substring(1));
      let strBytes = data.slice(startData, startData + strLength);
      startData += strLength;
      return new TextDecoder().decode(strBytes);
    }
  };

  const byteToObject = (fn, size) => {
    let keysMap = {
      0: ["fg"], // FILLSCREEN
      1: ["x", "y", "w", "h", "fg"], // DRAWRECT
      2: ["x", "y", "w", "h", "fg"], // FILLRECT
      3: ["x", "y", "w", "h", "r", "fg"], // DRAWROUNDRECT
      4: ["x", "y", "w", "h", "r", "fg"], // FILLROUNDRECT
      5: ["x", "y", "r", "fg"], // DRAWCIRCLE
      6: ["x", "y", "r", "fg"], // FILLCIRCLE
      7: ["x", "y", "x2", "y2", "x3", "y3", "fg"], // DRAWTRIANGLE
      8: ["x", "y", "x2", "y2", "x3", "y3", "fg"], // FILLTRIANGLE
      9: ["x", "y", "rx", "ry", "fg"], // DRAWELLIPSE
      10: ["x", "y", "rx", "ry", "fg"], // FILLELLIPSE
      11: ["x", "y", "x1", "y1", "fg"], // DRAWLINE
      12: ["x", "y", "r", "ir", "startAngle", "endAngle", "fg", "bg"], // DRAWARC
      13: ["x", "y", "bx", "by", "wd", "fg", "bg"], // DRAWWIDELINE
      14: ["x", "y", "size", "fg", "bg", "txt"], // DRAWCENTRESTRING
      15: ["x", "y", "size", "fg", "bg", "txt"], // DRAWRIGHTSTRING
      16: ["x", "y", "size", "fg", "bg", "txt"], // DRAWSTRING
      17: ["x", "y", "size", "fg", "bg", "txt"], // PRINT
      18: ["x", "y", "center", "ms", "fs", "file"], // DRAWIMAGE
      20: ["x", "y", "h", "fg"], // DRAWFASTVLINE
      21: ["x", "y", "w", "fg"], // DRAWFASTHLINE
      99: ["w", "h", "rotation"], // SCREEN_INFO
    };

    let r = {};
    let lengthLeft = size - 3;
    for (let key of keysMap[fn]) {
      if (["txt", "file"].includes(key)) {
        r[key] = getByteValue(`s${lengthLeft}`);
      } else if (["rotation", "fs"].includes(key)) {
        lengthLeft -= 1;
        r[key] = getByteValue("int8");
        if (key === "fs") {
          r[key] = r[key] === 0 ? "SD" : "FS"; // 0 for SD, 1 for FS
        }
      } else {
        lengthLeft -= 2;
        r[key] = getByteValue("int16");
      }
    }
    return r;
  };

  let offset = 0;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  let screenText = []; // Collect all text rendered on screen

  while (offset < data.length) {
    ctx.beginPath();
    if (data[offset] !== 0xaa) {
      console.warn("Invalid header at offset", offset);
      break;
    }

    startData = offset + 1;
    let size = getByteValue("int8");
    let fn = getByteValue("int8");
    offset += size;

    let input = byteToObject(fn, size);
    // reset to default before drawing again
    ctx.lineWidth = 1;
    ctx.fillStyle = "black";
    ctx.strokeStyle = "black";
    switch (fn) {
      case 99: // SCREEN_INFO
        canvas.width = input.w;
        canvas.height = input.h;
      case 0: // FILLSCREEN
        ctx.fillStyle = color565toCSS(input.fg);
        ctx.fillRect(0, 0, canvas.width, canvas.height);
        break;

      case 1: // DRAWRECT
        ctx.strokeStyle = color565toCSS(input.fg);
        ctx.strokeRect(input.x, input.y, input.w, input.h);
        break;

      case 2: // FILLRECT
        ctx.fillStyle = color565toCSS(input.fg);
        ctx.fillRect(input.x, input.y, input.w, input.h);
        break;

      case 3: // DRAWROUNDRECT
        ctx.strokeStyle = color565toCSS(input.fg);
        drawRoundRect(ctx, input, false);
        break;

      case 4: // FILLROUNDRECT
        ctx.fillStyle = color565toCSS(input.fg);
        drawRoundRect(ctx, input, true);
        break;

      case 5: // DRAWCIRCLE
        ctx.strokeStyle = color565toCSS(input.fg);
        ctx.arc(input.x, input.y, input.r, 0, Math.PI * 2);
        ctx.stroke();
        break;

      case 6: // FILLCIRCLE
        ctx.fillStyle = color565toCSS(input.fg);
        ctx.arc(input.x, input.y, input.r, 0, Math.PI * 2);
        ctx.fill();
        break;
      case 7: // DRAWTRIANGLE
        ctx.strokeStyle = color565toCSS(input.fg);
        ctx.beginPath();
        ctx.moveTo(input.x, input.y);
        ctx.lineTo(input.x2, input.y2);
        ctx.lineTo(input.x3, input.y3);
        ctx.closePath();
        ctx.stroke();
        break;

      case 8: // FILLTRIANGLE
        ctx.fillStyle = color565toCSS(input.fg);
        ctx.beginPath();
        ctx.moveTo(input.x, input.y);
        ctx.lineTo(input.x2, input.y2);
        ctx.lineTo(input.x3, input.y3);
        ctx.closePath();
        ctx.fill();
        break;
      case 9: // DRAWELLIPSE
        ctx.strokeStyle = color565toCSS(input.fg);
        ctx.beginPath();
        ctx.ellipse(input.x, input.y, input.rx, input.ry, 0, 0, Math.PI * 2);
        ctx.stroke();
        break;

      case 10: // FILLELLIPSE
        ctx.fillStyle = color565toCSS(input.fg);
        ctx.beginPath();
        ctx.ellipse(input.x, input.y, input.rx, input.ry, 0, 0, Math.PI * 2);
        ctx.fill();
        break;

      case 11: // DRAWLINE
        ctx.strokeStyle = color565toCSS(input.fg);
        ctx.moveTo(input.x, input.y);
        ctx.lineTo(input.x1, input.y1);
        ctx.stroke();
        break;

      case 12: // DRAWARC
        ctx.strokeStyle = color565toCSS(input.fg);
        ctx.lineWidth = input.r - input.ir || 1;
        const sa = ((input.startAngle + 90 || 0) * Math.PI) / 180;
        const ea = ((input.endAngle + 90 || 0) * Math.PI) / 180;
        const radius = (input.r + input.ir) / 2;
        ctx.beginPath();
        ctx.arc(input.x, input.y, radius, sa, ea);
        ctx.stroke();
        break;

      case 13: // DRAWWIDELINE
        ctx.strokeStyle = color565toCSS(input.fg);
        ctx.lineWidth = input.wd || 1;
        ctx.moveTo(input.x, input.y);
        ctx.lineTo(input.bx, input.by);
        ctx.stroke();
        break;

      case 14: // DRAWCENTRESTRING
      case 15: // DRAWRIGHTSTRING
      case 16: // DRAWSTRING
      case 17: // PRINT
        // This must be enhanced to make font width be multiple of 6px, the font used here is multiple of 4.5px,
        // "\n" are not treated, and long lines do not split into multi lines..
        if (input.bg == input.fg) {
          input.bg = 0;
        }
        ctx.fillStyle = color565toCSS(input.bg);

        input.txt = input.txt.replaceAll("\\n", ""); // remove new lines
        screenText.push(input.txt); // Collect text for WiFi detection

        var fw = input.size === 3 ? 13.5 : input.size === 2 ? 9 : 4.5;
        var o = 0;
        if (fn === 15) o = input.txt.length * fw;
        if (fn === 14) o = (input.txt.length * fw) / 2;
        // draw a rectangle at the text area, to avoid overlapping texts
        ctx.fillRect(
          input.x - o,
          input.y,
          input.txt.length * fw,
          input.size * 8,
        );

        ctx.fillStyle = color565toCSS(input.fg);
        ctx.font = `${input.size * 8}px monospace`;
        ctx.textBaseline = "top";
        ctx.textAlign = fn === 14 ? "center" : fn === 15 ? "right" : "left";
        ctx.fillText(input.txt, input.x, input.y);
        break;

      case 18: // DRAWIMAGE
        let url = `/file?fs=${input.fs}&name=${encodeURIComponent(input.file)}&action=image`;
        await drawImageCached(url, input);
        break;

      case 19: // DRAWPIXEL
        ctx.fillStyle = color565toCSS(input.fg);
        ctx.fillRect(input.x, input.y, 1, 1);
        break;
      case 20: // DRAWFASTVLINE
        ctx.fillStyle = color565toCSS(input.fg);
        ctx.fillRect(input.x, input.y, 1, input.h);
        break;

      case 21: // DRAWFASTHLINE
        ctx.fillStyle = color565toCSS(input.fg);
        ctx.fillRect(input.x, input.y, input.w, 1);
        break;
    }
  }

  // Check if WiFi menu is present on screen and show/hide warning
  const wifiWarning = $("#wifi-warning");
  const allText = screenText.join(" ").toLowerCase();
  const isWiFiMenu =
    allText.includes("wifi") ||
    allText.includes("evil portal") ||
    allText.includes("deauth") ||
    allText.includes("handshake");

  if (isWiFiMenu) {
    wifiWarning.classList.remove("hidden");
  } else {
    wifiWarning.classList.add("hidden");
  }
}
function drawCanvasLoading() {
  if (loadingDrawn || !showNavigating) return;
  loadingDrawn = true;
  const canvas = $("#navigator-screen");
  const ctx = canvas.getContext("2d");
  const width = canvas.width;
  const height = canvas.height;

  // Draw semi-transparent black background
  ctx.save();
  ctx.globalAlpha = 0.8;
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, width, height);
  ctx.globalAlpha = 1.0;

  // Draw "Loading" text in the center
  ctx.fillStyle = "#fff";
  ctx.font = "bold 14px 'DejaVu Sans Mono', Consolas, Menlo";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText("Navigating...", width / 2, height / 2);
  ctx.restore();
}

let oldTimerSession = sessionStorage.getItem("autoReload") || "0";
eConfigAutoReload.querySelector(`option[value="${oldTimerSession}"]`).selected =
  true;
eConfigAutoReload.addEventListener("change", async (e) => {
  e.preventDefault();
  autoReloadScreen();
  sessionStorage.setItem("autoReload", eConfigAutoReload.value);
});

btnForceReload.addEventListener("click", async (e) => {
  e.preventDefault();
  drawCanvasLoading();
  await reloadScreen();
});

window.ondragenter = () => $(".upload-area").classList.remove("hidden");
$(".upload-area").ondragleave = () => $(".upload-area").classList.add("hidden");
$(".upload-area").ondragover = (e) => e.preventDefault();
$(".upload-area").ondrop = async (e) => {
  e.preventDefault();
  $(".upload-area").classList.add("hidden");
  const items = e.dataTransfer.items;
  if (!items || items.length === 0) return;

  for (let i of items) {
    let entry = i.webkitGetAsEntry();
    if (!entry) continue;
    await appendDroppedFiles(entry);
  }

  if (!_runningUpload)
    setTimeout(() => {
      if (_queueUpload.length === 0) return;
      uploadFile();
    }, 100);
};

document.querySelectorAll(".inp-uploader").forEach((el) => {
  el.addEventListener("change", async (e) => {
    let files = e.target.files;
    if (!files || files.length === 0) return;

    appendFileToQueue(files);
    _queueUpload.push(...files);
    if (!_runningUpload) uploadFile();

    this.value = "";
  });
});

$(".container").addEventListener("click", async (e) => {
  let browseAction = e.target.closest(".act-browse");
  if (browseAction) {
    e.preventDefault();
    let drive =
      browseAction.getAttribute("data-drive") || currentDrive || "LittleFS";
    let path =
      browseAction.getAttribute("data-path") ||
      browseAction.closest("tr").getAttribute("data-path") ||
      "/";
    if (drive === currentDrive && path === currentPath) return;

    fetchFiles(drive, path);
    return;
  }

  let editFileAction = e.target.closest(".act-edit-file");
  if (editFileAction) {
    e.preventDefault();
    let editor = $(".dialog.editor .file-content");
    let file = editFileAction.closest("tr").getAttribute("data-file");
    if (!file) return;
    $(".dialog.editor .editor-file-name").textContent = file;
    editor.value = "";

    // Load file content
    Dialog.loading.show("Fetching content...");
    let r = await requestGet(
      `/file?fs=${currentDrive}&name=${encodeURIComponent(file)}&action=edit`,
    );
    editor.value = r;
    editor.setAttribute("data-hash", calcHash(r));

    // Update line numbers
    updateLineNumbers();

    $(".act-save-edit-file").disabled = true;

    let serial = getSerialCommand(file);
    if (serial === undefined) {
      $(".act-run-edit-file").classList.add("hidden");
    } else {
      $(".act-run-edit-file").classList.remove("hidden");
    }

    Dialog.loading.hide();
    Dialog.show("editor");

    // Update URL to include edit state
    updateURL(currentDrive, currentPath, file);
    return;
  }

  let oActionOInput = e.target.closest(".act-oinput");
  if (oActionOInput) {
    e.preventDefault();
    let action = oActionOInput.getAttribute("data-action");
    if (!action) return;

    let value = "",
      data = "";
    if (action.startsWith("rename")) {
      let row = oActionOInput.closest("tr");
      let filePath =
        row.getAttribute("data-file") || row.getAttribute("data-path");

      if (filePath != "") {
        value = filePath.substring(filePath.lastIndexOf("/") + 1);
        data = `${action}|${filePath}`;
      }
    } else if (action.startsWith("create")) {
      filePath = currentPath;
      data = `${action}|${filePath}`;
    } else {
      data = `${action}`;
    }
    Dialog.showOneInput(action, value, data);
    return;
  }

  let actDeleteFile = e.target.closest(".act-delete");
  if (actDeleteFile) {
    e.preventDefault();
    let file =
      actDeleteFile.closest(".file-row").getAttribute("data-file") ||
      actDeleteFile.closest(".file-row").getAttribute("data-path");
    if (!file) return;

    if (
      !confirm(
        `Are you sure you want to DELETE ${file}?\n\nTHIS ACTION CANNOT BE UNDONE!`,
      )
    )
      return;

    Dialog.loading.show("Deleting...");
    await requestGet("/file", {
      fs: currentDrive,
      action: "delete",
      name: file,
    });
    Dialog.loading.hide();
    fetchSystemInfo();
    fetchFiles(currentDrive, currentPath);
    return;
  }

  let actPlay = e.target.closest(".act-play");
  if (actPlay) {
    e.preventDefault();
    let cmd = actPlay.getAttribute("data-cmd");
    if (!cmd) return;

    actPlay.blur();
    await runCommand(cmd);
    return;
  }
});

$(".dialog-background").addEventListener("click", async (e) => {
  if (e.target.matches(".act-dialog-close")) {
    e.preventDefault();
    Dialog.hide();
    return;
  }
});

$(".act-save-oinput-file").addEventListener("click", async (e) => {
  let dialog = $(".dialog.oinput");
  let fileInput = $("#oinput-input");
  let fileName = fileInput.value.trim();
  if (!fileName) {
    alert("Filename cannot be empty.");
    return;
  }
  let action = dialog.getAttribute("data-cache");
  if (!action) {
    alert("No action specified.");
    return;
  }

  let refreshList = true;
  let [actionType, path] = action.split("|");
  if (actionType.startsWith("rename")) {
    Dialog.loading.show("Renaming...");
    await requestPost("/rename", {
      fs: currentDrive,
      filePath: path,
      fileName: fileName,
    });
  } else if (actionType === "createFolder") {
    Dialog.loading.show("Creating Folder...");
    let urlQuery = new URLSearchParams({
      fs: currentDrive,
      action: "create",
      name: path.replace(/\/+$/, "") + "/" + fileName,
    });
    await requestGet("/file?" + urlQuery.toString());
  } else if (actionType === "createFile") {
    Dialog.loading.show("Creating File...");
    let urlQuery = new URLSearchParams({
      fs: currentDrive,
      action: "createfile",
      name: path.replace(/\/+$/, "") + "/" + fileName,
    });
    await requestGet("/file?" + urlQuery.toString());
  } else if (actionType === "serial") {
    Dialog.loading.show("Running Serial Command...");
    await runCommand(fileName);
    refreshList = false; // No need to refresh file list for serial commands
  }

  if (refreshList) fetchFiles(currentDrive, currentPath);
  Dialog.hide();
});

$(".act-save-credential").addEventListener("click", async (e) => {
  let username = $("#cred-username").value.trim();
  let password = $("#cred-password").value.trim();
  if (!username || !password) {
    alert("Username and password cannot be empty.");
    return;
  }

  Dialog.loading.show("Saving WiFi Credentials...");
  await requestGet("/wifi", {
    usr: username,
    pwd: password,
  });
  Dialog.loading.hide();
  alert("Credentials saved successfully!");
});

$(".act-save-edit-file").addEventListener("click", async (e) => {
  await saveEditorFile();
});

const runEditorBtn = $(".act-run-edit-file");
runEditorBtn.addEventListener("click", async (e) => {
  await saveEditorFile(true);
  runEditorBtn.blur(); // remove focus
});

let showNavigating = localStorage.getItem("showNavigating") || false;
updateShowHideNavigatingButton();
$(".act-hide-show-navigating").addEventListener("click", async (e) => {
  e.preventDefault();
  showNavigating = !showNavigating;
  localStorage.setItem("showNavigating", showNavigating);
  updateShowHideNavigatingButton();
});

function updateShowHideNavigatingButton() {
  document.querySelector(".act-hide-show-navigating").innerHTML =
    "'Navigating...' Overlay<br>" +
    (showNavigating ? "Shown" : "Hidden") +
    "<br>(click to toggle)";
}

$(".act-reboot").addEventListener("click", async (e) => {
  e.preventDefault();
  if (!confirm("Are you sure you want to REBOOT the device?")) return;
  Dialog.loading.show("Rebooting...");
  try {
    await requestPost("/reboot", { action: "restart", confirm: "RESTART" });
  } catch (error) {
    Dialog.loading.hide();
    alert("Reboot failed: " + error.message);
    return;
  }

  // Wait until the delayed restart has taken the WebUI offline before checking for its return.
  await new Promise((resolve) => setTimeout(resolve, 1500));
  const deadline = Date.now() + 45000;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`/?restart-check=${Date.now()}`, { cache: "no-store" });
      if (response.ok) {
        window.location.href = "/";
        return;
      }
    } catch (_) {}
    await new Promise((resolve) => setTimeout(resolve, 1000));
  }
  Dialog.loading.hide();
  alert("The device has not returned yet. Reconnect to its Wi-Fi network and reload this page.");
});

$(".navigator-canvas").addEventListener("click", async (e) => {
  let nav = e.target.matches(".nav") ? e.target : e.target.closest(".nav");
  if (nav === null) return;

  let direction = nav.getAttribute("data-direction");
  if (direction === "Menu") {
    direction = "Sel 500";
  }

  await runNavigation(direction.toLowerCase());
});

window.addEventListener("keydown", async (e) => {
  let key = e.key.toLowerCase();
  if ($(".dialog.editor:not(.hidden)")) {
    // means editor tab is open
    if ((e.ctrlKey || e.metaKey) && key === "s") {
      e.preventDefault();
      e.stopImmediatePropagation();

      await saveEditorFile();
    } else if (e.altKey && key === "enter") {
      e.preventDefault();
      e.stopImmediatePropagation();

      await saveEditorFile(true);
    }
  }

  if ($(".dialog.navigator:not(.hidden)")) {
    const map_navigator = {
      arrowup: "Up",
      arrowdown: "Down",
      arrowleft: "Prev",
      arrowright: "Next",
      enter: "Sel",
      backspace: "Esc",
      m: "Menu",
      pageup: "NextPage",
      pagedown: "PrevPage",
    };

    if (key === "r") {
      e.preventDefault();
      e.stopImmediatePropagation();
      reloadScreen();
      return;
    }

    if (key in map_navigator) {
      e.preventDefault();
      e.stopImmediatePropagation();
      $(
        `.navigator-canvas .nav[data-direction="${map_navigator[key]}"]`,
      ).click();
      return;
    }
  }

  if (key === "escape" && $(".dialog-background:not(.hidden)")) {
    if ($(".dialog.editor:not(.hidden)")) {
      let editor = $(".dialog.editor .file-content");
      if (isModified(editor)) {
        if (
          !confirm("You have unsaved changes. Do you want to discard them?")
        ) {
          return;
        }
      }
    }

    let btnEscape = $(".dialog:not(.hidden) .act-escape");
    if (btnEscape) btnEscape.click();
    return;
  }
});

$(".file-content").addEventListener("keydown", function (e) {
  if (!$(".dialog.editor:not(.hidden)")) return;

  const textarea = this;
  const start = textarea.selectionStart;
  const end = textarea.selectionEnd;
  const TAB_SIZE = 2;
  const tabSpaces = " ".repeat(TAB_SIZE);

  const leadingSpacesRegex = /^ */;
  const closingCharRegex = /^[\}\)\]]/;

  const insertText = (text, newStart, newEnd, preserveSelection = true) => {
    textarea.setSelectionRange(start, end);
    document.execCommand("insertText", false, text);
    if (preserveSelection) {
      textarea.setSelectionRange(newStart, newEnd);
    } else {
      textarea.setSelectionRange(newStart, newStart);
    }
  };

  const getCurrentLine = (pos) => {
    const lineStart = textarea.value.lastIndexOf("\n", pos - 1) + 1;
    const lineEnd = textarea.value.indexOf("\n", pos);
    const line = textarea.value.slice(
      lineStart,
      lineEnd === -1 ? undefined : lineEnd,
    );
    return {
      line,
      lineStart,
      lineEnd: lineEnd === -1 ? textarea.value.length : lineEnd,
    };
  };

  const handleTab = (shift) => {
    if (start === end) {
      const { line, lineStart, lineEnd } = getCurrentLine(start);
      if (shift) {
        const remove = Math.min(
          line.match(leadingSpacesRegex)[0].length,
          TAB_SIZE,
        );
        textarea.setSelectionRange(lineStart, lineEnd);
        document.execCommand("insertText", false, line.slice(remove));
        textarea.setSelectionRange(start - remove, start - remove);
      } else {
        insertText(tabSpaces, start + TAB_SIZE, start + TAB_SIZE, false);
      }
      return;
    }

    // Expand selection to full first and last lines
    const { lineStart: firstLineStart } = getCurrentLine(start);
    const { lineEnd: lastLineEnd } = getCurrentLine(
      end === start ? end : end - 1,
    );

    const selectedFullText = textarea.value.slice(firstLineStart, lastLineEnd);
    const fullLines = selectedFullText.split("\n");

    let totalChange = 0;
    const newTextLines = fullLines.map((line, idx) => {
      const isLast = idx === fullLines.length - 1;
      const skipLast = isLast && /^\s*$/.test(line);

      if (skipLast) return line;

      const leadingSpaces = line.match(leadingSpacesRegex)[0].length;

      if (shift) {
        const remove = Math.min(leadingSpaces, TAB_SIZE);
        totalChange -= remove;
        return line.slice(remove);
      } else {
        const add = TAB_SIZE - (leadingSpaces % TAB_SIZE);
        totalChange += add;
        return " ".repeat(add) + line;
      }
    });

    // Replace the expanded selection using execCommand to preserve undo
    // This may become an issue when execCommand is removed since it's deprecated but only way to preserve undo for now
    textarea.setSelectionRange(firstLineStart, lastLineEnd);
    document.execCommand("insertText", false, newTextLines.join("\n"));
    textarea.setSelectionRange(
      firstLineStart,
      firstLineStart + newTextLines.join("\n").length,
    );
  };

  const handleEnter = () => {
    const { line } = getCurrentLine(start);
    const indentation = line.match(leadingSpacesRegex)[0] || "";

    const nextChar = start < textarea.value.length ? textarea.value[start] : "";
    const prevChar = start > 0 ? textarea.value[start - 1] : "";
    const pairs = { "{": "}", "(": ")", "[": "]" };

    if (pairs[prevChar] === nextChar) {
      const extraIndent = " ".repeat(TAB_SIZE);
      const insert = `\n${indentation + extraIndent}\n${indentation}`;
      insertText(
        insert,
        start + indentation.length + extraIndent.length + 1,
        start + indentation.length + extraIndent.length + 1,
      );
    } else {
      const closingLine = closingCharRegex.test(nextChar)
        ? "\n" + indentation
        : "";
      insertText(
        "\n" + indentation + closingLine,
        start + indentation.length + 1,
        start + indentation.length + 1,
      );
    }
  };

  const handleAutoPair = (key) => {
    const pairs = {
      "(": ")",
      "{": "}",
      "[": "]",
      '"': '"',
      "'": "'",
      "`": "`",
      "<": ">",
    };

    if (start === end) {
      // No selection - insert pair at cursor
      insertText(key + pairs[key], start + 1, start + 1, false);
    } else {
      // Has selection - wrap selected text with pair
      const selectedText = textarea.value.slice(start, end);
      const wrappedText = key + selectedText + pairs[key];
      insertText(wrappedText, start + 1, start + 1 + selectedText.length, true);
    }
  };

  const handleSkipCloser = () => {
    textarea.setSelectionRange(start + 1, start + 1);
  };

  const handleComment = (commentStr) => {
    const toggleComment = (line) => {
      const indentation = line.match(leadingSpacesRegex)[0] || "";
      const content = line.slice(indentation.length);

      if (content.startsWith(commentStr + " ")) {
        return {
          line: indentation + content.slice(commentStr.length + 1),
          offset: -(commentStr.length + 1),
        };
      } else if (content.startsWith(commentStr)) {
        return {
          line: indentation + content.slice(commentStr.length),
          offset: -commentStr.length,
        };
      } else {
        return {
          line: indentation + commentStr + " " + content,
          offset: commentStr.length + 1,
        };
      }
    };

    const isCommented = (line) => {
      const content = line.slice(
        (line.match(leadingSpacesRegex)[0] || "").length,
      );
      return (
        content.startsWith(commentStr + " ") || content.startsWith(commentStr)
      );
    };

    if (start === end) {
      // Single line - toggle comment
      const { line, lineStart, lineEnd } = getCurrentLine(start);
      const { line: newLine, offset: cursorOffset } = toggleComment(line);

      textarea.setSelectionRange(lineStart, lineEnd);
      document.execCommand("insertText", false, newLine);
      textarea.setSelectionRange(start + cursorOffset, start + cursorOffset);
      return;
    }

    // Multiple lines - toggle comment for all lines
    const { lineStart: firstLineStart } = getCurrentLine(start);
    const { lineEnd: lastLineEnd } = getCurrentLine(
      end === start ? end : end - 1,
    );
    const fullLines = textarea.value
      .slice(firstLineStart, lastLineEnd)
      .split("\n");

    // Find the minimum indentation level (excluding empty lines)
    const nonEmptyLines = fullLines.filter((line) => line.trim().length > 0);
    const minIndentation = Math.min(
      ...nonEmptyLines.map(
        (line) => (line.match(leadingSpacesRegex)[0] || "").length,
      ),
    );
    const commentIndent = " ".repeat(minIndentation);

    const allCommented = nonEmptyLines.every(isCommented);

    const newTextLines = fullLines.map((line, idx) => {
      const isLast = idx === fullLines.length - 1;
      const skipLast = isLast && /^\s*$/.test(line);

      if (skipLast || line.trim().length === 0) return line;

      const indentation = line.match(leadingSpacesRegex)[0] || "";
      const content = line.slice(indentation.length);

      if (allCommented) {
        // Remove comments
        if (content.startsWith(commentStr + " ")) {
          return indentation + content.slice(commentStr.length + 1);
        } else if (content.startsWith(commentStr)) {
          return indentation + content.slice(commentStr.length);
        }
        return line;
      } else {
        // Add comments at minimum indentation level
        return commentIndent + commentStr + " " + line.slice(minIndentation);
      }
    });

    textarea.setSelectionRange(firstLineStart, lastLineEnd);
    document.execCommand("insertText", false, newTextLines.join("\n"));
    textarea.setSelectionRange(
      firstLineStart,
      firstLineStart + newTextLines.join("\n").length,
    );
  };

  switch (e.key) {
    case "Tab":
      e.preventDefault();
      handleTab(e.shiftKey);
      return;
    case "Enter":
      e.preventDefault();
      handleEnter();
      return;
    case "/":
      if (e.ctrlKey || e.metaKey) {
        e.preventDefault();
        handleComment("//");
        return;
      }
      break;
    case "#":
      if (e.ctrlKey || e.metaKey) {
        e.preventDefault();
        handleComment("#");
        return;
      }
      break;
  }

  const nextChar = start < textarea.value.length ? textarea.value[start] : "";
  const closers = [")", "}", "]", ">", '"', "'", "`"];
  if (closers.includes(e.key) && nextChar === e.key) {
    e.preventDefault();
    handleSkipCloser();
    return;
  }

  const pairs = {
    "(": ")",
    "{": "}",
    "[": "]",
    '"': '"',
    "'": "'",
    "`": "`",
    "<": ">",
  };
  if (e.key in pairs) {
    e.preventDefault();
    handleAutoPair(e.key);
    return;
  }
});

$(".file-content").addEventListener("keyup", function (e) {
  if ($(".dialog.editor:not(.hidden)")) {
    $(".act-save-edit-file").disabled = !isModified(e.target);
    // Update line numbers when content changes
    updateLineNumbers();
  }
});

$(".file-content").addEventListener("scroll", function (e) {
  if ($(".dialog.editor:not(.hidden)")) {
    // Sync scrolling between textarea and line numbers
    syncScrolling();
  }
});

$(".file-content").addEventListener("input", function (e) {
  if ($(".dialog.editor:not(.hidden)")) {
    // Update line numbers on any input change
    updateLineNumbers();
  }
});

$(".oinput-text-submit").addEventListener("keyup", function (e) {
  // Submit using default button on Enter key
  if (e.key === "Enter" || e.keyCode === 13) {
    e.preventDefault();
    const dialog = this.closest(".dialog");
    const btn = dialog.querySelector(".btn-default");
    if (btn) btn.click();
  }
});

// Handle browser back/forward navigation
window.addEventListener("popstate", (event) => {
  if (event.state && event.state.drive && event.state.path) {
    fetchFiles(event.state.drive, event.state.path);

    // Restore edit state if present
    if (event.state.editFile) {
      setTimeout(async () => {
        try {
          let editor = $(".dialog.editor .file-content");
          $(".dialog.editor .editor-file-name").textContent =
            event.state.editFile;
          editor.value = "";

          Dialog.loading.show("Fetching content...");
          let r = await requestGet(
            `/file?fs=${event.state.drive}&name=${encodeURIComponent(event.state.editFile)}&action=edit`,
          );
          editor.value = r;
          editor.setAttribute("data-hash", calcHash(r));

          // Update line numbers
          updateLineNumbers();

          $(".act-save-edit-file").disabled = true;

          let serial = getSerialCommand(event.state.editFile);
          if (serial === undefined) {
            $(".act-run-edit-file").classList.add("hidden");
          } else {
            $(".act-run-edit-file").classList.remove("hidden");
          }

          Dialog.loading.hide();
          Dialog.show("editor");
        } catch (error) {
          console.error("Failed to restore file editor:", error);
        }
      }, 100);
    }
  } else {
    // Fallback: parse URL parameters
    const urlParams = getURLParams();
    const drive = urlParams.drive || (sdCardAvailable ? "SD" : "LittleFS");
    const path = urlParams.path || "/";
    fetchFiles(drive, path);

    // Handle edit file restoration from URL
    if (urlParams.editFile) {
      setTimeout(async () => {
        try {
          let editor = $(".dialog.editor .file-content");
          $(".dialog.editor .editor-file-name").textContent =
            urlParams.editFile;
          editor.value = "";

          Dialog.loading.show("Fetching content...");
          let r = await requestGet(
            `/file?fs=${drive}&name=${encodeURIComponent(urlParams.editFile)}&action=edit`,
          );
          editor.value = r;
          editor.setAttribute("data-hash", calcHash(r));

          // Update line numbers
          updateLineNumbers();

          $(".act-save-edit-file").disabled = true;

          let serial = getSerialCommand(urlParams.editFile);
          if (serial === undefined) {
            $(".act-run-edit-file").classList.add("hidden");
          } else {
            $(".act-run-edit-file").classList.remove("hidden");
          }

          Dialog.loading.hide();
          Dialog.show("editor");
        } catch (error) {
          console.error("Failed to restore file editor from URL:", error);
          updateURL(drive, path, null);
        }
      }, 100);
    }
  }
});

(async function () {
  await fetchSystemInfo();
  await initHeltecPanel();

  // Get initial state from URL parameters or use defaults
  const urlParams = getURLParams();
  let initialDrive = urlParams.drive;
  let initialPath = urlParams.path;
  let editFile = urlParams.editFile;

  // Validate and fallback to defaults if needed
  if (!initialDrive) {
    initialDrive = sdCardAvailable ? "SD" : "LittleFS";
  }
  if (!initialPath) {
    initialPath = "/";
  }

  await fetchFiles(initialDrive, initialPath);

  // If there's an edit file parameter, open the file editor
  if (editFile) {
    setTimeout(async () => {
      try {
        let editor = $(".dialog.editor .file-content");
        $(".dialog.editor .editor-file-name").textContent = editFile;
        editor.value = "";

        // Load file content
        Dialog.loading.show("Fetching content...");
        let r = await requestGet(
          `/file?fs=${currentDrive}&name=${encodeURIComponent(editFile)}&action=edit`,
        );
        editor.value = r;
        editor.setAttribute("data-hash", calcHash(r));

        // Update line numbers
        updateLineNumbers();

        $(".act-save-edit-file").disabled = true;

        let serial = getSerialCommand(editFile);
        if (serial === undefined) {
          $(".act-run-edit-file").classList.add("hidden");
        } else {
          $(".act-run-edit-file").classList.remove("hidden");
        }

        Dialog.loading.hide();
        Dialog.show("editor");
      } catch (error) {
        console.error("Failed to open file for editing:", error);
        // Remove edit parameter from URL if file loading fails
        updateURL(currentDrive, currentPath, null);
      }
    }, 100); // Small delay to ensure the file list is loaded first
  }
})();

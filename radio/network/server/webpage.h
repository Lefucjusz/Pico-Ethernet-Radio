#pragma once

static const char *webpage = R"HTML(
    <!DOCTYPE html>
    <html lang="en">
    <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>RP2040 Internet Radio</title>

    <style>

    :root {
        --bg: #0b0b0b;
        --card: #161616;
        --display: #0d1117;
        --border: #303030;
        --text: #d8d8d8;
        --green: #64ff64;
        --green-dark: #1b5e20;
        --red: #ff7070;
        --red-dark: #7f1d1d;
        --yellow: #ffcc00;
    }

    * {
        box-sizing: border-box;
    }

    body {
        margin: 0;
        padding: 20px;
        background: var(--bg);
        color: var(--text);
        font-family: "Segoe UI", Arial, sans-serif;
    }

    .card {
        max-width: 550px;
        margin: auto;
        background: var(--card);
        border: 1px solid var(--border);
        border-radius: 16px;
        padding: 24px;
    }

    .title {
        text-align: center;
        font-size: 1.8em;
        font-weight: bold;
        margin-bottom: 20px;
    }

    .display {
        background: var(--display);
        border: 1px solid #263238;
        border-radius: 10px;
        padding: 16px;
        margin-bottom: 20px;
        font-family: Consolas, monospace;
    }

    .label {
        color: #8aa0aa;
        font-size: 0.8em;
        margin-top: 6px;
        margin-bottom: 6px;
    }

    .value {
        margin-bottom: 14px;
        word-break: break-word;
    }

    .green {
        color: var(--green);
    }

    .yellow {
        color: var(--yellow);
    }

    .red {
        color: var(--red);
    }

    .volume-header {
        display: flex;
        justify-content: space-between;
        margin-bottom: 8px;
        font-weight: bold;
    }

    input[type=text] {
        width: 100%;
        padding: 12px;
        background: #222;
        color: white;
        border: 1px solid #444;
        border-radius: 10px;
        font-size: 1em;
    }

    input[type=text]:focus {
        outline: none;
        border-color: var(--green);
    }

    input[type=range] {
        width: 100%;
        accent-color: var(--green);
        margin-bottom: 20px;
    }

    select {
        width: 100%;
        height: 160px;
        padding: 10px;
        background: #121212;
        color: var(--text);
        border: 1px solid var(--border);
        border-radius: 12px;
        font-size: 0.95em;
        outline: none;
        margin-bottom: 8px;
    }

    select option {
        padding: 6px;
        background: #121212;
        color: var(--text);
    }

    select option:checked {
        background: var(--green-dark);
        color: white;
    }

    button {
        border: none;
        color: white;
        padding: 14px;
        border-radius: 10px;
        cursor: pointer;
        font-size: 1em;
        font-weight: bold;
    }

    .buttons {
        display: flex;
        gap: 10px;
        margin-top: 15px;
        margin-bottom: 20px;
    }

    .play-btn {
        flex: 1;
        background: var(--green-dark);
    }

    .stop-btn {
        flex: 1;
        background: var(--red-dark);
    }

    .status {
        margin-top: 15px;
        min-height: 45px;
        padding: 12px;
        background: #222;
        border-radius: 10px;
        border: 1px solid #333;
        color: #aaa;
    }

    .advanced {
        margin-top: 25px;
        border-top: 1px solid #333;
        padding-top: 15px;
        color: #888;
    }

    .advanced summary {
        cursor: pointer;
        font-size: 0.9em;
        color: #777;
        user-select: none;
        margin-bottom: 10px;
    }

    .advanced summary:hover {
        color: #aaa;
    }

    .advanced-buttons {
        display: flex;
        gap: 10px;
        margin-top: 12px;
    }

    .advanced-buttons button {
        flex: 1;
    }

    .fota-btn,
    .reboot-btn {
        background: #2b2b2b;
        border: 1px solid #444;
        color: #aaa;
        padding: 10px 14px;
        border-radius: 8px;
        font-size: 0.85em;
    }

    .fota-btn:hover,
    .reboot-btn:hover {
        border-color: #666;
        color: #ddd;
    }

    </style>
    </head>

    <body>

    <div class="card">
        <div class="title">📻 RP2040 INTERNET RADIO</div>

        <div class="display">
            <div class="label">NOW PLAYING</div>
            <div class="value" id="nowPlaying">Nothing playing</div>

            <div class="label">STATUS</div>
            <div class="value green" id="state">● Ready</div>
        </div>

        <div class="volume-header">
            <span>VOLUME</span>
            <span id="volumeValue">50%</span>
        </div>

        <input type="range" id="volumeSlider" min="0" max="100" value="50">

        <div class="label">STATIONS</div>
        <select id="stationList" size="10"></select>

        <div class="label">STREAM URL</div>
        <input type="text" id="streamUrl" placeholder="Custom stream URL">

        <div class="buttons">
            <button class="play-btn" onclick="startStream()">▶ PLAY</button>
            <button class="stop-btn" onclick="stopStream()">■ STOP</button>
        </div>

        <div class="status" id="status">Ready</div>

        <details class="advanced">
            <summary>Advanced</summary>

            <input type="file" id="firmwareFile" accept=".bin" hidden>

            <button class="fota-btn" onclick="selectFirmware()">
                Update Firmware
            </button>

            <button class="reboot-btn" onclick="rebootDevice()">
                Reboot
            </button>
        </details>
    </div>

    <script>

    const stations =
    [
        { name: "Jedynka", url: "mp3.polskieradio.pl:8900" },
        { name: "Dwójka", url: "mp3.polskieradio.pl:8902" },
        { name: "Trójka", url: "mp3.polskieradio.pl:8904" },
        { name: "RMF FM", url: "195.150.20.242:8000/rmf_fm" },
        { name: "RMF Grunge", url: "195.150.20.242:8000/rmf_grunge" },
        { name: "RMF Rock Progresywny", url: "195.150.20.242:8000/rmf_rock_progresywny" },
        { name: "Radio ZET", url: "zt04.cdn.eurozet.pl/ZET090.mp3" },
        { name: "Antyradio", url: "an04.cdn.eurozet.pl/ant-web.mp3" },
        { name: "Eska ROCK", url: "ic1.smcdn.pl/5380-1.mp3" },
        { name: "Radio Kampus", url: "193.0.98.66:8005" },
        { name: "Radio Gdańsk", url: "stream.task.gda.pl:8000/rg1" },
        { name: "Rockserwis FM", url: "stream9.nadaje.com:8002/live" },
        { name: "Radio Złote Przeboje", url: "poznan7.radio.pionier.net.pl:8000/tuba9-1.mp3" },

        { name: "KEXP 90.3", url: "kexp.streamguys1.com/kexp128.mp3" },
        { name: "Le Mellotron", url: "listen.radioking.com/radio/477719/stream/534044" },
        { name: "Flower Power Radio", url: "uk1.streamingpulse.com:7000/;" },
        { name: "Psychedelicized", url: "cast1.asurahosting.com/proxy/psychedelicized/stream" },
        { name: "Radio Caroline", url: "78.129.202.200:8040/;" },
        { name: "Funky Radio", url: "funkyradio.streamingmedia.it/play.mp3" },
        { name: "MOROW", url: "stream.fr.morow.com:8080/morow_med.mp3" },
        { name: "Yacht Rock Miami", url: "usa20.fastcast4u.com:4100/1753014835" },
        { name: "SomaFM Groove Salad", url: "ice2.somafm.com/groovesalad-128-mp3" },
        { name: "Ignore Radio Shoegaze", url: "sp1.autopo.st/8026/stream" },
        { name: "iFusion Radio", url: "listen.radioking.com/radio/523747/stream/582004" },
        { name: "Jazz24", url: "knkx-live-a.edge.audiocdn.com/6285_128k" }
    ];

    const STATE = {
        GET_LINK: 0,
        GET_IP: 1,
        READY: 2,
        START_STREAM: 3,
        START_DECODER: 4,
        START_PLAYER: 5,
        PLAYING: 6,
        RESTART: 7,
        ERROR: 8
    };

    const stationList = document.getElementById("stationList");
    const streamUrl = document.getElementById("streamUrl");
    const volumeSlider = document.getElementById("volumeSlider");
    const volumeValue = document.getElementById("volumeValue");
    const nowPlaying = document.getElementById("nowPlaying");
    const state = document.getElementById("state");
    const firmwareFile = document.getElementById("firmwareFile");
    const status = document.getElementById("status");

    let lastVolume = null;
    let lastUrl = null;
    let lastState = null;
    let statusTimer = null;

    function renderStations()
    {
        stationList.innerHTML = "";

        stations.forEach((station, index) => {
            const option = document.createElement("option");

            option.textContent = `${String(index + 1).padStart(2, "0")}. ${station.name}`;
            option.value = station.url;

            stationList.appendChild(option);
        });
    }

    renderStations();

    stationList.addEventListener("change", () => {
        streamUrl.value = stationList.value;
    });

    volumeSlider.addEventListener("input", (e) => {
        volumeValue.textContent = `${e.target.value}%`;
    });

    volumeSlider.addEventListener("change", async (e) => {
        try {
            await fetch(`/volume?value=${e.target.value}`);
            setStatus(`Volume set to ${e.target.value}%`);
        }
        catch {
            setStatus("Failed to set volume");
        }
    });

    async function startStream()
    {
        if (lastState !== STATE.READY) {
            stopStream();
        }

        const url = streamUrl.value.trim();
        if (!url) {
            setStatus("Please select a station");
            return;
        }

        try {
            await fetch(`/start?url=${url}`);

            setStatus("Starting stream...");
            nowPlaying.textContent = url;
        }
        catch {
            setStatus("Failed to start stream");
        }
    }

    async function stopStream()
    {
        try {
            await fetch("/stop");
            setStatus("Stopped");
        }
        catch {
            setStatus("Failed to stop stream");
        }
    }

    function setStatus(text)
    {
        status.textContent = text;

        clearTimeout(statusTimer);

        statusTimer = setTimeout(() => {
            status.textContent = "";
        }, 3000);
    }

    async function waitForReboot()
    {
        const start = Date.now();

        while (Date.now() - start < 60000) {
            try {
                const res = await fetch("/status", {
                    cache: "no-store"
                });

                if (res.ok) {
                    setStatus("Device online, reloading...");

                    setTimeout(() => {
                        location.reload();
                    }, 1000);

                    return;
                }
            }
            catch {
                // Device still rebooting
            }

            await new Promise(resolve => setTimeout(resolve, 500));
        }

        setStatus("Reboot timeout");
    }

    function selectFirmware()
    {
        firmwareFile.click();
    }

    async function uploadFirmware()
    {
        const file = firmwareFile.files[0];
        if (!file.name.endsWith(".bin")) {
            setStatus("Invalid firmware file");
            return;
        }

        setStatus(`Uploading ${file.name} (${file.size} bytes)...`);

        try {
            const res = await fetch("/fota", {
                method: "POST",
                body: file
            });

            if (res.ok) {
                setStatus("Firmware uploaded. Rebooting...");
                waitForReboot();
            }
            else {
                setStatus(`Upload failed (${res.status})`);
            }
        }
        catch {
            setStatus("Upload error");
        }
        finally {
            firmwareFile.value = "";
        }
    }

    firmwareFile.addEventListener("change", () => {
        if (!firmwareFile.files.length) {
            return;
        }

        const file = firmwareFile.files[0];

        if (confirm(`Flash ${file.name}?`)) {
            uploadFirmware();
        }
        else {
            firmwareFile.value = "";
        }
    });

    async function rebootDevice()
    {
        if (!confirm("Reboot device?")) {
            return;
        }

        setStatus("Rebooting...");

        try {
            await fetch("/reboot");

            waitForReboot();
        }
        catch {
            setStatus("Reboot request failed");
        }
    }

    async function updateStatus()
    {
        try {
            const res = await fetch("/status");
            if (!res.ok) {
                return;
            }

            const data = await res.json();

            if (data.volume !== lastVolume) {
                lastVolume = data.volume;

                volumeSlider.value = data.volume;
                volumeValue.textContent = `${data.volume}%`;
            }

            const url = data.url || "";
            if (url !== lastUrl) {
                lastUrl = url;

                nowPlaying.textContent = url || "Nothing playing";
            }

            if (data.state !== lastState) {
                lastState = data.state;

                let text = "● Unknown";
                let cls = "red";

                switch (data.state) {
                    case STATE.GET_LINK:
                        text = "● Getting link";
                        cls = "yellow";
                        break;

                    case STATE.GET_IP:
                        text = "● Getting IP";
                        cls = "yellow";
                        break;

                    case STATE.READY:
                        text = "● Ready";
                        cls = "green";
                        break;

                    case STATE.START_STREAM:
                        text = "● Starting stream";
                        cls = "yellow";
                        break;

                    case STATE.START_DECODER:
                        text = "● Starting decoder";
                        cls = "yellow";
                        break;

                    case STATE.START_PLAYER:
                        text = "● Starting player";
                        cls = "yellow";
                        break;

                    case STATE.PLAYING:
                        text = "● Playing";
                        cls = "green";
                        break;

                    case STATE.RESTART:
                        text = "● Awaiting restart";
                        cls = "yellow";
                        break;

                    case STATE.ERROR:
                        text = "● Error";
                        cls = "red";
                        break;
                }

                state.textContent = text;
                state.className = `value ${cls}`;
            }
        }
        catch {
            setStatus("Status update failed");
        }
    }

    setInterval(updateStatus, 1000);

    </script>

    </body>
    </html>

)HTML";

const char webpage[] = R"HTML(
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
        --red-dark: #7f1d1d;
    }

    * { box-sizing: border-box; }

    body {
        margin: 0;
        padding: 20px;
        background: var(--bg);
        color: var(--text);
        font-family: "Segoe UI", Arial, sans-serif;
    }

    .card {
        max-width: 500px;
        margin: 0 auto;
        background: var(--card);
        border: 1px solid var(--border);
        border-radius: 16px;
        padding: 24px;
        box-shadow: 0 0 30px rgba(0,0,0,0.5);
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
        margin-bottom: 6px;
    }

    .value {
        word-break: break-all;
        margin-bottom: 14px;
    }

    .green { color: var(--green); }
    .yellow { color: #ffcc00; }
    .red { color: #ff7070; }

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

    .buttons {
        display: flex;
        gap: 10px;
        margin-top: 15px;
        margin-bottom: 20px;
    }

    button {
        flex: 1;
        border: none;
        color: white;
        padding: 14px;
        border-radius: 10px;
        cursor: pointer;
        font-size: 1em;
        font-weight: bold;
    }

    .play-btn { background: var(--green-dark); }
    .play-btn:hover { filter: brightness(1.15); }

    .stop-btn { background: var(--red-dark); }
    .stop-btn:hover { filter: brightness(1.15); }

    .status {
        margin-top: 15px;
        padding: 12px;
        background: #222;
        border-radius: 10px;
        border: 1px solid #333;
        color: #aaa;
        font-size: 0.95em;
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
            <div class="value playing" id="state">● Ready</div>
        </div>

        <div class="volume-header">
            <span>VOLUME</span>
            <span id="volumeValue">50%</span>
        </div>

        <input type="range" id="volume" min="0" max="100">

        <div class="label">STREAM URL</div>

        <input type="text" id="streamUrl" placeholder="stream.example.com(:8000)" list="stations">

        <datalist id="stations">
            <option value="193.0.98.66:8005">Radio Kampus</option>
            <option value="mp3.polskieradio.pl:8904">Trójka</option>
            <option value="stream9.nadaje.com:8002/live">Radio Rockserwis FM</option>
            <option value="stream.fr.morow.com:8080/morow_med.mp3">MOROW</option>
        </datalist>

        <div class="buttons">
            <button class="play-btn" onclick="startStream()">▶ PLAY</button>
            <button class="stop-btn" onclick="stopStream()">■ STOP</button>
        </div>

        <div class="status" id="status">Ready</div>

    </div>

    <script>
    const volumeSlider = document.getElementById("volume");
    const volumeValue = document.getElementById("volumeValue");

    volumeSlider.addEventListener("input", function() {
        volumeValue.textContent = this.value + "%";
    });

    volumeSlider.addEventListener("change", async function() {
        try {
            await fetch("/volume?value=" + this.value);
            setStatus("Volume set to " + this.value + "%");
        } catch(e) {
            setStatus("Failed to set volume");
        }
    });

    async function startStream() {
        const url = document.getElementById("streamUrl").value.trim();

        if (!url) {
            setStatus("Please enter a stream URL");
            return;
        }

        try {
            await fetch("/start?url=" + url);
            setStatus("Stream started");
        } catch(e) {
            setStatus("Failed to start stream");
        }
    }

    async function stopStream() {
        try {
            await fetch("/stop");
            setStatus("Stream stopped");
        } catch(e) {
            setStatus("Failed to stop stream");
        }
    }

    function setStatus(text) {
        document.getElementById("status").textContent = text;
    }

    /* STATUS POLLING */
    async function updateStatus() {
        try {
            const res = await fetch("/status");
            if (!res.ok) return;

            const data = await res.json();

            // volume sync
            if (typeof data.volume === "number") {
                volumeSlider.value = data.volume;
                volumeValue.textContent = data.volume + "%";
            }

            // URL display
            const nowPlaying = document.getElementById("nowPlaying");

            if (!data.url || data.url.length === 0) {
                nowPlaying.textContent = "Nothing playing";
            } else {
                nowPlaying.textContent = data.url;
            }

            // state mapping
            const state = document.getElementById("state");

            let text = "● Unknown";
            let cls = "red"; // default

            switch (data.state) {
                case 0: text = "● Getting link"; cls = "yellow"; break;
                case 1: text = "● Getting IP"; cls = "yellow"; break;
                case 2: text = "● Ready"; cls = "green"; break;
                case 3: text = "● Starting stream"; cls = "yellow"; break;
                case 4: text = "● Starting decoder"; cls = "yellow"; break;
                case 5: text = "● Starting player"; cls = "yellow"; break;
                case 6: text = "● Playing"; cls = "green"; break;
                case 7: text = "● Awaiting stream restart"; cls = "yellow"; break;
                case 8: text = "● Error"; cls = "red"; break;
                default: text = "● Unknown"; cls = "red"; break;
            }

            state.textContent = text;
            state.className = `value ${cls}`;

        } catch (e) {
            setStatus("Status update failed");
        }
    }

    setInterval(updateStatus, 1000);
    updateStatus();
    </script>

    </body>
    </html>
)HTML";

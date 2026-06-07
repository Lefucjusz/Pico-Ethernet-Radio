#pragma once

static const char *webpage = 
    "<!DOCTYPE html>"
    "<html>"
        "<head>"
            "<title>RP2040 Internet Radio</title>"
        "</head>"

        "<body>"
            "<h2>RP2040 Internet Radio</h2>"

            "<p>Volume</p>"
            "<input type='range' min='0' max='100' value='50' id='volumeSlider' />"
            "<span id='volumeLabel'>50</span>"

            "<script>"
                "let slider = document.getElementById('volumeSlider');"
                "let label = document.getElementById('volumeLabel');"

                "let lastSent = 0;"
                "let idleTimeout = null;"

                "function send(value) {"
                "    fetch(`/api/volume?value=${value}`);"
                "}"

                "slider.oninput = (e) => {"
                "    const value = e.target.value;"
                "    label.innerHTML = value;"

                "    const now = Date.now();"

                "    if (now - lastSent > 250) {"
                "        send(value);"
                "        lastSent = now;"
                "    }"

                "    clearTimeout(idleTimeout);"
                "    idleTimeout = setTimeout(() => {"
                "        send(value);"
                "        lastSent = Date.now();"
                "    }, 250);"
                "};"
            "</script>"
        "</body>"
    "</html>";

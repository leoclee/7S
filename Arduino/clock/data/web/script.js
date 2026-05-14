// https://codeburst.io/throttling-and-debouncing-in-javascript-b01cad5c8edf
const throttle = (func, limit) => {
    let lastFunc
    let lastRan
    return function () {
        const context = this
        const args = arguments
        if (!lastRan) {
            func.apply(context, args)
            lastRan = Date.now()
        } else {
            clearTimeout(lastFunc)
            lastFunc = setTimeout(function () {
                if ((Date.now() - lastRan) >= limit) {
                    func.apply(context, args)
                    lastRan = Date.now()
                }
            }, limit - (Date.now() - lastRan))
        }
    }
}

const hueRange = document.getElementById("hueRange");
const satRange = document.getElementById("satRange");
const valRange = document.getElementById("valRange");

function updateBackground() {
    let hueScaled = hueRange.value * 360 / 255;
    let satScaled = satRange.value * 100 / 255;
    satRange.style.setProperty('--custom-bg', "linear-gradient(to right,#FFF,hsl(" + hueScaled + ",100%,50%))");
    valRange.style.setProperty('--custom-bg', "linear-gradient(to right,#000,hsl(" + hueScaled + ",100%," + (100 - satScaled / 2) + "%))");
}

function changeColor() {
    let params = new URLSearchParams({
        h: hueRange.value,
        s: satRange.value,
        v: valRange.value
    });
    console.log(params.toString());
    fetch(`api/color?${params.toString()}`, {method: 'PUT'});
}

hueRange.addEventListener("input", updateBackground);
satRange.addEventListener("input", updateBackground);
const throttledChangeColor = throttle(changeColor, 1000);
hueRange.addEventListener("input", throttledChangeColor);
satRange.addEventListener("input", throttledChangeColor);
valRange.addEventListener("input", throttledChangeColor);

// generate rainbow gradient for hue range background
let hueRangeBackground = "linear-gradient(to right";
for (let i = 0; i <= 360; i += 60) {
    hueRangeBackground += ",hsl(" + i + ",100%,50%)";
}
hueRangeBackground += ")";
hueRange.style.setProperty('--custom-bg', hueRangeBackground);

updateBackground();


document.querySelectorAll('input[type="checkbox"]').forEach(checkbox => {
    checkbox.addEventListener('change', () => {
        fetch(`api/${checkbox.id}?enabled=${checkbox.checked}`, {method: 'PUT'});
    })
});


const timeZoneSelect = document.getElementById("timeZone");
const tzDetect = document.getElementById("tzDetect");
tzDetect.addEventListener("click", () => {
    let browserTz = Intl.DateTimeFormat().resolvedOptions().timeZone;
    if (!browserTz || browserTz === "Etc/Unknown") {
        alert("unable to detect time zone or unsupported time zone: " + browserTz);
        return;
    }
    if (browserTz !== timeZoneSelect.value) {
        timeZoneSelect.value = browserTz;
        timeZoneSelect.dispatchEvent(new Event('change'));
    }
});
const timeZones = Intl.supportedValuesOf('timeZone');
for (const timeZone of timeZones) {
    if (timeZone === "Etc/Unknown") continue;
    const option = document.createElement("option");
    option.textContent = timeZone;
    timeZoneSelect.appendChild(option);
}
timeZoneSelect.addEventListener('change', () => {
    let tz = timeZoneSelect.value;
    console.log('tz change:', tz);

    if (tz === timeZoneSelect.options[0].value) {
        // remove time zone
        fetch("api/overrideTimeZone", {method: 'DELETE'});
    } else {
        // set time zone
        fetch("api/overrideTimeZone?value=" + tz, {method: 'PUT'}).then(response => {
            if (response.status === 400) alert("unsupported time zone: " + tz);
        });
    }
});

// Server-Sent Events (SSE)
if (!!window.EventSource) {
    const source = new EventSource('/events');
    source.addEventListener('open', function () {
        console.log("Events Connected");
    }, false);
    source.addEventListener('error', function (e) {
        if (e.target.readyState !== EventSource.OPEN) {
            console.log("Events Disconnected");
        }
    }, false);
    source.addEventListener('state', function (e) {
        console.log("state", e.data);
        try {
            // {"color":{"h":16,"s":209,"v":192},"colorCycle":false,"vRainbow":false,"hRainbow":false,"blink":false,"twelveHour":false,"timeZone":"America/Chicago"}
            const state = JSON.parse(e.data);
            hueRange.value = state['color']['h'];
            satRange.value = state['color']['s'];
            valRange.value = state['color']['v'];
            updateBackground();
            if (state['timeZone']) {
                timeZoneSelect.value = state['timeZone'];
            } else {
                // no time zone set--default to Automatic
                timeZoneSelect.value = timeZoneSelect.options[0].value;
            }
            document.querySelectorAll('input[type="checkbox"]').forEach(checkbox => {
                document.getElementById(checkbox.id).checked = state[checkbox.id];
            });
        } catch (error) {
            console.error('Error parsing state:', error);
        }
    }, false);
    source.addEventListener('heartbeat', function (e) {
        console.log(new Date().toLocaleString(), " heartbeat", e.data);
    }, false);
}
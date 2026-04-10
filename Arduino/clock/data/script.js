// https://codeburst.io/throttling-and-debouncing-in-javascript-b01cad5c8edf
const throttle = (func, limit) => {
  let lastFunc
  let lastRan
  return function() {
    const context = this
    const args = arguments
    if (!lastRan) {
      func.apply(context, args)
      lastRan = Date.now()
    } else {
      clearTimeout(lastFunc)
      lastFunc = setTimeout(function() {
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
    let hueScaled = hueRange.value*360/255;
    let satScaled = satRange.value*100/255;
    satRange.style.setProperty('--custom-bg', "linear-gradient(to right,#FFF,hsl(" + hueScaled + ",100%,50%))");
    valRange.style.setProperty('--custom-bg', "linear-gradient(to right,#000,hsl(" + hueScaled + ",100%," + (100 - satScaled/2) + "%))");
}

function changeColor() {
    let params = new URLSearchParams({
        h: hueRange.value,
        s: satRange.value,
        v: valRange.value
    });
    console.log(params.toString());
    let xhr = new XMLHttpRequest();
    xhr.open("PUT", `api/color?${params.toString()}`);
    xhr.send();
}

hueRange.addEventListener("input", updateBackground);
satRange.addEventListener("input", updateBackground);
var throttledChangeColor = throttle(changeColor, 1000);
hueRange.addEventListener("input", throttledChangeColor);
satRange.addEventListener("input", throttledChangeColor);
valRange.addEventListener("input", throttledChangeColor);

// generate rainbow gradient for hue range background
var hueRangeBackground = "linear-gradient(to right";
for (var i = 0; i <= 360; i += 60) {
    hueRangeBackground += ",hsl(" + i + ",100%,50%)";
}
hueRangeBackground += ")";
hueRange.style.setProperty('--custom-bg', hueRangeBackground);

updateBackground();



const rainbow = document.getElementById("rainbow");
rainbow.addEventListener('change', () => {
    let xhr = new XMLHttpRequest();
    xhr.open("PUT", "api/rainbow?enabled=" + rainbow.checked);
    xhr.send(this.value);
});
const twelveHour = document.getElementById("twelveHour");
twelveHour.addEventListener('change', () => {
    let xhr = new XMLHttpRequest();
    xhr.open("PUT", "api/twelveHour?enabled=" + twelveHour.checked);
    xhr.send(this.value);
});
const blink = document.getElementById("blink");
blink.addEventListener('change', () => {
    let xhr = new XMLHttpRequest();
    xhr.open("PUT", "api/blink?enabled=" + blink.checked);
    xhr.send(this.value);
});



const timeZoneSelect = document.getElementById("timeZone");
const tzDetect = document.getElementById("tzDetect");
tzDetect.addEventListener("click", () => {
    let browserTz = Intl.DateTimeFormat().resolvedOptions().timeZone;
    if (!browserTz || browserTz.startsWith("Etc") || browserTz === 'UTC') {
        alert("unable to detect time zone");
        return;
    }
    timeZoneSelect.value = browserTz;
    timeZoneSelect.dispatchEvent(new Event('change'));
});
const timeZones = Intl.supportedValuesOf('timeZone');
for (const timeZone of timeZones) {
    if (timeZone.startsWith("Etc") || timeZone === 'UTC') continue;
    const option = document.createElement("option");
    option.textContent = timeZone;
    timeZoneSelect.appendChild(option);
}
timeZoneSelect.addEventListener('change', () => {
    let tz = timeZoneSelect.value;
    console.log('tz change: ' + tz);

    if (tz === timeZoneSelect.options[0].value) {
        // remove time zone
        let xhr = new XMLHttpRequest();
        xhr.open("DELETE", "api/overrideTimeZone");
        xhr.send(this.value);
    } else {
        // set time zone
        let xhr = new XMLHttpRequest();
        xhr.open("PUT", "api/overrideTimeZone?value=" + tz);
        xhr.send(this.value);
    }
});

// Server-Sent Events (SSE)
if (!!window.EventSource) {
  var source = new EventSource('/events');
  source.addEventListener('open', function(e) {
    console.log("Events Connected");
  }, false);
  source.addEventListener('error', function(e) {
    if (e.target.readyState != EventSource.OPEN) {
      console.log("Events Disconnected");
    }
  }, false);
  source.addEventListener('state', function(e) {
    console.log("state", e.data);
    try {
        // {"color":{"h":16,"s":209,"v":192},"rainbow":false,"blink":false,"twelveHour":false,"timeZone":"America/Chicago"}
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
        rainbow.checked = state['rainbow'];
        blink.checked = state['blink'];
        twelveHour.checked = state['twelveHour'];
    } catch (error) {
        console.error('Error parsing state:', error);
    }
  }, false);
  source.addEventListener('heartbeat', function(e) {
    console.log("heartbeat", e.data);
  }, false);
}
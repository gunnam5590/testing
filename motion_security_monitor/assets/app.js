// Client-side logic for the Motion Security Monitor dashboard.
// Uses the WebUI Brick's JS client, which wraps a websocket connection
// back to python/main.py (ui.send_message / ui.on_message on the server).

const ui = new WebUI();

const videoFrame = document.getElementById("video-frame");
const badgeMotion = document.getElementById("badge-motion");
const badgePerson = document.getElementById("badge-person");
const badgeDistance = document.getElementById("badge-distance");
const logList = document.getElementById("log-list");

const MAX_LOG_ENTRIES = 200;

// The video_object_detection Brick serves its own annotated stream at
// /embed; retry until it's up (it can take a moment after the app starts).
function attachVideoFeed() {
  videoFrame.src = "/embed";
  videoFrame.onerror = () => setTimeout(attachVideoFeed, 1500);
}
attachVideoFeed();

function setBadge(el, text, cls) {
  el.textContent = text;
  el.className = "badge" + (cls ? " " + cls : "");
}

ui.on_message("status", (msg) => {
  if (typeof msg.motion === "boolean") {
    setBadge(badgeMotion, msg.motion ? "Motion: active" : "Motion: idle",
      msg.motion ? "on-info" : "");
  }
  if (typeof msg.person === "boolean") {
    setBadge(badgePerson, msg.person ? "Person: detected" : "Person: none",
      msg.person ? "on-alert" : "");
  }
});

ui.on_message("distance", (msg) => {
  const d = msg.distance_cm;
  if (d === null || d === undefined || d < 0) {
    setBadge(badgeDistance, "Distance: --", "");
    return;
  }
  const cls = d < 50 ? "on-alert" : d < 100 ? "on-warn" : "";
  setBadge(badgeDistance, `Distance: ${d.toFixed(0)} cm`, cls);
});

ui.on_message("log", (entry) => {
  const div = document.createElement("div");
  div.className = `log-entry ${entry.level}`;
  const time = new Date(entry.timestamp).toLocaleTimeString();
  div.innerHTML = `<span class="ts">${time}</span>${entry.message}`;
  logList.appendChild(div);

  while (logList.children.length > MAX_LOG_ENTRIES) {
    logList.removeChild(logList.firstChild);
  }
});

// Ask the server for the current state on load, in case we connected
// after motion/status events already happened.
ui.send_message("get_status", {});

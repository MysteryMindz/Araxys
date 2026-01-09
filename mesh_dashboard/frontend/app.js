/* const API = "http://localhost:8000"; // change if needed
let sosActive = false;

/* ---------------- NODE STATUS ---------------- */

function loadNodes() {
  fetch(`${API}/nodes`)
    .then(res => res.json())
    .then(nodes => {
      const container = document.getElementById("nodes");
      const select = document.getElementById("nodeSelect");

      container.innerHTML = "";
      select.innerHTML = "";

      Object.values(nodes).forEach(n => {
        const age = Date.now()/1000 - n.last_seen;
        let statusText = "ONLINE";
        let className = "";

        if (age > 30) {
          statusText = "OFFLINE";
          className = "offline";
        } else if (age > 10) {
          statusText = "DEGRADED";
          className = "degraded";
        }

        const card = document.createElement("div");
        card.className = `node-card ${className} ${n.sos ? "sos" : ""}`;
        card.innerHTML = `
          <b>${n.node_id}</b><br>
          Status: ${statusText}<br>
          Battery: ${n.battery}%<br>
          RSSI: ${n.rssi}<br>
          Hops: ${n.hop_count}<br>
          SOS: ${n.sos ? "🚨" : "—"}
        `;
        container.appendChild(card);

        const opt = document.createElement("option");
        opt.value = n.node_id;
        opt.textContent = n.node_id;
        select.appendChild(opt);
      });

      /* ---- NETWORK STATUS BAR (OUTSIDE LOOP) ---- */
      const nodesArr = Object.values(nodes);
      const offlineNodes = nodesArr.filter(
        n => (Date.now()/1000 - n.last_seen) > 30
      );

      const statusBar = document.getElementById("networkStatus");

      if (offlineNodes.length > 0) {
        statusBar.textContent = "Network Degraded";
        statusBar.style.background = "#7a1f1f";
      } else {
        statusBar.textContent = "Network Stable";
        statusBar.style.background = "#1f7a1f";
      }
    });
}


/* ---------------- MESSAGING ---------------- */

function sendMessage() {
  const to = document.getElementById("nodeSelect").value;
  const msg = document.getElementById("messageInput").value;

  fetch(`${API}/send-message`, {
    method: "POST",
    headers: {"Content-Type":"application/json"},
    body: JSON.stringify({ to, message: msg })
  });

  document.getElementById("messageInput").value = "";
}

function loadLogs() {
  fetch(`${API}/messages`)
    .then(res => res.json())
    .then(msgs => {
      const logs = document.getElementById("logs");
      logs.innerHTML = "";

      msgs.slice(-50).forEach(m => {
        const div = document.createElement("div");
        div.className = "log-entry";
        div.textContent = `[${m.from} → ${m.to}] ${m.payload} | hops:${m.hop_count} retries:${m.retry_count || 0}`;
        logs.appendChild(div);
      });

      logs.scrollTop = logs.scrollHeight;
    });
}

/* ---------------- SOS ---------------- */

function toggleSOS() {
  sosActive = !sosActive;

  fetch(`${API}/sos/${sosActive ? "on" : "off"}`, { method: "POST" });

  const btn = document.getElementById("sosBtn");

  if (sosActive) {
    btn.classList.add("active");
    btn.textContent = "SOS ON";
  } else {
    btn.classList.remove("active");
    btn.textContent = "SOS";
  }
}

/* ---------------- POLLING ---------------- */

setInterval(loadNodes, 3000);
setInterval(loadLogs, 3000);

loadNodes();
loadLogs(); */

const API = "http://localhost:8000";

let sosActive = false;

// ---------------- NODE STATUS ----------------
async function loadNodes() {
  const res = await fetch(`${API}/nodes`);
  const data = await res.json();

  const container = document.getElementById("nodes");
  const select = document.getElementById("nodeSelect");

  container.innerHTML = "";
  select.innerHTML = "";

  const nodes = Object.values(data);

  // Network banner
  document.getElementById("networkStatus").innerText =
    nodes.some(n => n.status === "offline")
      ? "Network Degraded"
      : "Network Stable";

  nodes.forEach(n => {
    // Node dropdown
    const opt = document.createElement("option");
    opt.value = n.node_id;
    opt.innerText = n.node_id;
    select.appendChild(opt);

    // Node card
    const card = document.createElement("div");
    card.className = `node-card ${n.status} ${n.sos ? "sos" : ""}`;

    card.innerHTML = `
      <b>${n.node_id}</b><br>
      RSSI: ${n.rssi}<br>
      Hops: ${n.hop_count}<br>
      Status: ${n.status.toUpperCase()}
    `;

    container.appendChild(card);
  });
}

// ---------------- MESSAGE ----------------
async function sendMessage() {
  const node = document.getElementById("nodeSelect").value;
  const text = document.getElementById("messageInput").value.trim();
  if (!text) return;

  await fetch(`${API}/message`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      sender: "DASHBOARD",
      message: text,
      counter: Date.now(),
      rssi: 0,
      timestamp: Date.now() / 1000
    })
  });

  document.getElementById("messageInput").value = "";
}

// ---------------- LOGS ----------------
async function loadLogs() {
  const res = await fetch(`${API}/messages`);
  const logs = await res.json();

  const container = document.getElementById("logs");
  container.innerHTML = "";

  logs.reverse().forEach(l => {
    const div = document.createElement("div");
    div.className = "log-entry";
    div.innerText = `[${l.sender}] ${l.message}`;
    container.appendChild(div);
  });
}

// ---------------- SOS ----------------
async function toggleSOS() {
  sosActive = !sosActive;
  const btn = document.getElementById("sosBtn");
  btn.classList.toggle("active", sosActive);

  await fetch(`${API}/sos`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      node_id: "DASHBOARD",
      active: sosActive,
      timestamp: Date.now() / 1000
    })
  });
}

// ---------------- POLLING ----------------
loadNodes();
loadLogs();
setInterval(loadNodes, 2000);
setInterval(loadLogs, 3000);


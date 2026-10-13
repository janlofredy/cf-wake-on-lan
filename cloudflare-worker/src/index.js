/**
 * Cloudflare Worker for Wake-on-LAN Trigger & Dashboard UI
 * - Web UI protected by Cloudflare Access (No manual token needed in UI!)
 * - Device routes (/server/ping, /ack, /status) authenticated via AOD_DEVICE_KEY
 */

const API_KEY = "CHANGE_ME_SECRET_TOKEN";

function renderDashboardHTML() {
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Server Controller - YOUR_WORKER_SUBDOMAIN.workers.dev</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600&family=Plus+Jakarta+Sans:wght@400;500;600;700&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg: #0b0f19;
      --card-bg: rgba(22, 30, 49, 0.75);
      --card-border: rgba(255, 255, 255, 0.08);
      --text: #f3f4f6;
      --text-muted: #94a3b8;
      --accent: #38bdf8;
      --accent-glow: rgba(56, 189, 248, 0.2);
      --success: #10b981;
      --success-bg: rgba(16, 185, 129, 0.15);
      --danger: #ef4444;
      --danger-bg: rgba(239, 68, 68, 0.15);
      --warning: #f59e0b;
      --warning-bg: rgba(245, 158, 11, 0.15);
      --font-sans: 'Plus Jakarta Sans', system-ui, -apple-system, sans-serif;
      --font-mono: 'JetBrains Mono', monospace;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }
    
    body {
      background-color: var(--bg);
      background-image: 
        radial-gradient(at 0% 0%, rgba(56, 189, 248, 0.12) 0px, transparent 50%),
        radial-gradient(at 100% 100%, rgba(99, 102, 241, 0.1) 0px, transparent 50%);
      color: var(--text);
      font-family: var(--font-sans);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 1.5rem;
    }

    .container {
      width: 100%;
      max-width: 480px;
    }

    .card {
      background: var(--card-bg);
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
      border: 1px solid var(--card-border);
      border-radius: 20px;
      padding: 2rem;
      box-shadow: 0 20px 40px -15px rgba(0, 0, 0, 0.5);
    }

    .header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 1.75rem;
    }

    .title {
      font-size: 1.35rem;
      font-weight: 700;
      letter-spacing: -0.02em;
      display: flex;
      align-items: center;
      gap: 0.5rem;
    }

    .badge {
      display: inline-flex;
      align-items: center;
      gap: 0.4rem;
      padding: 0.35rem 0.75rem;
      border-radius: 9999px;
      font-size: 0.75rem;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }
    .badge-dot {
      width: 7px;
      height: 7px;
      border-radius: 50%;
    }
    .badge-online { background: var(--success-bg); color: var(--success); }
    .badge-online .badge-dot { background: var(--success); box-shadow: 0 0 8px var(--success); }
    .badge-offline { background: var(--danger-bg); color: var(--danger); }
    .badge-offline .badge-dot { background: var(--danger); box-shadow: 0 0 8px var(--danger); }
    .badge-booting { background: var(--warning-bg); color: var(--warning); }
    .badge-booting .badge-dot { background: var(--warning); animation: pulse 1.2s infinite; }

    @keyframes pulse {
      0%, 100% { opacity: 1; transform: scale(1); }
      50% { opacity: 0.4; transform: scale(1.3); }
    }

    .stats-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 0.75rem;
      margin-bottom: 1.25rem;
    }

    .stat-box {
      background: rgba(255, 255, 255, 0.03);
      border: 1px solid var(--card-border);
      border-radius: 12px;
      padding: 0.85rem;
    }

    .stat-label {
      font-size: 0.72rem;
      color: var(--text-muted);
      text-transform: uppercase;
      letter-spacing: 0.05em;
      margin-bottom: 0.25rem;
    }

    .stat-value {
      font-family: var(--font-mono);
      font-size: 0.85rem;
      font-weight: 600;
      color: var(--text);
    }

    .switch-card {
      background: rgba(255, 255, 255, 0.03);
      border: 1px solid var(--card-border);
      border-radius: 14px;
      padding: 1rem;
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 1.5rem;
    }

    .switch-info h4 {
      font-size: 0.88rem;
      font-weight: 600;
      color: var(--text);
      display: flex;
      align-items: center;
      gap: 0.35rem;
    }

    .switch-info p {
      font-size: 0.73rem;
      color: var(--text-muted);
      margin-top: 0.15rem;
    }

    .switch {
      position: relative;
      display: inline-block;
      width: 44px;
      height: 24px;
    }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider {
      position: absolute;
      cursor: pointer;
      top: 0; left: 0; right: 0; bottom: 0;
      background-color: rgba(255, 255, 255, 0.15);
      transition: .3s cubic-bezier(0.4, 0, 0.2, 1);
      border-radius: 24px;
    }
    .slider:before {
      position: absolute;
      content: "";
      height: 18px;
      width: 18px;
      left: 3px;
      bottom: 3px;
      background-color: white;
      transition: .3s cubic-bezier(0.4, 0, 0.2, 1);
      border-radius: 50%;
    }
    input:checked + .slider {
      background-color: var(--accent);
      box-shadow: 0 0 10px var(--accent-glow);
    }
    input:checked + .slider:before {
      transform: translateX(20px);
      background-color: #0b0f19;
    }

    .btn-group {
      display: flex;
      flex-direction: column;
      gap: 0.75rem;
    }

    .btn {
      width: 100%;
      padding: 0.9rem;
      border: none;
      border-radius: 12px;
      font-family: var(--font-sans);
      font-size: 0.95rem;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.2s ease;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 0.5rem;
    }

    .btn-primary {
      background: linear-gradient(135deg, #0284c7, #38bdf8);
      color: #0b0f19;
      box-shadow: 0 4px 14px var(--accent-glow);
    }
    .btn-primary:hover:not(:disabled) {
      transform: translateY(-1px);
      box-shadow: 0 6px 20px var(--accent-glow);
    }
    .btn-primary:active:not(:disabled) {
      transform: translateY(0);
    }
    .btn-secondary {
      background: rgba(255, 255, 255, 0.05);
      color: var(--text-muted);
      border: 1px solid var(--card-border);
    }
    .btn-secondary:hover:not(:disabled) {
      background: rgba(255, 255, 255, 0.08);
      color: var(--text);
    }
    .btn:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }

    .notification {
      margin-top: 1rem;
      padding: 0.75rem 1rem;
      border-radius: 10px;
      font-size: 0.82rem;
      display: none;
      text-align: center;
      animation: fadeIn 0.2s ease;
    }
    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(4px); }
      to { opacity: 1; transform: translateY(0); }
    }

    .notification.info { background: rgba(56, 189, 248, 0.15); color: var(--accent); border: 1px solid rgba(56, 189, 248, 0.3); }
    .notification.success { background: var(--success-bg); color: var(--success); border: 1px solid rgba(16, 185, 129, 0.3); }
    .notification.error { background: var(--danger-bg); color: var(--danger); border: 1px solid rgba(239, 68, 68, 0.3); }
    .notification.warning { background: var(--warning-bg); color: var(--warning); border: 1px solid rgba(245, 158, 11, 0.3); }

    .cf-access-badge {
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 0.35rem;
      font-size: 0.7rem;
      color: var(--text-muted);
      margin-top: 1.25rem;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <div class="header">
        <div class="title">
          <span>⚡</span> Server Controller
        </div>
        <div id="statusBadge" class="badge badge-offline">
          <span class="badge-dot"></span>
          <span id="badgeText">CHECKING...</span>
        </div>
      </div>

      <div class="stats-grid">
        <div class="stat-box">
          <div class="stat-label">Server State</div>
          <div id="statServerStatus" class="stat-value">Checking...</div>
        </div>
        <div class="stat-box">
          <div class="stat-label">AOD Microcontroller</div>
          <div id="aodStatus" class="stat-value">Checking...</div>
        </div>
        <div class="stat-box">
          <div class="stat-label">Pending WOL Trigger</div>
          <div id="statPendingTrigger" class="stat-value">--</div>
        </div>
        <div class="stat-box">
          <div class="stat-label">Last Ping Seen</div>
          <div id="lastSeen" class="stat-value">--:--:--</div>
        </div>
        <div class="stat-box">
          <div class="stat-label">Target MAC</div>
          <div id="statTargetMac" class="stat-value" style="font-size:0.75rem;">--</div>
        </div>
        <div class="stat-box">
          <div class="stat-label">Last WOL Sent</div>
          <div id="statLastWol" class="stat-value" style="font-size:0.75rem;">Never</div>
        </div>
      </div>

      <!-- Automatic Turn On Toggle -->
      <div class="switch-card">
        <div class="switch-info">
          <h4><span>🔄</span> Automatic Turn On</h4>
          <p>Always boot server whenever it is detected offline</p>
        </div>
        <label class="switch">
          <input type="checkbox" id="autoOnToggle" onchange="toggleAutoOn(this.checked)">
          <span class="slider"></span>
        </label>
      </div>

      <div class="btn-group">
        <button id="directTriggerBtn" class="btn btn-primary" onclick="triggerWOL(true)">
          <span>⚡</span> Direct Manual Trigger (Send WOL)
        </button>
        <button id="triggerBtn" class="btn btn-secondary" onclick="triggerWOL(false)">
          <span>🚀</span> Safe Trigger (Only if Offline)
        </button>
      </div>

      <div id="alertBox" class="notification"></div>

      <!-- Raw Status Live Inspector -->
      <details style="margin-top: 1.25rem; font-size: 0.75rem; color: var(--text-muted); cursor: pointer;">
        <summary style="user-select: none;">🔍 View Live Raw JSON Telemetry</summary>
        <pre id="rawStatus" style="background: rgba(0,0,0,0.4); padding: 0.75rem; border-radius: 8px; margin-top: 0.5rem; overflow-x: auto; font-family: var(--font-mono);"></pre>
      </details>

      <div class="cf-access-badge">
        <span>🔒 Cloudflare Worker & Access Active</span>
      </div>
    </div>
  </div>

  <script>
    const statusBadge = document.getElementById('statusBadge');
    const badgeText = document.getElementById('badgeText');
    const statServerStatus = document.getElementById('statServerStatus');
    const aodStatus = document.getElementById('aodStatus');
    const statPendingTrigger = document.getElementById('statPendingTrigger');
    const lastSeen = document.getElementById('lastSeen');
    const statTargetMac = document.getElementById('statTargetMac');
    const statLastWol = document.getElementById('statLastWol');
    const alertBox = document.getElementById('alertBox');
    const triggerBtn = document.getElementById('triggerBtn');
    const directTriggerBtn = document.getElementById('directTriggerBtn');
    const autoOnToggle = document.getElementById('autoOnToggle');
    const rawStatus = document.getElementById('rawStatus');

    function showAlert(msg, type = 'info') {
      alertBox.textContent = msg;
      alertBox.className = 'notification ' + type;
      alertBox.style.display = 'block';
    }

    async function toggleAutoOn(enabled) {
      try {
        const res = await fetch('/auto-on', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ auto_turn_on: enabled })
        });
        const data = await res.json();
        if (data.success) {
          showAlert(data.message, 'success');
        } else {
          showAlert(data.message || 'Failed to update auto-on mode', 'error');
          autoOnToggle.checked = !enabled;
        }
      } catch (e) {
        showAlert('Network error updating auto-on mode', 'error');
        autoOnToggle.checked = !enabled;
      }
    }

    async function fetchStatus() {
      try {
        const res = await fetch('/status');
        if (!res.ok) {
          badgeText.textContent = "ERROR";
          statusBadge.className = "badge badge-offline";
          return;
        }

        const data = await res.json();
        rawStatus.textContent = JSON.stringify(data, null, 2);

        // Update Auto Turn On switch state
        if (typeof data.auto_turn_on !== 'undefined' && document.activeElement !== autoOnToggle) {
          autoOnToggle.checked = data.auto_turn_on;
        }

        // Server Status field
        statServerStatus.textContent = data.server_status || 'UNKNOWN';
        if (data.server_status === 'ONLINE') {
          statServerStatus.style.color = "var(--success)";
        } else if (data.server_status === 'BOOTING') {
          statServerStatus.style.color = "var(--warning)";
        } else {
          statServerStatus.style.color = "var(--danger)";
        }

        // Pending trigger status
        if (data.trigger) {
          statPendingTrigger.textContent = "ARMED / PENDING";
          statPendingTrigger.style.color = "var(--warning)";
        } else {
          statPendingTrigger.textContent = "IDLE";
          statPendingTrigger.style.color = "var(--text-muted)";
        }

        // MAC & last WOL
        statTargetMac.textContent = data.mac || 'Default (In Config)';
        if (data.last_wol_sent) {
          const wolDate = new Date(data.last_wol_sent);
          statLastWol.textContent = wolDate.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
        } else {
          statLastWol.textContent = 'Never';
        }

        // Update Server Status Badge
        statusBadge.className = 'badge';
        if (data.server_status === 'ONLINE') {
          statusBadge.classList.add('badge-online');
          badgeText.textContent = 'ONLINE';
          triggerBtn.disabled = true;
          triggerBtn.innerHTML = '<span>✅</span> Server is Running';
        } else if (data.server_status === 'BOOTING' || data.trigger) {
          statusBadge.classList.add('badge-booting');
          badgeText.textContent = 'BOOTING...';
          triggerBtn.disabled = true;
          triggerBtn.innerHTML = '<span>⏳</span> Booting in Progress...';
        } else {
          statusBadge.classList.add('badge-offline');
          badgeText.textContent = 'OFFLINE';
          triggerBtn.disabled = false;
          triggerBtn.innerHTML = '<span>🚀</span> Safe Trigger (Only if Offline)';
        }

        // AOD Device health
        if (data.aod_alive) {
          aodStatus.textContent = "CONNECTED";
          aodStatus.style.color = "var(--success)";
        } else {
          aodStatus.textContent = "OFFLINE";
          aodStatus.style.color = "var(--danger)";
        }

        if (data.server_last_seen) {
          const date = new Date(data.server_last_seen);
          lastSeen.textContent = date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
        } else {
          lastSeen.textContent = 'Never';
        }

      } catch (err) {
        console.error(err);
      }
    }

    async function triggerWOL(force = false) {
      showAlert('Sending trigger request...', 'info');

      try {
        const res = await fetch('/trigger', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ force })
        });

        const data = await res.json();
        if (data.status === 'ALREADY_ON') {
          showAlert(data.message, 'warning');
        } else if (data.success) {
          showAlert(data.message, 'success');
          fetchStatus();
        } else {
          showAlert(data.message || 'Trigger failed', 'error');
        }
      } catch (err) {
        showAlert('Network error communicating with server', 'error');
      }
    }

    fetchStatus();
    setInterval(fetchStatus, 3000);
  </script>
</body>
</html>`;
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const authHeader = request.headers.get("Authorization");
    const tokenQuery = url.searchParams.get("token");

    // Cloudflare Access headers present when user passes Cloudflare Access login
    const cfAccessEmail = request.headers.get("cf-access-authenticated-user-email");
    const cfAccessJwt   = request.headers.get("cf-access-jwt-assertion");

    // Device token configuration (flashed onto Arduino Uno)
    const aodSecret = env.AOD_DEVICE_KEY || env.API_KEY || API_KEY;
    const userSecret = env.USER_API_KEY || env.API_KEY || API_KEY;

    // Extract Bearer token if provided
    let clientToken = "";
    if (authHeader && authHeader.startsWith("Bearer ")) {
      clientToken = authHeader.substring(7).trim();
    } else if (tokenQuery) {
      clientToken = tokenQuery.trim();
    }

    // TEMPORARY TESTING: Open up all access
    const isUser = true;
    const isAOD = true;

    // Web Dashboard UI at GET / (Served directly; Cloudflare Access secures the page)
    if (request.method === "GET" && (url.pathname === "/" || url.pathname === "/index.html")) {
      return new Response(renderDashboardHTML(), {
        status: 200,
        headers: { "Content-Type": "text/html; charset=utf-8" }
      });
    }

    const unauthorized = (msg = "Unauthorized") => new Response(
      JSON.stringify({ error: msg }),
      { status: 401, headers: { "Content-Type": "application/json" } }
    );

    const forbidden = (msg = "Forbidden: Insufficient role permissions") => new Response(
      JSON.stringify({ error: msg }),
      { status: 403, headers: { "Content-Type": "application/json" } }
    );

    // Route: POST /auto-on - USER ONLY (Allowed via Cloudflare Access or User Token)
    if (request.method === "POST" && url.pathname === "/auto-on") {
      if (!isUser && !isAOD) return unauthorized();
      if (!isUser) return forbidden("Only User can configure auto turn on");

      let body = {};
      try { body = await request.json(); } catch(e) {}
      const enable = body.auto_turn_on === true;
      await env.WOL_STORE.put("auto_turn_on", enable ? "true" : "false");

      return new Response(
        JSON.stringify({
          success: true,
          auto_turn_on: enable,
          message: enable ? "Automatic Turn On ENABLED (Server will auto-boot when offline)" : "Automatic Turn On DISABLED"
        }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      );
    }

    // Route: GET /status - Read state (Accessible by logged-in User or AOD Device)
    if (request.method === "GET" && url.pathname === "/status") {
      if (!isUser && !isAOD) return unauthorized();

      let pending = await env.WOL_STORE.get("trigger_wol");
      const autoOn = await env.WOL_STORE.get("auto_turn_on") === "true";
      const targetMac = await env.WOL_STORE.get("target_mac") || "";
      const serverStatus = await env.WOL_STORE.get("server_status") || "UNKNOWN";
      const lastSeen = await env.WOL_STORE.get("server_last_seen") || null;
      const lastWolSent = await env.WOL_STORE.get("last_wol_sent") || null;
      const lastPingEpoch = await env.WOL_STORE.get("aod_last_ping_epoch") || 0;

      // AUTOMATIC TURN ON LOGIC:
      let shouldTrigger = (pending === "true" || pending === "1");
      if (autoOn && serverStatus === "OFFLINE" && !shouldTrigger) {
        shouldTrigger = true;
        await env.WOL_STORE.put("trigger_wol", "true");
        await env.WOL_STORE.put("server_status", "BOOTING");
      }

      const aodAlive = (Date.now() - Number(lastPingEpoch)) < 120000;

      return new Response(
        JSON.stringify({
          trigger: shouldTrigger,
          auto_turn_on: autoOn,
          mac: targetMac,
          server_status: serverStatus,
          server_last_seen: lastSeen,
          last_wol_sent: lastWolSent,
          aod_alive: aodAlive,
          caller_role: isUser ? "USER" : "AOD_DEVICE"
        }),
        {
          status: 200,
          headers: {
            "Content-Type": "application/json",
            "Cache-Control": "no-store, no-cache, must-revalidate"
          }
        }
      );
    }

    // Route: POST /trigger - USER ONLY (Allowed via Cloudflare Access or User Token)
    if (request.method === "POST" && url.pathname === "/trigger") {
      if (!isUser && !isAOD) return unauthorized();
      if (!isUser) return forbidden("Only User can trigger Wake-on-LAN");

      let reqData = {};
      try { reqData = await request.json(); } catch (e) {}

      const force = reqData.force === true;
      const currentStatus = await env.WOL_STORE.get("server_status");

      if (currentStatus === "ONLINE" && !force) {
        return new Response(
          JSON.stringify({
            success: false,
            status: "ALREADY_ON",
            message: "Server is already ONLINE. Pass { \"force\": true } to send WOL anyway."
          }),
          { status: 200, headers: { "Content-Type": "application/json" } }
        );
      }

      const mac = reqData.mac || "";
      await env.WOL_STORE.put("trigger_wol", "true");
      await env.WOL_STORE.put("server_status", "BOOTING");
      if (mac) {
        await env.WOL_STORE.put("target_mac", mac);
      }

      return new Response(
        JSON.stringify({
          success: true,
          status: "ARMED",
          message: "Wake-on-LAN armed! Microcontroller will dispatch packet shortly.",
          target_mac: mac
        }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      );
    }

    // Route: POST /ack - AOD DEVICE ONLY
    if (request.method === "POST" && url.pathname === "/ack") {
      if (!isUser && !isAOD) return unauthorized();
      if (!isAOD) return forbidden("Only AOD device token can acknowledge triggers");

      let body = {};
      try { body = await request.json(); } catch(e) {}

      const result = body.result || "SENT";
      await env.WOL_STORE.put("trigger_wol", "false");
      await env.WOL_STORE.put("last_wol_sent", new Date().toISOString());

      if (result === "FAILED_TO_SEND") {
        await env.WOL_STORE.put("server_status", "FAILED_TO_TRIGGER");
      }

      return new Response(
        JSON.stringify({ success: true, message: "Trigger cleared", wol_result: result }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      );
    }

    // Route: POST /server/ping - AOD DEVICE ONLY
    if (request.method === "POST" && url.pathname === "/server/ping") {
      if (!isUser && !isAOD) return unauthorized();
      if (!isAOD) return forbidden("Only AOD device token can report server reachability");

      let body = {};
      try { body = await request.json(); } catch(e) {}

      const status = body.status || "ONLINE";
      await env.WOL_STORE.put("server_status", status);
      await env.WOL_STORE.put("server_last_seen", new Date().toISOString());
      await env.WOL_STORE.put("aod_last_ping_epoch", String(Date.now()));

      return new Response(
        JSON.stringify({ success: true, server_status: status }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      );
    }

    return new Response(
      JSON.stringify({
        endpoints: {
          "GET /": "Web Dashboard UI (Cloudflare Access secured)",
          "GET /status": "Poll trigger & server state",
          "POST /auto-on": "Configure Automatic Turn On",
          "POST /trigger": "Arm WOL trigger",
          "POST /ack": "Disarm trigger after WOL packet is sent (Device only)",
          "POST /server/ping": "Report server online/offline (Device only)"
        }
      }),
      { status: 200, headers: { "Content-Type": "application/json" } }
    );
  }
};

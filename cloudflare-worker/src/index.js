/**
 * Cloudflare Worker for Wake-on-LAN Trigger & Dashboard UI
 * - Web UI protected by Password/Token Authentication or Cloudflare Access
 * - Dual security: Cloudflare Access (Zero Trust) + Token Auth with auto-login & local storage
 * - Device routes (/server/ping, /ack, /status) authenticated via secret token
 */

const API_KEY = "CHANGE_ME_SECRET_TOKEN";

function renderDashboardHTML(hasCfAccess = false) {
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Server Controller - Wake-on-LAN</title>
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
      position: relative;
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

    .header-actions {
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
    .badge-auth { background: rgba(56, 189, 248, 0.15); color: var(--accent); }

    @keyframes pulse {
      0%, 100% { opacity: 1; transform: scale(1); }
      50% { opacity: 0.4; transform: scale(1.3); }
    }

    .lock-btn {
      background: rgba(255, 255, 255, 0.05);
      border: 1px solid var(--card-border);
      color: var(--text-muted);
      border-radius: 8px;
      padding: 0.35rem 0.6rem;
      font-size: 0.75rem;
      cursor: pointer;
      display: flex;
      align-items: center;
      gap: 0.25rem;
      transition: all 0.2s ease;
    }
    .lock-btn:hover {
      color: var(--text);
      background: rgba(255, 255, 255, 0.1);
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
      margin-bottom: 1.25rem;
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

    .input-group {
      margin-bottom: 1.25rem;
    }
    .input-label {
      display: block;
      font-size: 0.78rem;
      font-weight: 600;
      color: var(--text-muted);
      margin-bottom: 0.4rem;
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }
    .input-field {
      width: 100%;
      padding: 0.85rem 1rem;
      background: rgba(255, 255, 255, 0.04);
      border: 1px solid var(--card-border);
      border-radius: 10px;
      color: var(--text);
      font-family: var(--font-mono);
      font-size: 0.9rem;
      transition: all 0.2s ease;
    }
    .input-field:focus {
      outline: none;
      border-color: var(--accent);
      box-shadow: 0 0 0 3px var(--accent-glow);
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

    .security-footer {
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 0.35rem;
      font-size: 0.72rem;
      color: var(--text-muted);
      margin-top: 1.25rem;
    }

    .view-section {
      display: none;
    }
    .view-active {
      display: block;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      
      <!-- ==================== VIEW 1: AUTHENTICATION LOCK VIEW ==================== -->
      <div id="authView" class="view-section">
        <div class="header">
          <div class="title">
            <span>🔒</span> Dashboard Locked
          </div>
          <span class="badge badge-auth">Security</span>
        </div>

        <p style="color:var(--text-muted); font-size:0.85rem; margin-bottom:1.25rem; line-height:1.5;">
          Enter your Access Key or Secret Token to unlock the Wake-on-LAN server controls.
        </p>

        <form id="authForm" onsubmit="handleLogin(event)">
          <div class="input-group">
            <label class="input-label" for="tokenInput">Secret Access Token</label>
            <input 
              type="password" 
              id="tokenInput" 
              class="input-field" 
              placeholder="Enter secret token..." 
              autocomplete="current-password"
              required
            />
          </div>

          <button type="submit" id="unlockBtn" class="btn btn-primary">
            <span>🔑</span> Unlock Server Controller
          </button>
        </form>

        <div id="authAlert" class="notification"></div>

        <div class="security-footer">
          <span>🔒 Protected with Token & Role Authentication</span>
        </div>
      </div>

      <!-- ==================== VIEW 2: MAIN DASHBOARD VIEW ==================== -->
      <div id="dashboardView" class="view-section">
        <div class="header">
          <div class="title">
            <span>⚡</span> Server Controller
          </div>
          <div class="header-actions">
            <div id="statusBadge" class="badge badge-offline">
              <span class="badge-dot"></span>
              <span id="badgeText">CHECKING...</span>
            </div>
            <button id="lockBtn" class="lock-btn" onclick="handleLogout()" title="Lock / Log Out">
              <span>🔒</span> Lock
            </button>
          </div>
        </div>

        <div class="stats-grid">
          <div class="stat-box">
            <div class="stat-label">AOD Device (ESP8266)</div>
            <div id="aodStatus" class="stat-value">Checking...</div>
          </div>
          <div class="stat-box">
            <div class="stat-label">Last Ping Seen</div>
            <div id="lastSeen" class="stat-value">--:--:--</div>
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
          <button id="triggerBtn" class="btn btn-primary" onclick="triggerWOL(false)">
            <span>🚀</span> Start Server (WOL)
          </button>
          <button id="forceTriggerBtn" class="btn btn-secondary" onclick="triggerWOL(true)">
            Force Send WOL Packet
          </button>
        </div>

        <div id="alertBox" class="notification"></div>

        <div class="security-footer">
          <span id="securityBadgeText">🔒 Authenticated Session Active</span>
        </div>
      </div>

    </div>
  </div>

  <script>
    const HAS_CF_ACCESS = ${hasCfAccess ? 'true' : 'false'};

    const authView = document.getElementById('authView');
    const dashboardView = document.getElementById('dashboardView');
    const tokenInput = document.getElementById('tokenInput');
    const authAlert = document.getElementById('authAlert');
    const unlockBtn = document.getElementById('unlockBtn');
    const lockBtn = document.getElementById('lockBtn');

    const statusBadge = document.getElementById('statusBadge');
    const badgeText = document.getElementById('badgeText');
    const aodStatus = document.getElementById('aodStatus');
    const lastSeen = document.getElementById('lastSeen');
    const alertBox = document.getElementById('alertBox');
    const triggerBtn = document.getElementById('triggerBtn');
    const autoOnToggle = document.getElementById('autoOnToggle');
    const securityBadgeText = document.getElementById('securityBadgeText');

    let pollInterval = null;

    function getSavedToken() {
      return localStorage.getItem('wol_token') || '';
    }

    function saveToken(token) {
      if (token) {
        localStorage.setItem('wol_token', token.trim());
      }
    }

    function clearToken() {
      localStorage.removeItem('wol_token');
    }

    function showAuthView(errorMsg = '') {
      if (pollInterval) {
        clearInterval(pollInterval);
        pollInterval = null;
      }
      dashboardView.classList.remove('view-active');
      authView.classList.add('view-active');
      if (errorMsg) {
        authAlert.textContent = errorMsg;
        authAlert.className = 'notification error';
        authAlert.style.display = 'block';
      } else {
        authAlert.style.display = 'none';
      }
      tokenInput.focus();
    }

    function showDashboardView() {
      authView.classList.remove('view-active');
      dashboardView.classList.add('view-active');
      if (HAS_CF_ACCESS) {
        lockBtn.style.display = 'none';
        securityBadgeText.textContent = '🔒 Protected by Cloudflare Access';
      } else {
        lockBtn.style.display = 'flex';
        securityBadgeText.textContent = '🔒 Authenticated with Secret Token';
      }
      fetchStatus();
      if (!pollInterval) {
        pollInterval = setInterval(fetchStatus, 4000);
      }
    }

    function showAlert(msg, type = 'info') {
      alertBox.textContent = msg;
      alertBox.className = 'notification ' + type;
      alertBox.style.display = 'block';
    }

    // Authenticated fetch wrapper
    async function authFetch(endpoint, options = {}) {
      options.headers = options.headers || {};
      const token = getSavedToken();
      if (token && !options.headers['Authorization']) {
        options.headers['Authorization'] = 'Bearer ' + token;
      }
      return fetch(endpoint, options);
    }

    async function handleLogin(e) {
      e.preventDefault();
      const enteredToken = tokenInput.value.trim();
      if (!enteredToken) return;

      unlockBtn.disabled = true;
      unlockBtn.innerHTML = '<span>⏳</span> Verifying...';

      try {
        const res = await fetch('/auth/verify', {
          method: 'POST',
          headers: {
            'Authorization': 'Bearer ' + enteredToken,
            'Content-Type': 'application/json'
          }
        });

        if (res.ok) {
          saveToken(enteredToken);
          tokenInput.value = '';
          showDashboardView();
        } else {
          authAlert.textContent = 'Invalid access token. Please check your credentials.';
          authAlert.className = 'notification error';
          authAlert.style.display = 'block';
        }
      } catch (err) {
        authAlert.textContent = 'Connection error. Could not verify credentials.';
        authAlert.className = 'notification error';
        authAlert.style.display = 'block';
      } finally {
        unlockBtn.disabled = false;
        unlockBtn.innerHTML = '<span>🔑</span> Unlock Server Controller';
      }
    }

    function handleLogout() {
      clearToken();
      showAuthView('Logged out. Please enter token to reconnect.');
    }

    async function toggleAutoOn(enabled) {
      try {
        const res = await authFetch('/auto-on', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ auto_turn_on: enabled })
        });

        if (res.status === 401 || res.status === 403) {
          showAuthView('Session expired or unauthorized. Please re-enter your token.');
          return;
        }

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
        const res = await authFetch('/status');
        if (res.status === 401 || res.status === 403) {
          showAuthView('Authentication required. Enter your token to continue.');
          return;
        }

        if (!res.ok) {
          badgeText.textContent = "OFFLINE";
          statusBadge.className = "badge badge-offline";
          return;
        }

        const data = await res.json();

        // Update Auto Turn On switch state
        if (typeof data.auto_turn_on !== 'undefined' && document.activeElement !== autoOnToggle) {
          autoOnToggle.checked = data.auto_turn_on;
        }

        // Update Server Status Badge
        statusBadge.className = 'badge';
        const serverStatus = (data.server_status || '').toUpperCase();

        if (serverStatus === 'ONLINE') {
          statusBadge.classList.add('badge-online');
          badgeText.textContent = 'ONLINE';
          triggerBtn.disabled = true;
          triggerBtn.innerHTML = '<span>✅</span> Server is Running';
        } else if (serverStatus === 'BOOTING' || data.trigger) {
          statusBadge.classList.add('badge-booting');
          badgeText.textContent = 'BOOTING...';
          triggerBtn.disabled = true;
          triggerBtn.innerHTML = '<span>⏳</span> Booting in Progress...';
        } else {
          statusBadge.classList.add('badge-offline');
          badgeText.textContent = 'OFFLINE';
          triggerBtn.disabled = false;
          triggerBtn.innerHTML = '<span>🚀</span> Start Server (WOL)';
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
        console.error('fetchStatus error:', err);
      }
    }

    async function triggerWOL(force = false) {
      showAlert('Sending trigger request...', 'info');

      try {
        const res = await authFetch('/trigger', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ force })
        });

        if (res.status === 401 || res.status === 403) {
          showAuthView('Session expired or unauthorized. Please re-enter your token.');
          return;
        }

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

    // Auto-login initialization flow
    (function init() {
      // Check URL query parameters for ?token=...
      const urlParams = new URLSearchParams(window.location.search);
      const queryToken = urlParams.get('token');

      if (queryToken) {
        saveToken(queryToken);
        // Clean URL to avoid leaking token in history/bookmarks
        window.history.replaceState({}, document.title, window.location.pathname);
      }

      if (HAS_CF_ACCESS || getSavedToken()) {
        showDashboardView();
      } else {
        showAuthView();
      }
    })();
  </script>
</body>
</html>`;
}

async function getAppState(env) {
  try {
    const raw = await env.WOL_STORE.get("wol_state");
    if (raw) {
      return JSON.parse(raw);
    }
  } catch (err) {
    console.error("Error reading wol_state from KV:", err);
  }

  // Backward compatibility: Fallback to individual legacy keys if wol_state is not set
  try {
    const [pending, autoOn, targetMac, serverStatus, lastSeen, lastWolSent, lastPingEpoch] = await Promise.all([
      env.WOL_STORE.get("trigger_wol"),
      env.WOL_STORE.get("auto_turn_on"),
      env.WOL_STORE.get("target_mac"),
      env.WOL_STORE.get("server_status"),
      env.WOL_STORE.get("server_last_seen"),
      env.WOL_STORE.get("last_wol_sent"),
      env.WOL_STORE.get("aod_last_ping_epoch")
    ]);
    return {
      trigger: pending === "true" || pending === "1",
      auto_turn_on: autoOn === "true",
      target_mac: targetMac || "",
      server_status: (serverStatus || "UNKNOWN").toUpperCase(),
      server_last_seen: lastSeen || null,
      last_wol_sent: lastWolSent || null,
      aod_last_ping_epoch: Number(lastPingEpoch) || 0
    };
  } catch (err) {
    console.error("Error reading legacy keys from KV:", err);
    return {
      trigger: false,
      auto_turn_on: false,
      target_mac: "",
      server_status: "UNKNOWN",
      server_last_seen: null,
      last_wol_sent: null,
      aod_last_ping_epoch: 0
    };
  }
}

async function saveAppState(env, state) {
  try {
    await env.WOL_STORE.put("wol_state", JSON.stringify(state));
    return true;
  } catch (err) {
    console.error("KV put failed (rate limit / quota reached):", err);
    return false;
  }
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const authHeader = request.headers.get("Authorization");
    const tokenQuery = url.searchParams.get("token");

    // Cloudflare Access headers present when user passes Cloudflare Access login
    const cfAccessEmail = request.headers.get("cf-access-authenticated-user-email");
    const cfAccessJwt   = request.headers.get("cf-access-jwt-assertion");

    // Device and User token configuration
    const aodSecret  = env.AOD_DEVICE_KEY || env.API_KEY || API_KEY;
    const userSecret = env.USER_API_KEY || env.API_KEY || API_KEY;

    // Extract Bearer token if provided via Authorization header, URL query, or Cookie
    let clientToken = "";
    if (authHeader && authHeader.startsWith("Bearer ")) {
      clientToken = authHeader.substring(7).trim();
    } else if (tokenQuery) {
      clientToken = tokenQuery.trim();
    } else {
      const cookieHeader = request.headers.get("Cookie") || "";
      const match = cookieHeader.match(/(?:^|;\s*)auth_token=([^;]+)/);
      if (match) {
        clientToken = decodeURIComponent(match[1]).trim();
      }
    }

    // Role detection:
    // Authorized user: Valid Cloudflare Access session OR matching User/Device/API token
    const isUser = Boolean(
      cfAccessEmail || 
      cfAccessJwt || 
      (clientToken && (clientToken === userSecret || clientToken === aodSecret))
    );

    // Authorized AOD device: Matching AOD_DEVICE_KEY, USER_API_KEY, or API_KEY
    const isAOD = Boolean(
      clientToken && (clientToken === aodSecret || clientToken === userSecret)
    );

    // Web Dashboard UI at GET /
    if (request.method === "GET" && (url.pathname === "/" || url.pathname === "/index.html")) {
      const hasCfAccess = Boolean(cfAccessEmail || cfAccessJwt);
      return new Response(renderDashboardHTML(hasCfAccess), {
        status: 200,
        headers: { 
          "Content-Type": "text/html; charset=utf-8",
          "Cache-Control": "no-cache, no-store, must-revalidate"
        }
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

    // Route: POST /auth/verify - Verify user token or session
    if (request.method === "POST" && url.pathname === "/auth/verify") {
      if (isUser) {
        return new Response(
          JSON.stringify({ success: true, role: "USER", email: cfAccessEmail || null }),
          { status: 200, headers: { "Content-Type": "application/json" } }
        );
      }
      return unauthorized("Invalid access token");
    }

    // Route: POST /auto-on - Allowed for authorized users
    if (request.method === "POST" && url.pathname === "/auto-on") {
      if (!isUser && !isAOD) return unauthorized();
      if (!isUser) return forbidden("Only User can configure auto turn on");

      let body = {};
      try { body = await request.json(); } catch(e) {}
      const enable = body.auto_turn_on === true;
      
      const state = await getAppState(env);
      state.auto_turn_on = enable;
      await saveAppState(env, state);

      return new Response(
        JSON.stringify({
          success: true,
          auto_turn_on: enable,
          message: enable ? "Automatic Turn On ENABLED (Server will auto-boot when offline)" : "Automatic Turn On DISABLED"
        }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      );
    }

    // Route: GET /status - Read state (Accessible by authenticated User or AOD Device)
    if (request.method === "GET" && url.pathname === "/status") {
      if (!isUser && !isAOD) return unauthorized();

      const state = await getAppState(env);
      let shouldTrigger = Boolean(state.trigger);

      // AUTOMATIC TURN ON LOGIC: only write when transitioning to BOOTING
      if (state.auto_turn_on && (state.server_status || "").toUpperCase() === "OFFLINE" && !shouldTrigger) {
        shouldTrigger = true;
        state.trigger = true;
        state.server_status = "BOOTING";
        await saveAppState(env, state);
      }

      // Microcontroller is alive if it checked in within the last 20 minutes
      const aodAlive = (Date.now() - Number(state.aod_last_ping_epoch || 0)) < (20 * 60 * 1000);

      return new Response(
        JSON.stringify({
          trigger: shouldTrigger,
          auto_turn_on: Boolean(state.auto_turn_on),
          mac: state.target_mac || "",
          server_status: state.server_status || "UNKNOWN",
          server_last_seen: state.server_last_seen || null,
          last_wol_sent: state.last_wol_sent || null,
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

    // Route: POST /trigger - Allowed for authorized users
    if (request.method === "POST" && url.pathname === "/trigger") {
      if (!isUser && !isAOD) return unauthorized();
      if (!isUser) return forbidden("Only User can trigger Wake-on-LAN");

      let reqData = {};
      try { reqData = await request.json(); } catch (e) {}

      const force = reqData.force === true;
      const state = await getAppState(env);
      const currentStatus = (state.server_status || "").toUpperCase();

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
      state.trigger = true;
      state.server_status = "BOOTING";
      if (mac) {
        state.target_mac = mac;
      }
      await saveAppState(env, state);

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

    // Route: POST /ack - Microcontroller acknowledges WOL dispatch
    if (request.method === "POST" && url.pathname === "/ack") {
      if (!isUser && !isAOD) return unauthorized();

      let body = {};
      try { body = await request.json(); } catch(e) {}

      const result = body.result || "SENT";
      const state = await getAppState(env);
      state.trigger = false;
      state.last_wol_sent = new Date().toISOString();

      if (result === "FAILED_TO_SEND") {
        state.server_status = "FAILED_TO_TRIGGER";
      }
      await saveAppState(env, state);

      return new Response(
        JSON.stringify({ success: true, message: "Trigger cleared", wol_result: result }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      );
    }

    // Route: POST /server/ping - Microcontroller reports reachability
    if (request.method === "POST" && url.pathname === "/server/ping") {
      if (!isUser && !isAOD) return unauthorized();

      let body = {};
      try { body = await request.json(); } catch(e) {}

      const incomingStatus = (body.status || "ONLINE").toUpperCase();
      const now = Date.now();
      const state = await getAppState(env);

      // KV optimization: only write to KV if state changed OR periodic keepalive elapsed (15 min)
      const currentStatus = (state.server_status || "").toUpperCase();
      const statusChanged = currentStatus !== incomingStatus;
      const keepaliveDue = (now - Number(state.aod_last_ping_epoch || 0)) >= (15 * 60 * 1000);

      if (statusChanged || keepaliveDue) {
        state.server_status = incomingStatus;
        if (incomingStatus === "ONLINE") {
          state.server_last_seen = new Date(now).toISOString();
        }
        state.aod_last_ping_epoch = now;
        await saveAppState(env, state);
      }

      return new Response(
        JSON.stringify({ 
          success: true, 
          server_status: incomingStatus,
          kv_write_occurred: statusChanged || keepaliveDue
        }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      );
    }

    return new Response(
      JSON.stringify({
        endpoints: {
          "GET /": "Web Dashboard UI (Secured by Token or Cloudflare Access)",
          "POST /auth/verify": "Verify authentication token",
          "GET /status": "Poll trigger & server state",
          "POST /auto-on": "Configure Automatic Turn On",
          "POST /trigger": "Arm WOL trigger",
          "POST /ack": "Disarm trigger after WOL packet is sent (Device)",
          "POST /server/ping": "Report server reachability (Device)"
        }
      }),
      { status: 200, headers: { "Content-Type": "application/json" } }
    );
  }
};

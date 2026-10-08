/**
 * Cloudflare Worker for Wake-on-LAN Trigger
 * Free Tier KV usage:
 * - Reads: Up to 100,000 / day (polling every 1-2 mins easily fits within free tier)
 * - Writes: Up to 1,000 / day
 */

const API_KEY = "CHANGE_ME_SECRET_TOKEN"; // Set as a secret via wrangler secret put API_KEY

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const authHeader = request.headers.get("Authorization");
    const tokenQuery = url.searchParams.get("token");
    const secret = env.API_KEY || API_KEY;

    // Basic Token Authentication
    const isAuthorized = authHeader === `Bearer ${secret}` || tokenQuery === secret;

    if (!isAuthorized) {
      return new Response(JSON.stringify({ error: "Unauthorized" }), {
        status: 401,
        headers: { "Content-Type": "application/json" }
      });
    }

    // Route: GET /status - Polled by Microcontroller or User
    if (request.method === "GET" && url.pathname === "/status") {
      const pending = await env.WOL_STORE.get("trigger_wol");
      const targetMac = await env.WOL_STORE.get("target_mac") || "";
      const serverStatus = await env.WOL_STORE.get("server_status") || "UNKNOWN";
      const lastSeen = await env.WOL_STORE.get("server_last_seen") || null;
      const lastWolSent = await env.WOL_STORE.get("last_wol_sent") || null;
      const mcuHeartbeat = await env.WOL_STORE.get("mcu_last_heartbeat") || null;

      return new Response(
        JSON.stringify({
          trigger: pending === "true" || pending === "1",
          mac: targetMac,
          server_status: serverStatus,      // "ONLINE", "OFFLINE", "BOOTING", "FAILED_TO_WAKE"
          server_last_seen: lastSeen,
          last_wol_sent: lastWolSent,
          mcu_online: mcuHeartbeat ? (Date.now() - Number(mcuHeartbeat) < 120000) : false
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

    // Route: POST /trigger - Trigger Wake-on-LAN (checks if already online)
    if (request.method === "POST" && url.pathname === "/trigger") {
      let reqData = {};
      try {
        reqData = await request.json();
      } catch (e) {}

      const force = reqData.force === true;
      const currentStatus = await env.WOL_STORE.get("server_status");

      // Check if server is already reported as ONLINE
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
      await env.WOL_STORE.put("wol_trigger_time", String(Date.now()));
      if (mac) {
        await env.WOL_STORE.put("target_mac", mac);
      }

      return new Response(
        JSON.stringify({
          success: true,
          status: "ARMED",
          message: "Wake-on-LAN armed. AOD Microcontroller will dispatch packet shortly.",
          target_mac: mac
        }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      );
    }

    // Route: POST /ack - Microcontroller acknowledges packet broadcast or reports error
    if (request.method === "POST" && url.pathname === "/ack") {
      let body = {};
      try { body = await request.json(); } catch(e) {}

      const result = body.result || "SENT"; // "SENT" or "FAILED_TO_SEND"
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

    // Route: POST /mcu/heartbeat - Keep track if microcontroller is connected & polling
    if (request.method === "POST" && url.pathname === "/mcu/heartbeat") {
      await env.WOL_STORE.put("mcu_last_heartbeat", String(Date.now()));
      return new Response(JSON.stringify({ ok: true }), {
        status: 200,
        headers: { "Content-Type": "application/json" }
      });
    }

    // Route: POST /server/ping - Microcontroller (ping/port probe) or Server pinging Cloudflare
    if (request.method === "POST" && url.pathname === "/server/ping") {
      let body = {};
      try { body = await request.json(); } catch(e) {}

      const status = body.status || "ONLINE"; // "ONLINE" or "OFFLINE"
      await env.WOL_STORE.put("server_status", status);
      await env.WOL_STORE.put("server_last_seen", new Date().toISOString());

      return new Response(
        JSON.stringify({ success: true, server_status: status }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      );
    }

    return new Response(
      JSON.stringify({
        endpoints: {
          "GET /status": "Poll trigger state",
          "POST /trigger": "Arm WOL trigger (optional JSON: { mac: 'AA:BB:CC:DD:EE:FF' })",
          "POST /ack": "Disarm trigger after WOL packet is sent"
        }
      }),
      {
        status: 200,
        headers: { "Content-Type": "application/json" }
      }
    );
  }
};

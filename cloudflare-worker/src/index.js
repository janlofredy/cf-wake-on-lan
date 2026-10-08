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

    // Route: GET /status - Polled by ESP32 / Arduino Uno Shield
    if (request.method === "GET" && url.pathname === "/status") {
      const pending = await env.WOL_STORE.get("trigger_wol");
      const targetMac = await env.WOL_STORE.get("target_mac") || "";

      return new Response(
        JSON.stringify({
          trigger: pending === "true" || pending === "1",
          mac: targetMac
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

    // Route: POST /trigger - Trigger Wake-on-LAN
    if (request.method === "POST" && url.pathname === "/trigger") {
      let reqData = {};
      try {
        reqData = await request.json();
      } catch (e) {}

      const mac = reqData.mac || "";

      await env.WOL_STORE.put("trigger_wol", "true");
      if (mac) {
        await env.WOL_STORE.put("target_mac", mac);
      }

      return new Response(
        JSON.stringify({
          success: true,
          message: "Wake-on-LAN trigger armed. Microcontroller will pick it up on next poll.",
          target_mac: mac
        }),
        {
          status: 200,
          headers: { "Content-Type": "application/json" }
        }
      );
    }

    // Route: POST /ack - Microcontroller acknowledges it fired WOL
    if (request.method === "POST" && url.pathname === "/ack") {
      await env.WOL_STORE.put("trigger_wol", "false");
      return new Response(
        JSON.stringify({ success: true, message: "Trigger cleared" }),
        {
          status: 200,
          headers: { "Content-Type": "application/json" }
        }
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

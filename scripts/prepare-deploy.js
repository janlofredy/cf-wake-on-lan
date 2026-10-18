/**
 * Cloudflare Worker Pre-Deploy Script
 *
 * Dynamically injects the KV namespace ID into wrangler.toml from the environment
 * variable (WOL_KV_ID or KV_NAMESPACE_ID) during Cloudflare Workers Builds.
 *
 * This keeps the public git repository clean (no personal IDs committed) while
 * preventing Wrangler from wiping out the KV binding on deployment.
 */

const fs = require('fs');
const path = require('path');

const targetFiles = [
  path.resolve(__dirname, '..', 'wrangler.toml'),
  path.resolve(__dirname, '..', 'cloudflare-worker', 'wrangler.toml')
];

const kvId = (
  process.env.WOL_KV_ID ||
  process.env.KV_NAMESPACE_ID ||
  process.env.WOL_STORE_ID ||
  process.env.CLOUDFLARE_KV_ID ||
  ''
).trim();

if (!kvId) {
  console.log('[prepare-deploy] No WOL_KV_ID / KV_NAMESPACE_ID environment variable detected.');
  console.log('[prepare-deploy] Keeping generic wrangler.toml configuration.');
  process.exit(0);
}

console.log(`[prepare-deploy] Found KV Namespace ID in environment (${kvId.substring(0, 4)}...${kvId.substring(kvId.length - 4)}).`);

for (const tomlPath of targetFiles) {
  if (!fs.existsSync(tomlPath)) continue;

  let content = fs.readFileSync(tomlPath, 'utf8');
  let updated = false;

  const kvBlockRegex = /^[ \t]*#?[ \t]*\[\[kv_namespaces\]\][\s\S]*?^[ \t]*#?[ \t]*id\s*=\s*["'][^"']*["']/m;

  const replacementBlock = `[[kv_namespaces]]\nbinding = "WOL_STORE"\nid = "${kvId}"`;

  if (kvBlockRegex.test(content)) {
    content = content.replace(kvBlockRegex, replacementBlock);
    updated = true;
  } else {
    // Append to file if not found
    content += `\n\n# Dynamically injected by prepare-deploy.js\n${replacementBlock}\n`;
    updated = true;
  }

  if (updated) {
    fs.writeFileSync(tomlPath, content, 'utf8');
    console.log(`[prepare-deploy] Successfully injected WOL_STORE KV binding into: ${path.relative(process.cwd(), tomlPath)}`);
  }
}

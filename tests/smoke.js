// Post-install smoke test: checks the plugin is loaded and configured sanely.
//
//   $env:SB_WS_PASSWORD = '<obs-websocket password>'
//   node tests/smoke.js
//
// Requires OBS running with obs-websocket enabled. The password is read from
// the environment so it is never committed. Exits non-zero on failure.

const crypto = require('crypto');

const PORT = process.env.SB_WS_PORT || 4455;
const PASSWORD = process.env.SB_WS_PASSWORD || '';
const b64 = (b) => b.toString('base64');
const sha256 = (s) => crypto.createHash('sha256').update(s, 'utf8').digest();
const authString = (p, salt, ch) => b64(sha256(b64(sha256(p + salt)) + ch));

let id = 0;
const pending = new Map();
let failed = 0;

function check(ok, what) {
  console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`);
  if (!ok) failed++;
}

const ws = new WebSocket(`ws://127.0.0.1:${PORT}`);
function send(t, d) {
  const requestId = String(++id);
  return new Promise((res, rej) => {
    pending.set(requestId, res);
    const p = { requestType: t, requestId };
    if (d !== undefined) p.requestData = d;
    ws.send(JSON.stringify({ op: 6, d: p }));
    setTimeout(() => rej(new Error(`${t} timed out`)), 8000);
  });
}

ws.addEventListener('error', () => {
  console.log('Could not reach obs-websocket. Is OBS running with the websocket server enabled?');
  process.exit(1);
});

ws.addEventListener('message', (ev) => {
  const m = JSON.parse(ev.data);
  if (m.op === 0) {
    const a = m.d.authentication;
    const payload = { rpcVersion: 1 };
    if (a) {
      if (!PASSWORD) {
        console.log('obs-websocket requires a password; set SB_WS_PASSWORD.');
        process.exit(1);
      }
      payload.authentication = authString(PASSWORD, a.salt, a.challenge);
    }
    ws.send(JSON.stringify({ op: 1, d: payload }));
    return;
  }
  if (m.op === 2) { main().catch((e) => { console.log('ERR', e.message); process.exit(1); }); return; }
  if (m.op === 7) { const p = pending.get(m.d.requestId); if (p) { pending.delete(m.d.requestId); p(m.d); } }
});

async function main() {
  console.log('SpaceBeam smoke test');

  const inputs = await send('GetInputList');
  const scenes = await send('GetSceneList');

  // Find any source carrying a SpaceBeam filter.
  let found = null;
  for (const sc of scenes.responseData.scenes) {
    const items = await send('GetSceneItemList', { sceneName: sc.sceneName });
    for (const it of items.responseData.sceneItems || []) {
      const fl = await send('GetSourceFilterList', { sourceName: it.sourceName });
      for (const f of fl.responseData?.filters || []) {
        if (f.filterKind === 'spacebeam_filter') { found = { source: it.sourceName, name: f.filterName }; break; }
      }
      if (found) break;
    }
    if (found) break;
  }
  check(!!found, 'a SpaceBeam filter exists in the scene collection');
  if (!found) { finish(); return; }

  const filter = await send('GetSourceFilter', { sourceName: found.source, filterName: found.name });
  const s = filter.responseData.filterSettings;

  check(filter.responseData.filterEnabled === true, 'the filter is enabled (not bypassed)');
  check(typeof s.k_amt === 'number' || s.k_amt === undefined, 'kaleidoscope amount readable');
  check(s.preset_time !== undefined ? s.preset_time >= 0 : true, 'transition time is valid');

  const slots = [1, 2, 3, 4, 5, 6, 7, 8, 9].filter((n) => {
    const v = s['preset_' + n];
    return typeof v === 'string' && v.length > 50;
  });
  check(slots.length > 0, `at least one preset slot is saved (found ${slots.length})`);

  if (s.audio_on) {
    check(!!s.audio_src, 'music reaction has an audio source');
    const routed = [1, 2, 3, 4].some((n) => s['audio' + n + '_on']);
    check(routed, 'music reaction has at least one routing');
  } else {
    console.log('  --   music reaction is off (not an error)');
  }

  finish();
}

function finish() {
  console.log(failed === 0 ? '\nPASS' : `\n${failed} check(s) failed`);
  process.exit(failed === 0 ? 0 : 1);
}

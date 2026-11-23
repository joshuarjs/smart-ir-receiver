// Minify webui.html / hotspot.html for the firmware: terser on the inline script, whitespace
// collapse on CSS and markup. Used by make_webui.py when node is available; the pages
// work unminified too, they just gzip a little larger.
const fs = require('fs'); const path = require('path');
let terser; try { terser = require('terser'); } catch (e) { terser = require(path.join(__dirname, '..', '..', '..', 'ptool', 'node_modules', 'terser')); }
(async () => {
  const src = fs.readFileSync(process.argv[2], 'utf8');
  let out = src;
  const scripts = [...src.matchAll(/<script>([\s\S]*?)<\/script>/g)];
  for (const m of scripts) {
    const r = await terser.minify(m[1], { compress: { passes: 2 }, mangle: true, format: { ascii_only: false } });
    if (r.error) throw r.error;
    out = out.replace(m[0], '<script>' + r.code + '</script>');
  }
  out = out.replace(/<style>([\s\S]*?)<\/style>/g, (_, css) => '<style>' + css.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\s*([{};:,>])\s*/g, '$1').replace(/;}/g, '}').replace(/\s+/g, ' ').trim() + '</style>');
  out = out.replace(/>\s+</g, '><').replace(/\n\s*/g, '\n').replace(/\n+/g, '\n');
  process.stdout.write(out);
})().catch(e => { console.error(e); process.exit(1); });

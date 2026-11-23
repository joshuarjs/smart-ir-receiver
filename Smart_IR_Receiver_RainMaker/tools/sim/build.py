#!/usr/bin/env python3
"""Build the browser preview: the real console page (minified) + sim.js, which replaces /api with a
JS copy of the firmware logic and adds the amber simulator bar. Output: /mnt/user-data/outputs/preview/index.html"""
import os, subprocess, re
here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, '..', '..')
html = subprocess.run(['node', os.path.join(src, 'tools', 'minify.js'), os.path.join(src, 'webui.html')], capture_output=True, text=True, check=True).stdout
ver = re.search(r'FW_VERSION\s+"([^"]+)"', open(os.path.join(src, 'config.h')).read()).group(1)
sim = open(os.path.join(here, 'sim.js')).read().replace('__VER__', ver)
css = open(os.path.join(here, 'simbar_css.html')).read()
bar = open(os.path.join(here, 'simbar.html')).read().replace('__VER__', ver)
assert '</style></head><body>' in html, 'injection marker missing'
html = html.replace('</style></head><body>', '</style>' + css + '<script>' + sim + '</script></head><body>\n' + bar, 1)
html = html.replace('<title>Smart IR Receiver</title>', '<title>Smart IR Receiver v' + ver + ' preview</title>', 1)
out = '/mnt/user-data/outputs/preview/index.html'
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, 'w').write(html)
print(out, len(html))

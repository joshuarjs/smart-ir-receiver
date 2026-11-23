#!/bin/bash
# Recreate the Linux build environment used for releases (no Arduino IDE needed):
# arduino-cli + arduino-esp32 3.3.11 for the C3 and C6, laid out by hand because this sandbox
# can't reach downloads.arduino.cc. Then: tools/build.sh
set -e
A=/home/claude/ard; D=$A/data; mkdir -p $D $A/downloads $A/user /home/claude/tools
cat > $A/cli.yaml <<Y
directories: {data: $D, downloads: $A/downloads, user: $A/user}
board_manager: {additional_urls: [https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json]}
Y
cd /home/claude/tools && [ -x arduino-cli ] || curl -sL https://github.com/arduino/arduino-cli/releases/download/v1.3.1/arduino-cli_1.3.1_Linux_64bit.tar.gz | tar xz arduino-cli
export ARDUINO_CONFIG_FILE=$A/cli.yaml
./arduino-cli core update-index >/dev/null 2>&1 || true
echo '{"packages":[]}' > $D/package_index.json; echo '{"libraries":[]}' > $D/library_index.json
python3 - <<'PY'
import json,subprocess,os,tarfile,zipfile,shutil
D='/home/claude/ard/data'; d=json.load(open(D+'/package_esp32_index.json')); p=[x for x in d['packages'] if x['name']=='esp32'][0]
pl=[x for x in p['platforms'] if x['version']=='3.3.11' and x['architecture']=='esp32'][0]
want={t['name']:t['version'] for t in pl['toolsDependencies'] if t['packager']=='esp32' and t['name'] in ('esp-rv32','esp32c3-libs','esp32c6-libs','esptool_py','mklittlefs','mkspiffs')}
urls=[('core',pl['url'])]+[(t['name']+'|'+t['version'],s['url']) for t in p['tools'] if t['name'] in want and t['version']==want[t['name']] for s in t['systems'] if s['host']=='x86_64-pc-linux-gnu']
P=D+'/packages/esp32'; os.makedirs(P+'/hardware/esp32',exist_ok=True); os.makedirs(P+'/tools',exist_ok=True)
for name,url in urls:
    dest=P+'/hardware/esp32/3.3.11' if name=='core' else P+'/tools/'+name.split('|')[0]+'/'+name.split('|')[1]
    if os.path.isdir(dest): continue
    f='/home/claude/ard/downloads/'+url.split('/')[-1]
    if not os.path.exists(f): subprocess.run(['curl','-sL','--retry','3','-o',f,url],check=True)
    tmp='/tmp/x_'+name.replace('|','_'); shutil.rmtree(tmp,ignore_errors=True); os.makedirs(tmp)
    (zipfile.ZipFile(f).extractall(tmp) if f.endswith('.zip') else tarfile.open(f).extractall(tmp))
    inner=os.listdir(tmp); root=os.path.join(tmp,inner[0]) if len(inner)==1 and os.path.isdir(os.path.join(tmp,inner[0])) else tmp
    shutil.move(root,dest); print('installed',name)
PY
# tools the core lists but we can't download: stub dfu-util, use the system ctags
mkdir -p $D/packages/arduino/tools/dfu-util/0.11.0-arduino5 && printf '#!/bin/sh\n' > $D/packages/arduino/tools/dfu-util/0.11.0-arduino5/dfu-util && chmod +x $D/packages/arduino/tools/dfu-util/0.11.0-arduino5/dfu-util
which ctags >/dev/null || (apt-get install -y -qq exuberant-ctags >/dev/null 2>&1 || (apt-get update -qq >/dev/null && apt-get install -y -qq exuberant-ctags >/dev/null))
mkdir -p $D/packages/builtin/tools/ctags/5.8-arduino11 && ln -sf "$(which ctags)" $D/packages/builtin/tools/ctags/5.8-arduino11/ctags
# page tools for make_webui.py / the preview
mkdir -p /home/claude/ptool && cd /home/claude/ptool && [ -d node_modules/terser ] || (npm init -y >/dev/null 2>&1 && npm install prettier@3 terser@5 --silent)
echo "dev environment ready"

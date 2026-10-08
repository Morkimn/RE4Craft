"""Build/test the RE4 placement patch against a redistributable SkyCraft mod JAR.

Minecraft/Fabric classes are neither copied nor distributed. The original JAR
is read-only. Use Java 25 (JAVA_HOME or --java-home).
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--original', type=Path, default=root/'tools/PeakCraft/fabric/build/libs/skycraft-0.1.2.jar')
parser.add_argument('--java-home', type=Path, default=Path(os.environ['JAVA_HOME']) if 'JAVA_HOME' in os.environ else None)
args = parser.parse_args()
if args.java_home is None:
    parser.error('Set JAVA_HOME to Java 25 or pass --java-home.')
java = args.java_home/'bin'
original = args.original.resolve()
classes = root/'build/guest-patch-classes'
classes.mkdir(parents=True, exist_ok=True)
source = root/'src/guest/SkyRay.java'
subprocess.run([str(java/'javac.exe'), '-cp', str(original), '-d', str(classes), str(source), str(root/'tests/GroundPlacement.java')], check=True)
subprocess.run([str(java/'java.exe'), '-cp', str(classes)+os.pathsep+str(original), 'GroundPlacement'], check=True)
output = root/'build/guest/skycraft-0.1.2-re4.1.jar'
output.parent.mkdir(parents=True, exist_ok=True)
replacement = {str(p.relative_to(classes)).replace('\\', '/'): p.read_bytes() for p in (classes/'dev/skycraft/world').glob('SkyRay*.class')}
assert len(replacement) == 2
with zipfile.ZipFile(original) as src, zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as dst:
    assert not any(n.startswith(('net/minecraft/', 'com/mojang/', 'assets/minecraft/')) for n in src.namelist())
    for item in src.infolist():
        data = replacement.pop(item.filename, None)
        if data is None:
            data = src.read(item.filename)
        if item.filename == 'fabric.mod.json':
            metadata = json.loads(data)
            metadata['version'] = '0.1.2-re4.1'
            metadata['name'] = 'SkyCraft (RE4Craft adapter)'
            metadata['description'] = 'RE4Craft guest: Minecraft logic bridged into Resident Evil 4.'
            data = json.dumps(metadata, indent=2).encode()
        dst.writestr(item, data)
    assert not replacement, replacement.keys()
    dst.writestr('LICENSE-RE4Craft-SkyCraft.txt', (root/'licenses/SkyCraft-MIT.txt').read_bytes())
print('Guest patch:', output, 'SHA256', hashlib.sha256(output.read_bytes()).hexdigest())

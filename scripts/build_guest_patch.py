"""Build RE4 guest overrides. Minecraft dependencies stay outside the JAR.

Pass a SkyCraft 0.1.2 JAR, Java 25 home and Prism's libraries directory.
The input JAR is read-only; temporary classes/dependencies go under build/.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--original', type=Path, required=True)
parser.add_argument('--java-home', type=Path, default=Path(os.environ['JAVA_HOME']) if 'JAVA_HOME' in os.environ else None)
parser.add_argument('--libraries', type=Path, required=True)
parser.add_argument('--fabric-api', type=Path, required=True)
parser.add_argument('--output', type=Path, default=root/'build/guest/skycraft-0.1.2-re4.2.jar')
parser.add_argument('--test-mod', action='store_true', help='Also build the separate development-only integration test mod.')
args = parser.parse_args()
if args.java_home is None:
    parser.error('Set JAVA_HOME to Java 25 or pass --java-home.')
java = args.java_home/'bin'
original, output = args.original.resolve(), args.output.resolve()
if original == output:
    parser.error('Output must differ from the read-only input JAR.')
jars = sorted(args.libraries.resolve().rglob('*.jar')) + [args.fabric_api.resolve()]
if not any('minecraft-26.3-client' in p.name for p in jars):
    parser.error('The libraries directory must contain Minecraft 26.3.')
(root/'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='guest-patch-', dir=root/'build') as work:
    work = Path(work)
    classes = work/'classes'
    classes.mkdir()
    # javac cannot see Fabric API's modules inside the outer dependency JAR.
    for dependency in jars[:]:
        if dependency.name.startswith('fabric-api-'):
            with zipfile.ZipFile(dependency) as src:
                for name in src.namelist():
                    if name.startswith('META-INF/jars/') and name.endswith('.jar'):
                        target = work/Path(name).name
                        target.write_bytes(src.read(name))
                        jars.append(target)
    cp = os.pathsep.join(map(str, [original, *jars]))
    sources = sorted((root/'src/guest').glob('*.java'))
    if args.test_mod:
        sources.append(root/'tests/GuestIntegration.java')
    subprocess.run([str(java/'javac.exe'), '-proc:none', '-cp', cp, '-d', str(classes), *map(str, sources), str(root/'tests/GroundPlacement.java')], check=True)
    subprocess.run([str(java/'java.exe'), '-cp', str(classes)+os.pathsep+cp, 'GroundPlacement'], check=True)
    replacement = {p.relative_to(classes).as_posix(): p.read_bytes() for p in (classes/'dev/skycraft').rglob('*.class')}
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(original) as src, zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as dst:
        assert not any(n.startswith(('net/minecraft/', 'com/mojang/', 'assets/minecraft/')) for n in src.namelist())
        for item in src.infolist():
            if item.filename == 'LICENSE-RE4Craft-SkyCraft.txt':
                continue
            data = replacement.pop(item.filename, None)
            if data is None:
                data = src.read(item.filename)
            if item.filename == 'fabric.mod.json':
                metadata = json.loads(data)
                metadata['version'] = '0.1.2-re4.2'
                metadata['name'] = 'SkyCraft (RE4Craft adapter)'
                metadata['description'] = 'RE4Craft guest: Minecraft logic bridged into Resident Evil 4.'
                data = json.dumps(metadata, indent=2).encode()
            if item.filename == 'skycraft.mixins.json':
                mixins = json.loads(data)
                if 'WalkNodeEvaluatorMixin' not in mixins['mixins']:
                    mixins['mixins'].append('WalkNodeEvaluatorMixin')
                data = json.dumps(mixins, indent=2).encode()
            dst.writestr(item, data)
        for name, data in replacement.items():
            dst.writestr(name, data)
        dst.writestr('LICENSE-RE4Craft-SkyCraft.txt', (root/'licenses/SkyCraft-MIT.txt').read_bytes())
    if args.test_mod:
        with zipfile.ZipFile(output.parent/'re4craft-integration-test.jar', 'w', zipfile.ZIP_DEFLATED) as dst:
            for p in (classes/'dev/re4craft/test').rglob('*.class'):
                dst.write(p, p.relative_to(classes).as_posix())
            dst.writestr('fabric.mod.json', json.dumps({'schemaVersion': 1, 'id': 're4craft_test', 'version': '1.0', 'environment': 'client',
                'entrypoints': {'main': ['dev.re4craft.test.GuestIntegration']}, 'depends': {'skycraft': '*', 'fabric-lifecycle-events-v1': '*'}}))
print('Guest patch:', output, 'SHA256', hashlib.sha256(output.read_bytes()).hexdigest())

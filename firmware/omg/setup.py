#!/usr/bin/env python3
"""Fetch OpenMQTTGateway, add the camper environments and build or flash it.

    python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt
    python3 setup.py                     # clone/update OpenMQTTGateway, copy camper_env.ini
    python3 setup.py build camper-ble    # build (camper-ble = ESP32, camper-ble-c3 = ESP32-C3)
    python3 setup.py upload camper-ble   # build and flash over USB
    python3 setup.py monitor camper-ble  # serial monitor

Upstream is used unchanged: it is cloned into ./OpenMQTTGateway (not in git) at the tag OMG_REF.
"""
import os
import shutil
import subprocess
import sys

OMG_URL = 'https://github.com/1technophile/OpenMQTTGateway.git'
OMG_REF = os.environ.get('OMG_REF', 'v1.8.1')   # release tag; newer: OMG_REF=<tag> python3 setup.py

HERE = os.path.dirname(os.path.abspath(__file__))
OMG = os.path.join(HERE, 'OpenMQTTGateway')


def run(cmd, cwd=None):
    print('+', ' '.join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def fetch():
    if not os.path.isdir(os.path.join(OMG, '.git')):
        run(['git', 'clone', '--depth', '1', '--branch', OMG_REF, OMG_URL, OMG])
    else:
        run(['git', 'fetch', '--depth', '1', 'origin', 'tag', OMG_REF], cwd=OMG)
        run(['git', 'checkout', '-q', OMG_REF], cwd=OMG)
    shutil.copy(os.path.join(HERE, 'camper_env.ini'), os.path.join(OMG, 'camper_env.ini'))
    local = os.path.join(HERE, 'camper_local_env.ini')
    target = os.path.join(OMG, 'camper_local_env.ini')
    if os.path.exists(local):
        shutil.copy(local, target)
        print('camper_local_env.ini copied (WiFi/MQTT built in)')
    elif os.path.exists(target):
        os.remove(target)
    print(f'OpenMQTTGateway {OMG_REF} ready in {OMG}')


def pio(action, env):
    args = {'build': [], 'upload': ['-t', 'upload'], 'monitor': ['-t', 'monitor']}[action]
    run(['pio', 'run', '-e', env] + args, cwd=OMG)
    if action == 'build':
        print('Firmware:', os.path.join(OMG, '.pio', 'build', env, 'firmware.bin'))


def main():
    fetch()
    if len(sys.argv) >= 2:
        action = sys.argv[1]
        env = sys.argv[2] if len(sys.argv) > 2 else 'camper-ble'
        if action not in ('build', 'upload', 'monitor'):
            sys.exit('usage: setup.py [build|upload|monitor] [camper-ble|camper-ble-c3]')
        pio(action, env)


if __name__ == '__main__':
    main()

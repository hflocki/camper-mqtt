"""Shared MQTT connection helpers for sim.py, record.py and clean.py."""
import argparse
import os
import sys

try:
    import paho.mqtt.client as mqtt
except ImportError:  # pragma: no cover - only hit without the venv
    sys.exit("paho-mqtt missing: python3 -m venv .venv && . .venv/bin/activate && pip install -r tools/mqtt/requirements.txt")


def add_broker_args(ap: argparse.ArgumentParser) -> None:
    ap.add_argument("--host", default=os.environ.get("MQTT_HOST", "localhost"), help="broker (env MQTT_HOST)")
    ap.add_argument("--port", type=int, default=int(os.environ.get("MQTT_PORT", "1883")), help="port (env MQTT_PORT)")
    ap.add_argument("--user", default=os.environ.get("MQTT_USER", ""), help="user (env MQTT_USER)")
    # the password only from the environment, so it does not end up in the shell history
    ap.add_argument("--root", default="camper", help="topic root of the camper devices (default: camper)")


def connect(args, client_id: str, on_connect=None, on_message=None, will=None) -> "mqtt.Client":
    """Connect and start the network thread. will = (topic, payload) as retained last will."""
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    if on_connect:
        c.on_connect = on_connect
    if on_message:
        c.on_message = on_message
    if will:
        c.will_set(will[0], will[1], qos=1, retain=True)
    if args.user:
        c.username_pw_set(args.user, os.environ.get("MQTT_PASSWORD", ""))
    c.connect(args.host, args.port, keepalive=30)
    c.loop_start()
    return c

"""LabDrop logger: subscribes to MQTT events and appends them to labdrop_log.csv

Setup:  pip install paho-mqtt
Run:    python logger.py   (leave it running while you use the simulation)
"""
import csv, json, os
from datetime import datetime
import paho.mqtt.client as mqtt

BROKER = "broker.hivemq.com"
ROOT = "labdrop-test-67"        # must match ROOT in sketch.ino
FILE = "labdrop_log.csv"
FIELDS = ["logged_at", "device_time", "item_id", "item_name", "event", "student"]

if not os.path.exists(FILE):
    with open(FILE, "w", newline="") as f:
        csv.writer(f).writerow(FIELDS)

def on_connect(client, userdata, flags, reason_code, properties):
    print("Connected to broker, listening on", ROOT + "/events")
    client.subscribe(ROOT + "/events")

def on_message(client, userdata, msg):
    try:
        d = json.loads(msg.payload)
    except ValueError:
        return
    row = [
        datetime.now().isoformat(timespec="seconds"),
        datetime.fromtimestamp(d["ts"]).isoformat(timespec="seconds") if d.get("ts") else "",
        d["id"], d["name"],
        "CHECKOUT" if d["status"] == "borrowed" else "RETURN",
        d.get("by", "-"),
    ]
    with open(FILE, "a", newline="") as f:
        csv.writer(f).writerow(row)
    print(row)

c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
c.on_connect, c.on_message = on_connect, on_message
c.connect(BROKER, 1883)
c.loop_forever()

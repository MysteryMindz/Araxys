'''
import time

nodes = {}        # node_id -> last heartbeat
messages = []     # message logs
sos_nodes = {}    # active SOS

OFFLINE_TIMEOUT = 5  # seconds


def normalize(node_id: str) -> str:
    return node_id.strip().upper()


def update_heartbeat(data: dict):
    node_id = normalize(data["node_id"])
    data["node_id"] = node_id
    data["last_seen"] = time.time()
    nodes[node_id] = data


def get_nodes():
    now = time.time()
    result = {}

    for node_id, data in nodes.items():
        age = now - data["last_seen"]
        status = "online" if age < OFFLINE_TIMEOUT else "offline"

        result[node_id] = {
            **data,
            "status": status,
            "sos": node_id in sos_nodes   
        }

    return result

'''
'''
import time

nodes = {}
messages = []
sos_nodes = {}

#offline timeout =

def update_node(heartbeat: dict):
    # heartbeat["last_seen"] = time.time()
    # nodes[heartbeat["node_id"]] = heartbeat

    node_id = heartbeat["node_id"].strip().upper()
    heartbeat["node_id"] = node_id
    heartbeat["last_seen"] = time.time()
    nodes[node_id] = heartbeat

def get_node_status():
    now = time.time()
    result = {}

    for node_id,data in nodes.items():
        age = now - data["last_seen"]
        if age < 3:
            status = "online"
        #elif age < offline_timeout:
        #    status = "degraded"
        else:
            status = "offline"

        result[node_id] = {
            **data,
            "status": status
        }

    return result
'''
import time

nodes = {}
messages = []      # History of all messages
outbox = []        # Queue for messages waiting to be sent to Serial
sos_nodes = {}
current_host_id = "WAITING..."

def update_node(data: dict):
    node_id = data["node_id"].strip().upper()
    data["last_seen"] = time.time()
    nodes[node_id] = data
    if data.get("is_direct") == True:
        global current_host_id
        current_host_id = data["node_id"]

def get_node_status():
    now = time.time()
    result = {}
    for node_id, data in nodes.items():
        age = now - data.get("last_seen", 0)
        status = "online" if age < 5 else "offline"
        result[node_id] = {**data, "status": status}
    return result


'''
from fastapi import FastAPI
import time

from models import Heartbeat, MessageLog, SOS, StatusResponse
import store

app = FastAPI(
    title="Decentralized Mesh Observer",
    version="1.0"
)

# ================= ROOT =================
@app.get("/")
def root():
    return {
        "nodes": len(store.nodes),
        "messages": len(store.messages),
        "active_sos": len(store.sos_nodes)
    }

# ================= HEARTBEAT =================
@app.post("/heartbeat", response_model=StatusResponse)
def heartbeat(hb: Heartbeat):
    store.update_heartbeat(hb.model_dump())
    return {"status": "ok"}

@app.get("/nodes")
def nodes():
    return store.get_nodes()

# ================= MESSAGE LOG =================
@app.post("/message", response_model=StatusResponse)
def log_message(msg: MessageLog):
    store.messages.append(msg.model_dump())
    return {"status": "logged"}

@app.get("/messages")
def get_messages():
    return store.messages[-50:]

# ================= SOS =================
@app.post("/sos", response_model=StatusResponse)
def sos(sos: SOS):
    node_id = store.normalize(sos.node_id)
    if sos.active:
        store.sos_nodes[node_id] = sos.model_dump()
    else:
        store.sos_nodes.pop(node_id, None)
    return {"status": "updated"}

@app.get("/sos")
def get_sos():
    return store.sos_nodes


'''
'''
from fastapi import FastAPI
import time

from models import Heartbeat, Message, SOSOn, SOSOff
import store

app = FastAPI(
    title="Decentralized Mesh Backend",
    version="1.0"
)

# ROOT
@app.get("/")
def root():
    return {
        "status": "running",
        "nodes": len(store.nodes),
        "messages": len(store.messages),
        "active_sos": len(store.sos_nodes)
    }

# HEARTBEAT
@app.post("/heartbeat")
def heartbeat(hb: Heartbeat):
    store.update_node(hb.model_dump())
    return {"status": "ok"}

@app.get("/nodes")
def nodes():
    return store.get_node_status()

# MESSAGES
@app.post("/send-message")
def send_message(msg: Message):
    store.messages.append(msg.model_dump())
    return {"status": "queued"}

@app.get("/messages")
def messages():
    return store.messages[-50:]

# SOS
@app.post("/sos/on")
def sos_on(sos: SOSOn):
    store.sos_nodes[sos.node_id] = sos.model_dump()
    return {"status": "SOS activated"}

@app.post("/sos/off")
def sos_off(data: SOSOff):
    store.sos_nodes.pop(data.node_id, None)
    return {"status": "SOS cleared"}

@app.get("/sos/status")
def sos_status():
    return store.sos_nodes
'''
from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel
from typing import List, Optional
import store
import time

app = FastAPI()

# --- MODELS ---
class Heartbeat(BaseModel):
    type: str = "heartbeat"
    node_id: str
    battery: int
    rssi: int
    sos: bool
    is_direct: bool = False

class Message(BaseModel):
    packet_id: str
    type: str = "message"
    from_node: str
    to: str = "BROADCAST"
    payload: str
    rssi: int
    timestamp: float
    is_direct: bool = False

class SOSOn(BaseModel):
    node_id: str
    active: bool

# --- ROUTES ---

@app.get("/")
def read_root():
    return FileResponse('static.html')

# 1. Received from Hardware (via Bridge)
@app.post("/heartbeat")
def heartbeat(hb: Heartbeat):
    store.update_node(hb.model_dump())
    return {"status": "ok"}

@app.post("/receive-message")
def receive_message(msg: Message):
    store.messages.append(msg.model_dump())
    return {"status": "stored"}

# 2. Send from Frontend (to Hardware)
@app.post("/send-message")
def send_message(msg: Message):
    # Log it locally
    store.messages.append(msg.model_dump())
    # Queue it for the Bridge to pick up
    store.outbox.append(msg.model_dump())
    return {"status": "queued"}

@app.get("/outbox")
def get_outbox():
    if store.outbox:
        return store.outbox.pop(0) # Pop the oldest message
    return {} # Empty

# 3. Data for Frontend
@app.get("/nodes")
def get_nodes():
    return store.get_node_status()

@app.get("/messages")
def get_messages():
    return store.messages[-50:]

@app.get("/sos/status")
def get_sos():
    return store.sos_nodes

@app.post("/sos/on")
def sos_on(sos: SOSOn):
    store.sos_nodes[sos.node_id] = sos.dict()
    # Trigger hardware via outbox
    store.outbox.append({"payload": "SOS_ON"}) 
    return {"status": "active"}

@app.post("/sos/off")
def sos_off(data: SOSOn):
    store.sos_nodes.pop(data.node_id, None)
    store.outbox.append({"payload": "SOS_OFF"})
    return {"status": "cleared"}

# main.py
@app.get("/host-info")
def get_host_info():
    return {"host_id": store.current_host_id}
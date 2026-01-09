'''
from pydantic import BaseModel

# ================= HEARTBEAT =================
class Heartbeat(BaseModel):
    node_id: str
    rssi: int
    battery: int
    hop_count: int
    sos: bool
    timestamp: float


# ================= MESSAGE LOG =================
class MessageLog(BaseModel):
    sender: str
    message: str
    counter: int
    rssi: int
    timestamp: float


# ================= SOS =================
class SOS(BaseModel):
    node_id: str
    active: bool
    timestamp: float


# ================= GENERIC STATUS =================
class StatusResponse(BaseModel):
    status: str


'''
from pydantic import BaseModel
from typing import Optional

# HEARTBEAT

class Heartbeat(BaseModel):
    type: str = "heartbeat"
    node_id: str
    battery: int
    rssi: int
    hop_count: int
    sos: bool
    timestamp: float


# MESSAGE

class Message(BaseModel):
    packet_id: str
    type: str = "message"
    from_node: str
    to: str
    payload: str
    hop_count: int
    rssi: int
    timestamp: float



# SOS

class SOSOn(BaseModel):
    packet_id: str
    type: str = "sos"
    node_id: str
    active: bool
    battery: int
    timestamp: float


# SOS OFF

class SOSOff(BaseModel):
    node_id: str

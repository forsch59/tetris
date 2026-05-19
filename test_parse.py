import re
import ctypes
import os

with open("src/network.hpp", "r") as f:
    content = f.read()

enum_match = re.search(r'enum class PacketType\s*:\s*\w+\s*\{([^}]+)\}', content)
pkt_names = {}
if enum_match:
    for line in enum_match.group(1).split('\n'):
        line = line.strip()
        if not line or line.startswith('//'): continue
        match = re.match(r'([A-Z_]+)\s*=\s*(\d+)', line)
        if match:
            pkt_names[int(match.group(2))] = match.group(1)

print("PKT_NAMES:", pkt_names)

TYPE_MAP = {
    'uint8_t': ctypes.c_uint8,
    'int8_t': ctypes.c_int8,
    'uint16_t': ctypes.c_uint16,
    'int16_t': ctypes.c_int16,
    'uint32_t': ctypes.c_uint32,
    'int32_t': ctypes.c_int32,
}

struct_matches = re.finditer(r'struct\s+(\w+)\s*\{([^}]+)\};', content)
struct_classes = {}

for match in struct_matches:
    struct_name = match.group(1)
    body = match.group(2)
    fields = []
    for line in body.split('\n'):
        line = line.strip()
        if not line or line.startswith('//'): continue
        line = line.split('//')[0].strip()
        fmatch = re.match(r'([A-Za-z0-9_]+)\s+([A-Za-z0-9_]+)(?:\[(\d+)\])?;', line)
        if fmatch:
            ftype, fname, arr_size = fmatch.groups()
            
            ctype = TYPE_MAP.get(ftype) or struct_classes.get(ftype)
            if not ctype: continue
            
            if arr_size:
                ctype = ctype * int(arr_size)
            fields.append((fname, ctype))
    
    class DynStruct(ctypes.BigEndianStructure):
        _pack_ = 1
        _fields_ = fields
    DynStruct.__name__ = struct_name
    struct_classes[struct_name] = DynStruct

print("STRUCTS:", struct_classes.keys())
print("StateUpdatePacket fields:", struct_classes['StateUpdatePacket']._fields_)
print("CommandPacket size:", ctypes.sizeof(struct_classes['CommandPacket']))
print("StateUpdatePacket size:", ctypes.sizeof(struct_classes['StateUpdatePacket']))

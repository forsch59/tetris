import json
import os

def generate_cpp(schema, out_path):
    out = "#pragma once\n\n#include <cstdint>\n\n"
    out += "enum class PacketType : uint8_t {\n"
    for k, v in schema["packet_types"].items():
        out += f"    {k} = {v},\n"
    out += "};\n\n"
    
    out += "#pragma pack(push, 1)\n\n"
    for struct in schema["structs"]:
        out += f"struct {struct['name']} {{\n"
        for field in struct["fields"]:
            if "array" in field:
                out += f"    {field['type']} {field['name']}[{field['array']}];\n"
            else:
                out += f"    {field['type']} {field['name']};\n"
        out += "};\n\n"
    out += "#pragma pack(pop)\n"
    
    with open(out_path, "w") as f:
        f.write(out)

def generate_python(schema, out_path):
    type_map = {
        "uint8_t": "ctypes.c_uint8",
        "int8_t": "ctypes.c_int8",
        "uint16_t": "ctypes.c_uint16",
        "int16_t": "ctypes.c_int16",
        "uint32_t": "ctypes.c_uint32",
        "int32_t": "ctypes.c_int32"
    }

    out = "import ctypes\n\n"
    
    # Constants
    for k, v in schema["packet_types"].items():
        out += f"{k} = {v}\n"
    out += "\nPKT_NAMES = {\n"
    for k, v in schema["packet_types"].items():
        out += f"    {v}: \"{k}\",\n"
    out += "}\n\n"

    # Structs
    for struct in schema["structs"]:
        out += f"class {struct['name']}(ctypes.BigEndianStructure):\n"
        out += "    _pack_ = 1\n"
        out += "    _fields_ = [\n"
        for field in struct["fields"]:
            t = type_map.get(field["type"], field["type"])
            if "array" in field:
                t = f"{t} * {field['array']}"
            out += f"        (\"{field['name']}\", {t}),\n"
        out += "    ]\n\n"

    with open(out_path, "w") as f:
        f.write(out)

if __name__ == "__main__":
    with open("protocol.json", "r") as f:
        schema = json.load(f)
    
    generate_cpp(schema, "src/network_protocol.hpp")
    generate_python(schema, "backend/protocol.py")

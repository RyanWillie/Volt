import json


def schematic_projection(schematic):
    return json.loads(schematic.to_json())

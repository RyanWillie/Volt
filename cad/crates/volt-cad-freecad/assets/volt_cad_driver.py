"""
Volt CAD FreeCAD driver — executed by freecadcmd.

Usage (from Rust): freecadcmd driver.py <job.json> <result.json>
"""

from __future__ import annotations

import json
import sys
import traceback
from pathlib import Path


def _write_result(path: Path, payload: dict) -> None:
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def _fail(result_path: Path, message: str) -> None:
    _write_result(
        result_path,
        {"status": "error", "message": message, "data": {}},
    )


def _ok(data: dict | None = None) -> dict:
    out = {"status": "ok", "data": data or {}}
    return out


def _vec3(v) -> "FreeCAD.Vector":
    import FreeCAD  # type: ignore

    return FreeCAD.Vector(float(v[0]), float(v[1]), float(v[2]))


def _norm_axis(axis) -> "FreeCAD.Vector":
    import FreeCAD  # type: ignore

    a = _vec3(axis)
    if a.Length < 1e-12:
        a = FreeCAD.Vector(0, 0, 1)
    else:
        a.normalize()
    return a


def _ensure_doc():
    import FreeCAD as App  # type: ignore

    doc = App.ActiveDocument
    if doc is None:
        raise RuntimeError("no active document; use document-new or document-open first")
    return doc


def _set_shape_obj(doc, obj_name: str, shape) -> None:
    import FreeCAD as App  # type: ignore

    if obj_name in [o.Name for o in doc.Objects]:
        obj = doc.getObject(obj_name)
        obj.Shape = shape
    else:
        obj = doc.addObject("Part::Feature", obj_name)
        obj.Shape = shape
    doc.recompute()


def _get_shape(shapes: dict, sid: str):
    if sid not in shapes:
        raise KeyError(f"unknown solid id: {sid}")
    return shapes[sid]


def _mesh_from_shape(shape, linear_deflection: float):
    import Mesh  # type: ignore

    try:
        import MeshPart  # type: ignore

        return MeshPart.meshFromShape(shape, MaxLength=float(linear_deflection))
    except Exception:  # noqa: BLE001
        pts, faces = shape.tessellate(float(linear_deflection))
        m = Mesh.Mesh()
        for f in faces:
            i0, i1, i2 = int(f[0]), int(f[1]), int(f[2])
            p0, p1, p2 = pts[i0], pts[i1], pts[i2]
            m.addFacet(p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], p2[0], p2[1], p2[2])
        return m


def run_job(job: dict, result_path: Path) -> None:
    import FreeCAD as App  # type: ignore
    import Mesh  # type: ignore
    import Part  # type: ignore

    shapes: dict[str, Part.Shape] = {}
    meshes: dict[str, Mesh.Mesh] = {}
    data_accum: dict = {}

    try:
        for op in job.get("operations", []):
            kind = op.get("op")
            if kind == "document_new":
                name = op.get("name", "Unnamed")
                for dn in list(App.listDocuments().keys()):
                    App.closeDocument(dn)
                App.newDocument(name)
                App.setActiveDocument(name)
                shapes.clear()
                meshes.clear()
            elif kind == "document_open":
                path = Path(op["path"])
                for dn in list(App.listDocuments().keys()):
                    App.closeDocument(dn)
                doc = App.openDocument(str(path))
                App.setActiveDocument(doc.Name)
                shapes.clear()
                meshes.clear()
            elif kind == "document_save":
                doc = _ensure_doc()
                path = Path(op["path"])
                path.parent.mkdir(parents=True, exist_ok=True)
                doc.saveAs(str(path))
            elif kind == "solid_box":
                doc = _ensure_doc()
                origin = op.get("origin", [0, 0, 0])
                size = op["size"]
                lx, ly, lz = float(size[0]), float(size[1]), float(size[2])
                base = _vec3(origin)
                shp = Part.makeBox(lx, ly, lz, base)
                sid = op["id"]
                shapes[sid] = shp
                _set_shape_obj(doc, sid, shp)
            elif kind == "solid_cylinder":
                doc = _ensure_doc()
                base = _vec3(op.get("base", [0, 0, 0]))
                axis = _norm_axis(op.get("axis", [0, 0, 1]))
                r = float(op["radius"])
                h = float(op["height"])
                shp = Part.makeCylinder(r, h, base, axis)
                sid = op["id"]
                shapes[sid] = shp
                _set_shape_obj(doc, sid, shp)
            elif kind == "solid_sphere":
                doc = _ensure_doc()
                c = _vec3(op.get("center", [0, 0, 0]))
                r = float(op["radius"])
                shp = Part.makeSphere(r, c)
                sid = op["id"]
                shapes[sid] = shp
                _set_shape_obj(doc, sid, shp)
            elif kind == "solid_cone":
                doc = _ensure_doc()
                base = _vec3(op.get("base", [0, 0, 0]))
                axis = _norm_axis(op.get("axis", [0, 0, 1]))
                r1 = float(op["r1"])
                r2 = float(op["r2"])
                h = float(op["height"])
                shp = Part.makeCone(r1, r2, h, base, axis)
                sid = op["id"]
                shapes[sid] = shp
                _set_shape_obj(doc, sid, shp)
            elif kind == "transform_translate":
                doc = _ensure_doc()
                sid = op["id"]
                shp = _get_shape(shapes, sid).copy()
                shp.translate(_vec3([op["dx"], op["dy"], op["dz"]]))
                shapes[sid] = shp
                _set_shape_obj(doc, sid, shp)
            elif kind == "transform_rotate":
                doc = _ensure_doc()
                sid = op["id"]
                shp = _get_shape(shapes, sid).copy()
                origin = _vec3(op.get("origin", [0, 0, 0]))
                axis = _norm_axis(op["axis"])
                ang = float(op["angle_deg"])
                shp.rotate(origin, axis, ang)
                shapes[sid] = shp
                _set_shape_obj(doc, sid, shp)
            elif kind == "transform_scale":
                doc = _ensure_doc()
                sid = op["id"]
                shp = _get_shape(shapes, sid).copy()
                u = float(op.get("uniform", 1.0))
                center = _vec3(op.get("center", [0, 0, 0]))
                shp.scale(u, center)
                shapes[sid] = shp
                _set_shape_obj(doc, sid, shp)
            elif kind == "boolean_union":
                doc = _ensure_doc()
                a = _get_shape(shapes, op["a"])
                b = _get_shape(shapes, op["b"])
                out = a.fuse(b)
                oid = op["out"]
                shapes[oid] = out
                _set_shape_obj(doc, oid, out)
            elif kind == "boolean_cut":
                doc = _ensure_doc()
                base_shp = _get_shape(shapes, op["base"])
                tool = _get_shape(shapes, op["tool"])
                out = base_shp.cut(tool)
                oid = op["out"]
                shapes[oid] = out
                _set_shape_obj(doc, oid, out)
            elif kind == "boolean_common":
                doc = _ensure_doc()
                a = _get_shape(shapes, op["a"])
                b = _get_shape(shapes, op["b"])
                out = a.common(b)
                oid = op["out"]
                shapes[oid] = out
                _set_shape_obj(doc, oid, out)
            elif kind == "import_step":
                doc = _ensure_doc()
                path = Path(op["path"])
                shp = Part.Shape()
                shp.read(str(path))
                sid = op["id"]
                shapes[sid] = shp
                _set_shape_obj(doc, sid, shp)
            elif kind == "import_stl":
                doc = _ensure_doc()
                path = Path(op["path"])
                m = Mesh.Mesh()
                m.read(str(path))
                shp = Part.Shape()
                shp.makeShapeFromMesh(m.Topology, 0.1)
                shp = shp.removeSplitter()
                sid = op["id"]
                shapes[sid] = shp
                _set_shape_obj(doc, sid, shp)
            elif kind == "export_step":
                shp = _get_shape(shapes, op["id"])
                path = Path(op["path"])
                path.parent.mkdir(parents=True, exist_ok=True)
                shp.exportStep(str(path))
            elif kind == "export_stl":
                shp = _get_shape(shapes, op["id"])
                path = Path(op["path"])
                path.parent.mkdir(parents=True, exist_ok=True)
                ld = float(op.get("linear_deflection", 0.2))
                m = _mesh_from_shape(shp, ld)
                m.write(str(path))
            elif kind == "mesh_from_shape":
                doc = _ensure_doc()
                shp = _get_shape(shapes, op["solid_id"])
                ld = float(op.get("linear_deflection", 0.2))
                m = _mesh_from_shape(shp, ld)
                mid = op["mesh_id"]
                meshes[mid] = m
                if mid in [o.Name for o in doc.Objects]:
                    mobj = doc.getObject(mid)
                    mobj.Mesh = m
                else:
                    mobj = doc.addObject("Mesh::Feature", mid)
                    mobj.Mesh = m
                doc.recompute()
            elif kind == "export_mesh_stl":
                mid = op["mesh_id"]
                if mid not in meshes:
                    raise KeyError(f"unknown mesh id: {mid}")
                path = Path(op["path"])
                path.parent.mkdir(parents=True, exist_ok=True)
                meshes[mid].write(str(path))
            elif kind == "check_clearance":
                a = _get_shape(shapes, op["a"])
                b = _get_shape(shapes, op["b"])
                dist, _, _ = a.distToShape(b)
                if dist:
                    d0 = min(float(d[0]) for d in dist)
                else:
                    d0 = float("nan")
                data_accum["clearance_mm"] = d0
                data_accum["intersects"] = d0 < 1e-6
            elif kind == "check_bounding_boxes":
                a = _get_shape(shapes, op["a"])
                b = _get_shape(shapes, op["b"])
                bb1 = a.BoundBox
                bb2 = b.BoundBox
                data_accum["intersects"] = bb1.intersect(bb2)
            else:
                raise RuntimeError(f"unsupported operation: {kind}")

        _write_result(result_path, _ok(data_accum))
    except Exception as e:  # noqa: BLE001
        tb = traceback.format_exc()
        _fail(result_path, f"{e}\n{tb}")


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: freecadcmd volt_cad_driver.py <job.json> <result.json>", file=sys.stderr)
        return 2
    job_path = Path(sys.argv[1])
    result_path = Path(sys.argv[2])
    try:
        job = json.loads(job_path.read_text(encoding="utf-8"))
    except Exception as e:  # noqa: BLE001
        _fail(result_path, f"invalid job json: {e}")
        return 1
    run_job(job, result_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

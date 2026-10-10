"""Measures the fallback that hands vertices to the wheel instead of writing
into its draw list from native code: what it costs to get one paragraph's
ImDrawVert/ImDrawIdx arrays from imtypeset into the wheel's ImDrawList

  bulk       ImDrawList.prim_reserve() through the binding, then two memmoves to
             the addresses the binding itself reports (ImVector.data_address()),
             indices rebased with numpy if it is there;
  per-vertex ImDrawList.prim_write_vtx()/prim_write_idx(), one binding call each;
  direct     imtypeset.Paragraph.draw() into the same draw list (the sidecar).

    IMTYPESET_LIBRARY=<libimtypeset_bundle.so> python -P -B \
        tools/bundle_route/measure_buffer_route.py

Headless: one Dear ImGui context, no window. The paragraph is drawn with the
sidecar library in every case, so the vertices are the same; what differs is
the hand-over.
"""

import ctypes
import json
import os
import statistics
import struct
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [os.path.join(REPO, "python"), os.path.join(REPO, "examples", "python")]

from imgui_bundle import imgui  # noqa: E402

import imtypeset as ts  # noqa: E402
import bundle_support as support  # noqa: E402

TEXT = " ".join(
    "The breaker keeps a list of active nodes and, for every legal breakpoint, asks each of "
    "them how bad the line from there to here would be.".split() * 6
)

imgui.create_context()
io = imgui.get_io()
io.set_ini_filename(None)
io.display_size = imgui.ImVec2(1280.0, 900.0)
io.delta_time = 1.0 / 60.0
io.backend_flags = imgui.BackendFlags_.renderer_has_textures
ts.create_context()
support.add_fonts()
paragraph = ts.Paragraph(TEXT)
info = ts.build_info()
vertex_size, index_size = info.sizeof_imdrawvert, info.sizeof_imdrawidx
clip = (0.0, 0.0, 1280.0, 900.0)
try:
    import numpy
except ImportError:
    numpy = None


def bulk(dl):
    vertices, indices = paragraph.draw_to_buffers((20.0, 40.0), clip)
    n_v, n_i = len(vertices) // vertex_size, len(indices) // index_size
    base_v, base_i = dl.vtx_buffer.size(), dl.idx_buffer.size()
    first = dl._vtx_current_idx
    dl.prim_reserve(n_i, n_v)
    ctypes.memmove(dl.vtx_buffer.data_address() + base_v * vertex_size, vertices, len(vertices))
    if numpy is not None:
        rebased = (numpy.frombuffer(indices, dtype=numpy.uint32) + first).tobytes()
    else:
        rebased = struct.pack(f"<{n_i}I", *(i + first for i in struct.unpack(f"<{n_i}I", indices)))
    ctypes.memmove(dl.idx_buffer.data_address() + base_i * index_size, rebased, len(rebased))
    # prim_reserve leaves the list's vertex counter where it was, and the next
    # primitive Dear ImGui adds numbers its vertices from it. The binding lets
    # it be set; without this line everything drawn afterwards is garbled.
    dl._vtx_current_idx = first + n_v
    return n_v


def per_vertex(dl):
    vertices, indices = paragraph.draw_to_buffers((20.0, 40.0), clip)
    n_v, n_i = len(vertices) // vertex_size, len(indices) // index_size
    first = dl._vtx_current_idx
    dl.prim_reserve(n_i, n_v)
    for x, y, u, v, colour in struct.iter_unpack("<ffffI", vertices):
        dl.prim_write_vtx((x, y), (u, v), colour)
    for (i,) in struct.iter_unpack("<I", indices):
        dl.prim_write_idx(i + first)
    return n_v


def direct(dl):
    paragraph.draw(support.draw_list_address(), (20.0, 40.0))
    return 0


results = {"numpy": numpy is not None}
for name, how in (("direct", direct), ("bulk", bulk), ("per_vertex", per_vertex)):
    samples, count = [], 0
    for frame in range(120):
        imgui.new_frame()
        imgui.set_next_window_pos(imgui.ImVec2(0.0, 0.0))
        imgui.set_next_window_size(imgui.ImVec2(1280.0, 900.0))  # nothing is culled
        imgui.begin("measure")
        paragraph.layout(900.0)
        dl = imgui.get_window_draw_list()
        before = dl.vtx_buffer.size()
        started = time.perf_counter()
        how(dl)
        samples.append(time.perf_counter() - started)
        count = dl.vtx_buffer.size() - before
        if frame == 0:
            # A primitive of ImGui's own after ours must index its own vertices.
            dl.add_rect_filled((1.0, 1.0), (2.0, 2.0), 0xFFFFFFFF)
            last = struct.unpack("<I", ctypes.string_at(
                dl.idx_buffer.data_address() + (dl.idx_buffer.size() - 1) * index_size, index_size))[0]
            results.setdefault("list_consistent_after", {})[name] = last < dl._vtx_current_idx and (
                last >= dl._vtx_current_idx - 4)
        imgui.end()
        imgui.end_frame()
    steady = samples[20:]
    results[name] = {"median_us": round(statistics.median(steady) * 1e6, 1), "vertices": count}
print(json.dumps(results))
paragraph.close()
ts.destroy_context()
imgui.destroy_context()

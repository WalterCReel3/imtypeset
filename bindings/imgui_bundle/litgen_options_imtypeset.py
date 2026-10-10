# DRAFT, UNRUN. litgen is not installed on the machine this was written on and
# the bundle's ImGui submodule was not checked out, so not one line of this has
# been executed. It is modelled, option for option, on
# imgui_bundle/external/implot/bindings/litgen_options_implot.py (clone at
# v1.92.900-723-gf639fda4) and on external/imgui/bindings/litgen_options_imgui.py,
# and each option below names the line it is copied from. Where it goes in a
# bundle checkout: external/imtypeset/bindings/litgen_options_imtypeset.py.
# See README.md next to this file, and docs/bindings-notes.md for the
# declaration-by-declaration reasoning.
#
# Written against include/imtypeset.h version 0.2.0, the header as task
# `api-revise` left it (docs/decisions.md D36), by task `c-api`. Still unrun.
from codemanip import code_utils

from litgen.options import LitgenOptions

import sys
import os

THIS_DIR = os.path.dirname(__file__)
sys.path.append(THIS_DIR + "/../../imgui/bindings")
from litgen_options_imgui import litgen_options_imgui, ImguiOptionsType  # noqa: E402


def litgen_options_imtypeset() -> LitgenOptions:
    # As implot does (litgen_options_implot.py:16): start from imgui's options, so
    # that ImVec2, ImVec4, ImU32, ImDrawList, ImFont, ImFontConfig, ImGuiTextBuffer
    # and the ImVector_xxx classes mean, in this module, the classes the imgui
    # module registered. litgen_options_imgui() registers no custom binding
    # (its docstring, litgen_options_imgui.py:1404-1407), which is what a
    # library built on imgui has to start from.
    options = litgen_options_imgui(ImguiOptionsType.imgui_h, docking_branch=True)

    # litgen_options_implot.py:17-18
    options.namespaces_root = ["ImTypeset"]
    options.srcmlcpp_options.functions_api_prefixes = "IMTYPESET_API"

    # litgen_options_implot.py:38 does this for "ImPlot": ImTypesetStyle -> Style,
    # ImTypesetRun -> Run, ImTypesetFace_ -> Face_, and so on. The enum VALUES
    # lose their prefix through imgui's own option (litgen_options_imgui.py:171,
    # enum_flag_remove_values_prefix): ImTypesetFace_Regular -> Face_.regular.
    options.type_replacements.add_last_replacement(r"ImTypeset([A-Z][a-zA-Z0-9]*)", r"\1")
    # litgen_options_implot.py:36
    options.type_replacements.add_first_replacement("ImGuiContext", "ImGui_Context")

    # The structs a host fills or is answered with are plain values; publish copy and deepcopy
    # (litgen_options_implot.py:40-41 does this for ImPlotStyle).
    options.class_copy__regex = (
        "^ImTypesetStyle$|^ImTypesetRun$|^ImTypesetTextRange$|^ImTypesetLine$|^ImTypesetHit$|^ImTypesetFormula$"
    )
    options.class_copy_add_info_in_stub = True

    # `const char* Message` and `const char* Reason` point into storage the
    # library owns. Reading one from Python copies it into a str; assigning one
    # would store a pointer to a temporary. imgui's options make `char*` members
    # read-only (litgen_options_imgui.py:309-311) and drop ImGuiIO's bare
    # `const char*` members altogether (:258); the read-only form is what fits.
    options.member_readonly_by_type__regex = code_utils.append_regex(
        options.member_readonly_by_type__regex, r"^const char\s*\*"
    )

    # --- text_end ---------------------------------------------------------------
    # imgui's bindings turn `const char* text_end = NULL` into an OPTIONAL SECOND
    # STRING (pybind_imgui.cpp:906-921, text_unformatted): a str passed there is
    # converted on its own and `text_end - text` is then the distance between
    # two unrelated buffers. In ImGui that is a known wart nobody trips over.
    # Here the library hashes, copies and compares `text_end - text` bytes, so
    # the parameter must not exist in Python at all: a str carries its own
    # length. Parameters WITH A DEFAULT VALUE can be dropped by name
    # (litgen_options_implot.py:24 drops "stride"): that covers TextUnformatted,
    # TextWithID and CalcTextSize. TextCodepointToByteOffset and
    # TextByteToCodepointOffset take `text_end` with no default, in the middle
    # of their parameters, and are bound by hand below for that reason alone.
    options.fn_params_exclude_names__regex = code_utils.append_regex(
        options.fn_params_exclude_names__regex, r"^text_end$"
    )

    # --- Declarations that are not bound --------------------------------------------
    # --- and declarations litgen's conventions do not handle as written -------------
    # The second kind is excluded here and bound by hand in generate_imtypeset.py
    # (_add_custom_bindings), the way imgui's own options replace ImDrawList's
    # pointer + count polygons with versions taking a list
    # (litgen_options_imgui.py:244-247 excludes, :1263-1285 rebinds).
    options.fn_exclude_by_name__regex = code_utils.append_regex(
        options.fn_exclude_by_name__regex,
        code_utils.join_string_by_pipe_char(
            [
                # NOT BOUND. What IMTYPESET_CHECKVERSION() calls with the sizes of
                # the translation unit it is written in: the binding's module init
                # calls the macro once (pybind_imtypeset.cpp) and Python has no
                # sizes of its own to offer.
                r"^DebugCheckVersionAndDataLayout$",
                # NOT BOUND. A va_list. Text(fmt, ...) is left to the generator,
                # which binds a printf-style function as one str passed through
                # "%s" (pybind_imgui.cpp, the `text` entry after :921).
                r"^TextV$",
                # (face, const void* font_data, int font_data_size, ...): a `void*`
                # parameter is a PyCapsule in nanobind and nothing else (measured
                # on the installed bundle: ImFontConfig.font_data refuses bytes,
                # bytearray, memoryview, numpy and ctypes objects, and takes a
                # capsule named "nb_handle"). Rebound taking `bytes`.
                r"^AddFontFromMemoryTTF$",
                # (..., const ImTypesetRun* runs, int runs_count, ...): a pointer and
                # a count of STRUCTS. litgen's buffer adaptation is for numeric
                # element types (fn_params_buffer_types, litgen_options_implot.py:43-68).
                # Rebound taking a list of Run.
                r"^SetParagraphText$",
                # (..., ImTypesetTextRange* selection = NULL): a struct that is
                # read at entry and written before return. The bundle's rule for
                # in-out scalars is value in, tuple out (litgen_options_imgui.py:401);
                # for a struct it is by hand. Rebound returning (bool, TextRange).
                r"^SelectableText$",
                # (..., ImGuiTextBuffer* out) -> int: would work as written with an
                # imgui.TextBuffer, but a Python caller wants the str. Rebound
                # taking the text so far and returning a str.
                r"^AppendParagraphRangeText$",
                # (..., ImVector<ImVec4>* out_rects) -> int: would work as written
                # with an imgui.ImVector_ImVec4. Rebound returning a list of ImVec4.
                r"^GetParagraphRangeRects$",
                # (text, text_end, offset) with no default on text_end: see above.
                r"^TextCodepointToByteOffset$",
                r"^TextByteToCodepointOffset$",
            ]
        ),
    )

    # DrawParagraphToBuffers(..., ImVector<ImDrawVert>*, ImVector<ImDrawIdx>*, ...)
    # is left to the generator: imgui's options instantiate ImVector<ImDrawVert>
    # and, through the synonym ImDrawIdx=uint, ImVector<uint>
    # (litgen_options_imgui.py:412-473), both default-constructible from Python
    # and both with data_address() for a numpy view.

    return options

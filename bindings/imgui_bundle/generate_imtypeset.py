# DRAFT, UNRUN: see the header of litgen_options_imtypeset.py. Modelled on
# imgui_bundle/external/implot/bindings/generate_implot.py. Where it goes in a
# bundle checkout: external/imtypeset/bindings/generate_imtypeset.py. The
# bundle finds it by its name: the one generate_*.py of the folder
# (external_library.py:77-89), whose main() autogenerate_all.py calls (:9-16).
import os

import litgen
from litgen_options_imtypeset import litgen_options_imtypeset


THIS_DIR = os.path.dirname(__file__)
PYDEF_DIR = THIS_DIR
STUB_DIR = THIS_DIR + "/../../../bindings/imgui_bundle/"

# The submodule. implot's header is at the root of its repository
# (generate_implot.py:14, :103); this repository keeps its one public header in
# include/.
CPP_HEADERS_DIR = THIS_DIR + "/../imtypeset/include"


# The declarations litgen_options_imtypeset() excludes, bound by hand. Stub and
# C++ side by side, as litgen_options_imgui.py does for ImDrawList's polygons
# (:1263-1285). `LG_MODULE` is the placeholder litgen replaces with the module
# object in a custom binding of the main module.
#
# Written against include/imtypeset.h 0.2.0 (docs/decisions.md D36).
#
# The first two entries are the opaque handles. The public header declares
# ImTypesetContext and ImTypesetParagraph and never defines them, and nanobind
# has to be given a class for every type that crosses. The bundle does exactly
# this for imgui_rich_md's Context: an empty class in a custom binding
# (external/imgui_rich_md/bindings/generate_rich_md.py:50-59), with the
# library's INTERNAL header included by the binding file so that the type is
# complete (pybind_rich_md.cpp:18). Here that header is src/context.h.
_STUB = '''
class Context:
    """Everything the library holds: made by create_context, destroyed by destroy_context. Opaque."""
    pass

class Paragraph:
    """One paragraph: text, its layout for a width, what was shipped. Made by create_paragraph, destroyed by
    destroy_paragraph or with its context. Opaque."""
    pass

def add_font_from_memory_ttf(
    face: Face, font_data: bytes, face_index: int = 0, font_cfg: Optional[ImFontConfig] = None
) -> Optional[ImFont]:
    """The bytes are copied: the caller need not keep them."""
    pass

def set_paragraph_text(
    paragraph: Paragraph, text: str, runs: Optional[List[Run]] = None, flags: TextFlags = 0
) -> bool:
    """Run.start and Run.end are BYTE offsets into text.encode("utf-8"): see text_codepoint_to_byte_offset."""
    pass

def selectable_text(
    str_id: str,
    text: str,
    flags: TextFlags = 0,
    wrap_width: float = 0.0,
    style: Optional[Style] = None,
    selection: Optional[TextRange] = None,
) -> Tuple[bool, TextRange]:
    """The selection is the caller's, as a checkbox's bool is: pass the one returned last frame. With None the
    selection is kept with the layout, and what is returned is empty."""
    pass

def append_paragraph_range_text(paragraph: Paragraph, range: TextRange, so_far: str = "") -> str:
    """`so_far` followed by this paragraph's text in the range, with a blank line between them if both are non-empty."""
    pass

def get_paragraph_range_rects(paragraph: Paragraph, range: TextRange) -> List[ImVec4]:
    """One (min x, min y, max x, max y) per line the range touches."""
    pass

def text_codepoint_to_byte_offset(text: str, codepoint_offset: int) -> int:
    """Where codepoint number `codepoint_offset` of `text` starts in text.encode("utf-8")."""
    pass

def text_byte_to_codepoint_offset(text: str, byte_offset: int) -> int:
    """How many codepoints of `text` start before byte `byte_offset` of text.encode("utf-8")."""
    pass
'''

_PYDEF = '''
nb::class_<ImTypesetContext>(LG_MODULE, "Context", "Everything the library holds. Opaque.");
nb::class_<ImTypesetParagraph>(LG_MODULE, "Paragraph", "One paragraph. Opaque.");

LG_MODULE.def("add_font_from_memory_ttf",
    [](ImTypesetFace face, nb::bytes font_data, int face_index, const ImFontConfig* font_cfg) -> ImFont* {
        return ImTypeset::AddFontFromMemoryTTF(face, font_data.c_str(), (int)font_data.size(), face_index, font_cfg);
    },
    nb::arg("face"), nb::arg("font_data"), nb::arg("face_index") = 0, nb::arg("font_cfg") = nb::none(),
    nb::rv_policy::reference);

LG_MODULE.def("set_paragraph_text",
    [](ImTypesetParagraph* paragraph, const std::string& text, const std::vector<ImTypesetRun>& runs, ImTypesetTextFlags flags) -> bool {
        return ImTypeset::SetParagraphText(paragraph, text.data(), text.data() + text.size(),
                                           runs.empty() ? nullptr : runs.data(), (int)runs.size(), flags);
    },
    nb::arg("paragraph"), nb::arg("text"), nb::arg("runs") = std::vector<ImTypesetRun>(), nb::arg("flags") = 0);

LG_MODULE.def("selectable_text",
    [](const std::string& str_id, const std::string& text, ImTypesetTextFlags flags, float wrap_width,
       const ImTypesetStyle* style, std::optional<ImTypesetTextRange> selection) -> std::tuple<bool, ImTypesetTextRange> {
        ImTypesetTextRange range = selection.has_value() ? *selection : ImTypesetTextRange{0, 0};
        const bool ok = ImTypeset::SelectableText(str_id.c_str(), text.data(), text.data() + text.size(), flags,
                                                  wrap_width, style, selection.has_value() ? &range : nullptr);
        return std::make_tuple(ok, range);
    },
    nb::arg("str_id"), nb::arg("text"), nb::arg("flags") = 0, nb::arg("wrap_width") = 0.0f,
    nb::arg("style") = nb::none(), nb::arg("selection") = nb::none());

LG_MODULE.def("append_paragraph_range_text",
    [](const ImTypesetParagraph* paragraph, const ImTypesetTextRange& range, const std::string& so_far) -> std::string {
        ImGuiTextBuffer buffer;
        buffer.append(so_far.data(), so_far.data() + so_far.size());
        ImTypeset::AppendParagraphRangeText(paragraph, range, &buffer);
        return std::string(buffer.begin(), buffer.end());
    },
    nb::arg("paragraph"), nb::arg("range"), nb::arg("so_far") = std::string());

LG_MODULE.def("get_paragraph_range_rects",
    [](const ImTypesetParagraph* paragraph, const ImTypesetTextRange& range) -> std::vector<ImVec4> {
        ImVector<ImVec4> rects;
        ImTypeset::GetParagraphRangeRects(paragraph, range, &rects);
        return std::vector<ImVec4>(rects.begin(), rects.end());
    },
    nb::arg("paragraph"), nb::arg("range"));

LG_MODULE.def("text_codepoint_to_byte_offset",
    [](const std::string& text, int codepoint_offset) -> int {
        return ImTypeset::TextCodepointToByteOffset(text.data(), codepoint_offset, text.data() + text.size());
    },
    nb::arg("text"), nb::arg("codepoint_offset"));

LG_MODULE.def("text_byte_to_codepoint_offset",
    [](const std::string& text, int byte_offset) -> int {
        return ImTypeset::TextByteToCodepointOffset(text.data(), byte_offset, text.data() + text.size());
    },
    nb::arg("text"), nb::arg("byte_offset"));
'''


def _add_custom_bindings(options: litgen.LitgenOptions) -> None:
    # The same call litgen_options_imgui.py makes at :614 and :701.
    options.custom_bindings.add_custom_bindings_to_main_module(stub_code=_STUB, pydef_code=_PYDEF)


def autogenerate_imtypeset() -> None:
    print("autogenerate_imtypeset")
    input_cpp_header = CPP_HEADERS_DIR + "/imtypeset.h"
    output_cpp_pydef_file = PYDEF_DIR + "/pybind_imtypeset.cpp"
    output_stub_pyi_file = STUB_DIR + "/imtypeset.pyi"

    # Configure options
    options = litgen_options_imtypeset()
    options.srcmlcpp_options.flag_show_progress = True

    # generate_implot.py:111-112
    options.use_nanobind()
    options.fn_params_type_replacements.add_replacements([(r"\bImVec2\b", "ImVec2Like"), (r"\bImVec4\b", "ImVec4Like")])

    _add_custom_bindings(options)

    litgen.write_generated_code_for_file(
        options,
        input_cpp_header_file=input_cpp_header,
        output_cpp_pydef_file=output_cpp_pydef_file,
        output_stub_pyi_file=output_stub_pyi_file,
        omit_boxed_types=True,
    )


def sandbox() -> None:
    code = """
    """
    options = litgen_options_imtypeset()
    generator = litgen.LitgenGenerator(options)
    generator.process_cpp_code(code, "file")
    print(generator.stub_code())


def main() -> None:
    autogenerate_imtypeset()
    # sandbox()


if __name__ == "__main__":
    main()

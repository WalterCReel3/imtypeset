# imtypeset

Typeset text for [Dear ImGui](https://github.com/ocornut/imgui): paragraphs
broken into lines the way TeX breaks them, justified and kerned,
formulas written as `$...$` set by a math engine, tables, columns, selection
and copying, drawn through ImGui's own font atlas into your own windows.

This distribution is the Python route. It is a C++ library and a `ctypes`
package over its C ABI, for an application that runs on the
[`imgui-bundle`](https://pypi.org/project/imgui-bundle/) wheel. The library
holds no Dear ImGui of its own: it binds to the one inside the installed
`imgui-bundle` and draws into the application's context. The C++ library, its
documentation and the demo are in the
[repository](https://github.com/WalterCReel3/imtypeset).

## One imtypeset for one imgui-bundle

A library that shares Dear ImGui's structs with another binary is built for
exactly that binary. So every file here is built for **one** `imgui-bundle`
version and requires it exactly, and the version says which:

```
imtypeset 0.4.1.192900     is imtypeset 0.4.1 for imgui-bundle 1.92.900
imtypeset 0.4.1.192801     is imtypeset 0.4.1 for imgui-bundle 1.92.801
```

The fourth number is the `imgui-bundle` version written as one number
(`1.92.900` is `192900`). Pin both, exactly, and move them together:

```
imgui-bundle==1.92.900
imtypeset==0.4.1.192900
```

- `imtypeset==0.4.1` matches nothing: the version has four numbers.
- `imtypeset==0.4.1.*` beside an exact `imgui-bundle==X` resolves to the build
  for X.
- `pip install imtypeset` with nothing else pinned takes the newest line **and
  installs the `imgui-bundle` it was built for, replacing another one**.

`import imtypeset` checks the pair before it loads anything and raises
`ImportError` with a sentence that says what to install if they do not match.

## Where there are wheels

| | CPython | imgui-bundle |
|---|---|---|
| Linux x86-64, glibc 2.28 or later (`manylinux_2_28_x86_64`) | 3.11, 3.12, 3.13, 3.14 | 1.92.900, 1.92.801 |
| macOS 14 or later on Apple Silicon (`macosx_14_0_arm64`) | 3.11, 3.12, 3.13, 3.14 | 1.92.900, 1.92.801 |

Nowhere else: not Windows, not Linux on arm64 or with musl, not macOS on
Intel (for which `imgui-bundle` has no wheel either). There is no source
distribution, so on those `pip` finds nothing to install and says so; a
package that depends on imtypeset should say so with an environment marker
([docs/releasing.md](https://github.com/WalterCReel3/imtypeset/blob/main/docs/releasing.md)
has the line).

## Using it

```python
import os
import imgui_bundle
from imgui_bundle import imgui
import imtypeset as ts

imgui.create_context()  # an application has one already
imgui.get_io().backend_flags |= imgui.BackendFlags_.renderer_has_textures
ts.create_context()     # after ImGui's context, and destroyed before it
fonts = os.path.join(os.path.dirname(imgui_bundle.__file__), "assets", "fonts")
ts.add_font(ts.Face.Regular, os.path.join(fonts, "DroidSans.ttf"))
doc = ts.Document.from_blocks([ts.Block(text="Text set in lines of even colour, the way TeX sets it.")])
doc.layout(240.0)
print(ts.version(), doc.size(), doc.range_text(((0, 0), (0, 8))))
ts.destroy_context()
```

That runs with no window and prints `0.4.1 (240.0, 31.0) Text set`. In an
application the document is laid out and drawn each frame, inside a window:

```python
doc.layout(imgui.get_content_region_avail().x)
at = imgui.get_cursor_screen_pos()
doc.draw(None, (at.x, at.y))  # None: the current window's draw list
```

`imtypeset.host_info()` says which library was loaded and how the pair was
checked.

## More

- [What a wheel is, and what `import imtypeset` checks](https://github.com/WalterCReel3/imtypeset/blob/main/docs/sidecar.md)
- [What a version promises, and what to pin](https://github.com/WalterCReel3/imtypeset/blob/main/docs/versioning.md)
- [Changelog](https://github.com/WalterCReel3/imtypeset/blob/main/docs/CHANGELOG.md)
- [Which Python calls raise, and what](https://github.com/WalterCReel3/imtypeset/blob/main/docs/python-errors.md)

MIT licence. The wheel carries the notices of what the library derives from
(Dear ImGui, fontTools, Unicode data) beside its own licence.

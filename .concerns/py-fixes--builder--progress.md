# py-fixes progress

- claimed; reading. No prior py-fixes progress file existed (fresh start).
- both sidecars built (build/py-fixes-pptmstr, build/py-fixes-advtext), gate accepted. hello_imgui read at the wheels' pinned commits (4c187c66 for 1.92.801, 82f162b8 for 1.92.900) from ~/Source/hello_imgui git objects: LoadAdditionalFonts is called ONCE in Setup after Fonts->Clear(), then nulled; again only if the app re-assigns the callback (no clear then). No DPI-change reload exists there. Now writing tools/bundle_route/font_lifetime.py to run it.

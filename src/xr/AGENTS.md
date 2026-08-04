# XR / OpenXR Documentation

Per-directory guide for `src/xr/` and `include/xr/`. See root `AGENTS.md` for build/lint/testing/style. When editing: mimic neighbors; lint/format/build; no regressions.

| File | Role |
|------|------|
| `src/xr/openxr_context.c` | Instance, system, session management |
| `src/xr/openxr_vulkan.c` | Vulkan interop (graphics binding) |
| `src/xr/openxr_session.c` | Session lifecycle (begin/end/state) |
| `src/xr/openxr_input.c` | VR controller input, actions |
| `src/xr/openxr_view.c` | View configuration (eye poses) |
| `src/xr/openxr_frame.c` | Frame loop, predicted display times |
| `include/xr/openxr_context.h` | `XrContext` type, XR context API |
| `include/xr/openxr_frame.h` | XR frame API |

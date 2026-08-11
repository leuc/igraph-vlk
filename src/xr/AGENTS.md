# XR / OpenXR Documentation

Per-directory guide for `src/xr/` and `include/xr/`. See root `AGENTS.md` for build, test, and style rules.

OpenXR is optional. CMake adds these sources and `USE_OPENXR=1` only when `USE_OPENXR` is enabled and the loader package is found. Keep non-XR builds free of unconditional XR types and calls.

| Files | Role |
|------|------|
| `openxr_context.c` | Instance/system discovery, extension negotiation, events, session-state tracking, and global cleanup |
| `openxr_vulkan.c` | Runtime-required Vulkan instance/device extensions and graphics-device interop |
| `openxr_session.c` | Vulkan-bound session, reference space, view swapchains, image enumeration, and teardown |
| `openxr_input.c` | Action set, controller bindings, pose/action spaces, sync, buttons, hand poses, and capability logging |
| `openxr_view.c` | Frame wait/begin/end, view location, and per-eye view/projection matrices |
| `openxr_frame.c` | App integration: VR initialization, controller movement/menu selection, per-eye rendering, and missed-frame handling |
| `openxr_context.h` | `XrContext`, actions, views, swapchains, session state, and low-level XR API |
| `openxr_frame.h` | App-facing VR initialization, input, and frame API |

## Ownership and Boundaries

- `XrContext` owns the OpenXR instance, system, session, spaces, actions, views, and swapchains.
- `src/vulkan/renderer_xr.c` owns Vulkan framebuffers and depth images created for OpenXR swapchain images.
- `openxr_frame.c` bridges both sides and uses the same menu hover/activation contract as desktop input.
- Controller selection updates the shared hovered menu row before activation; do not add a second activation raycast.
- Destroy per-session resources before instance cleanup and keep partial-initialization cleanup safe.

Do not launch the application or an XR runtime for testing. Build checks are allowed; interactive headset verification is left to the user.

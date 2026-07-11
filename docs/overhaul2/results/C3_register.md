# C3 — registration + CMake snippets (orchestrator merge)

**Do not apply from C3 owner** — C2 owns `register_tools.cpp` and
`src/agent/CMakeLists.txt` this wave. Merge these exactly.

## `register_tools.cpp`

### Includes (with the other tool headers)

```cpp
#include "tools/screen_tools.h"
```

(If the TU includes headers without the `tools/` prefix — same style as
`#include "camera_tools.h"` — use:)

```cpp
#include "screen_tools.h"
```

### Registration (Read risk section, next to camera tools)

```cpp
reg.add(std::make_shared<ScreenCaptureTool>(), ToolRiskClass::Read);
reg.add(std::make_shared<ScreenDescribeTool>(), ToolRiskClass::Read);
```

## `src/agent/CMakeLists.txt` (`pm_agent` sources)

Add one line with the other `tools/*.cpp` entries:

```
tools/screen_tools.cpp
```

Example placement (after `tools/camera_tools.cpp`):

```cmake
    tools/camera_tools.cpp
    tools/screen_tools.cpp
```

Qt Gui is already linked on `pm_agent` (`Qt6::Gui`) — no new link line.

## Test impact

- **`tests/test_agent_e2e.cpp`**: builtin tool count was **29**; after registration
  it becomes **31** (`+2`: `screen_capture`, `screen_describe`).
  Update the assert:

  ```cpp
  assert(names.size() == 31 && "expected 31 builtin tools");
  ```

  (Or whatever the count is after other concurrent tool adds this wave — delta is **+2**.)

## Config (already applied by C3)

- Key: `keys::ScreenCapture` = `"privacy.screen_capture"` (default `"1"`)
- Seeded in `Config::seedDefaults`
- Master-gated via `Config::isMasterGated` (with mic/ambient/face/cameras)
- Privacy UI toggle row is **E5** (one-line add) — not C3

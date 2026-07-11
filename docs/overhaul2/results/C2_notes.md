# C2 — System tools (fs / process / clipboard)

## Delivered

Nine `ITool` subclasses in `src/agent/tools/system_tools.{h,cpp}`, registered in
`register_tools.cpp` with risk classes. AgentLoop already runs `SafetyPolicy`
before `invoke()` — these tools do **not** re-implement path/cmd gates.

| Tool | Args | Risk |
|------|------|------|
| `fs_list` | `{path}` | Read |
| `fs_read` | `{path, max_kb?}` (default 64) | Read |
| `fs_write` | `{path, content, mode:create\|overwrite\|append}` | WriteLocal |
| `fs_move` | `{src, dst}` | Destructive |
| `fs_delete` | `{path}` | Destructive |
| `run_command` | `{command, cwd?, timeout_s?}` | Destructive |
| `app_launch` | `{name_or_path, args?}` | External |
| `clipboard_read` | `{}` | Read |
| `clipboard_write` | `{text}` | WriteLocal |

Builtin count: **29 → 38** (`test_agent_e2e` assertion updated).

## Behaviour notes

### Paths
- Normalized via `QFileInfo::absoluteFilePath` + `QDir::cleanPath`.
- Clear errors for missing / wrong-type paths.
- `fs_write` / `fs_move` call `QDir::mkpath` for missing parents (roots still
  gated by SafetyPolicy).

### `fs_write` modes
- **create** — fails if the file already exists.
- **overwrite** — truncate + write (still **WriteLocal**, not escalated here).
- **append** — open Append.

**Overwrite Confirm:** DAG text said “overwrite of an existing file = Confirm”.
Escalating only on `mode=overwrite` would need SafetyPolicy/arg inspection that
C2 does not own. Overwrite remains **WriteLocal**; A4/C1 Confirm is by risk
ceiling + mode (`strict` still auto-allows WriteLocal). Documented rather than
forking policy. If product wants overwrite→Confirm, extend `SafetyPolicy::check`
to treat `fs_write` + existing path + `mode=overwrite` as Confirm (C1 territory).

### `fs_delete` — Recycle Bin only
- **Windows:** `SHFileOperationW(FO_DELETE)` with `FOF_ALLOWUNDO` (and
  silent / no-UI flags). Never `QFile::remove` hard-delete.
- **Non-Windows:** `QFile::moveToTrash`.
- Result includes `recycled: true` and `removed_from_path`.

### `run_command`
- Windows: `powershell.exe -NoProfile -NonInteractive -Command <command>`.
- Else: `/bin/sh -c`.
- Merged stdout+stderr, truncated to **8 KiB** (`truncated` flag).
- Default timeout 30s (max 300); kill on timeout.
- `cwd` must exist and be a directory when provided.
- Command denylist + path roots remain SafetyPolicy’s job (not re-checked here).

### `app_launch`
- `QProcess::startDetached` first (path or PATH name).
- Windows fallback: `ShellExecuteW("open", …)`.
- Detached; returns `pid` when Qt reports one.

### Clipboard
- Needs `QGuiApplication` (pm_agent already links Qt6::Gui).
- Tools return a clear error if no GUI app instance.

## Registration / C3 marker

`register_tools.cpp` owns C2 registration lines. Comment left for parallel C3:

```cpp
// C3: screen_capture / screen_describe registered by orchestrator ...
```

C3 must **not** race-edit this file per wave plan. C3 left
`docs/overhaul2/results/C3_register.md` for orchestrator merge (`screen_capture` /
`screen_describe` → would make tool count **40** after C2's 38).

## Tests (`tests/test_system_tools.cpp`)

Temp dir under `std::filesystem::temp_directory_path()/pm_system_tools_test`:

1. Registry presence + risk classes for all 9 tools  
2. `fs_write` create → `fs_read` round-trip; second create fails  
3. `fs_write` append  
4. `fs_write` overwrite  
5. `fs_list` sees files + dirs  
6. `fs_move`  
7. `fs_delete` removes from original path (trash)  
8. `run_command` `Write-Output 'pm-ok'` with cwd  
9. Missing-path errors  
10. `clipboard_write` + `clipboard_read` (QGuiApplication + offscreen)

Linked: `pm_core pm_agent Qt6::Core Qt6::Gui`.  
CMake test env: `QT_QPA_PLATFORM=offscreen`.

## Files touched

| File | Action |
|------|--------|
| `src/agent/tools/system_tools.h` | NEW |
| `src/agent/tools/system_tools.cpp` | NEW |
| `src/agent/tools/register_tools.cpp` | +include, +9 reg.add, C3 comment |
| `src/agent/CMakeLists.txt` | +system_tools.cpp |
| `tests/test_system_tools.cpp` | NEW |
| `tests/CMakeLists.txt` | append-only test block |
| `tests/test_agent_e2e.cpp` | count 29→38 + name/risk asserts |
| `docs/overhaul2/results/C2_notes.md` | this file |

## Not done (out of scope / deferred)

- Live “desktop notes.txt” manual accept path (orchestrator / QA).
- `safety.mode==trusted` read-only command auto-allow heuristic (DAG optional;
  Destructive remains always-Confirm per A4 invariant).
- Start-Menu deep search for `app_launch` (PATH + ShellExecute only).
- No builds/ctest run in this node (per C2 strict rules).

# Overhaul 2 — Resume handoff (2026-07-10)

## Committed & green
Batches 1–4: A1–A4, B1–B4, C1–C3, D1, D4, E1–E4. Serial ctest 21/21.

## RESUME HERE
- **D2** goal-tree (agent_loop exclusive)
- **D3** advisor persona + skills (after D1+C3; may touch proactive_engine lightly)
- **E5** copy polish + privacy.screen_capture toggle row
- **EV** apply all results/*_stubs.md to capture_views.cpp
- **F1** full CPU+CUDA builds · **F2** live e2e + owner sign-off · **F3** tag v0.3.0

Build: vcvars64 → cmake --build C:/pm/build/cpu --config Release -j 6
Test: ctest -C Release -j1 (always serial)

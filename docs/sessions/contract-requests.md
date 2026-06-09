# Contract change requests

The frozen contracts — `src/core/event_bus.h` and `src/core/schema.h` — must not be edited
mid-wave (every service depends on their current shape). If a card needs a change, **append an
entry here instead** and code around it. A coordinator reconciles all requests in one pass at
the end of the wave.

Format:

```
## <card-id> — <short title>
- Contract: event_bus | schema
- Proposed change: <signal/field + type>
- Why: <what it unblocks>
- Workaround used meanwhile: <stub / local-only / deferred>
```

---

(none yet)

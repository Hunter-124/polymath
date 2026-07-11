# E2 — stub notes for EV (`src/ui/tools/capture_views.cpp`)

`PersonalitiesView.qml` is now a card list + editor (was a read-only
`QStringList` list) bound to a new `personalityModel` context property, and a
new `PersonalityEditor.qml` needs its own capture entry. `StubApp` gets four
new write-API pass-throughs plus two read-only helper invokables the editor's
combos need.

## 1. New context property `personalityModel`

Add a seeded `StubListModel` (same generic role-named model already used for
`chatModel`/`shoppingModel`/etc., `src/ui/tools/capture_views.cpp:49-106`) with
these roles — they must match `PersonalityModel::roleNames()`
(`src/ui/models/personality_model.cpp`) exactly:

```
{"name","systemPrompt","voice","preferredModel","wakePhrase","tools",
 "temperature","topP","topK","repeatPenalty","maxTokens","avatarPath","isActive"}
```

Seed data (mirrors the existing `stub.personalities()` list —
Assistant/Marcus Aurelius/Ada Lovelace — so `PersonalitiesView`'s populated
capture stays consistent with other views' seed):

```cpp
auto personalities = empty
    ? new StubListModel(
          {"name","systemPrompt","voice","preferredModel","wakePhrase","tools",
           "temperature","topP","topK","repeatPenalty","maxTokens","avatarPath","isActive"},
          {}, &stub)
    : new StubListModel(
          {"name","systemPrompt","voice","preferredModel","wakePhrase","tools",
           "temperature","topP","topK","repeatPenalty","maxTokens","avatarPath","isActive"},
          QVariantList{
              QVariantMap{{"name","Assistant"},
                          {"systemPrompt","You are a helpful local home assistant."},
                          {"voice",""},{"preferredModel","fast"},{"wakePhrase",""},
                          {"tools",QStringList{}},
                          {"temperature",0.7},{"topP",0.9},{"topK",40},
                          {"repeatPenalty",1.1},{"maxTokens",1024},
                          {"avatarPath",""},{"isActive",false}},
              QVariantMap{{"name","Marcus Aurelius"},
                          {"systemPrompt","You are Marcus Aurelius, Roman emperor and Stoic philosopher. You speak with calm, measured wisdom..."},
                          {"voice","en_GB-alan-medium"},{"preferredModel","fast"},{"wakePhrase","Marcus"},
                          {"tools",QStringList{}},
                          {"temperature",0.7},{"topP",0.9},{"topK",40},
                          {"repeatPenalty",1.1},{"maxTokens",1024},
                          {"avatarPath",""},{"isActive",true}},
              QVariantMap{{"name","Ada Lovelace"},
                          {"systemPrompt","You are Ada Lovelace, mathematician and writer, the first computer programmer..."},
                          {"voice","en_GB-jenny_dioco-medium"},{"preferredModel","fast"},{"wakePhrase","Ada"},
                          {"tools",QStringList{}},
                          {"temperature",0.75},{"topP",0.9},{"topK",40},
                          {"repeatPenalty",1.1},{"maxTokens",1024},
                          {"avatarPath",""},{"isActive",false}},
          }, &stub);
```

Register it in `renderView`'s shared context alongside the other models:

```cpp
ctx->setContextProperty("personalityModel", personalities);
```

## 2. `StubListModel` needs two generic invokables

`PersonalityModel` exposes `indexOfName(name)` and `get(row)` — the QML
(`PersonalitiesView.qml`, `PersonalityEditor.qml`) calls both. Add these as
generic methods on `StubListModel` itself (`src/ui/tools/capture_views.cpp`
class around line 49) — they only need `rows_`, which is already a
`QVariantList` of `QVariantMap`, so they're trivially generic and also make
`StubListModel` reusable for any future model that needs name-keyed lookup:

```cpp
Q_INVOKABLE int indexOfName(const QString& name) const {
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_.at(i).toMap().value(QStringLiteral("name")).toString() == name)
            return i;
    return -1;
}
Q_INVOKABLE QVariantMap get(int row) const {
    if (row < 0 || row >= rows_.size()) return {};
    return rows_.at(row).toMap();
}
```

## 3. `StubApp` — new Q_INVOKABLEs

Personality write-API pass-throughs (all no-ops that report success, mirroring
the existing `addModel`/`setModelRole` stub style at
`src/ui/tools/capture_views.cpp:174-177`):

```cpp
Q_INVOKABLE bool createPersonality(const QString&) { return true; }
Q_INVOKABLE bool savePersonality(const QString&, const QString&) { return true; }
Q_INVOKABLE bool setPersonalityAvatar(const QString&, const QString&) { return true; }
Q_INVOKABLE bool deletePersonality(const QString&) { return true; }
```

Combo data sources the editor reads at open (`PersonalityEditor.qml`):

```cpp
Q_INVOKABLE QStringList availableToolNames() const {
    return {"web_search", "fetch_page", "remember", "recall", "shopping_add",
            "reminder_set", "ui_control", "youtube_search", "screen_capture",
            "fs_read", "fs_write", "run_command"};
}
Q_INVOKABLE QStringList availableModels() const {
    return {"fast", "heavy", "gemma-3n-E4B-it-Q4_K_M", "gemma-3-27b-it-Q4_K_M"};
}
```

## 4. `StubSettings` — `ttsVoices()` (if D4's stub hasn't landed yet)

`PersonalityEditor.qml`'s voice combo calls `settings.ttsVoices()` (guarded —
falls back to a free-text field if the method is absent, so this won't crash
the capture either way, just renders the combo empty). D4 already documented
this exact addition in `docs/overhaul2/results/D4_stubs.md` §"New
Q_INVOKABLE" — if EV applies D4's stub notes first, E2 gets it for free;
otherwise apply D4's `ttsVoices()` stub before/alongside this one so both
`SettingsView` and `PersonalityEditor` capture with a populated voice combo.

## 5. `views[]` entries

Add one new capture entry for the standalone editor (list it right after
`08-personalities` in `src/ui/tools/capture_views.cpp:502-516`):

```cpp
{"PersonalitiesView.qml",  "08-personalities", false},
{"PersonalityEditor.qml",  "08b-personality-editor", false},
```

`PersonalityEditor.qml` loads standalone (it's a plain `Item`, not reachable
through nav) and reads the shared `personalityModel`/`settings`/`app` context
properties set up in `renderView` — no extra wiring needed beyond §1–4 above.
Since its `personaName` property defaults to `""`, the un-parameterized
capture renders the **create-new** state; to also verify the edit state, EV
may optionally add a second wrapper view that sets
`personaName: "Marcus Aurelius"` (not required for the accept criteria, just
a nice-to-have — the "New Personality" state alone proves the QML loads and
binds correctly).

## New source/QML files to register in CMake

- `src/ui/models/personality_model.h` (new)
- `src/ui/models/personality_model.cpp` (new) — add to whatever the
  `pm_ui`/models target's source list is (mirrors `shopping_model.cpp`'s
  entry).
- `src/ui/qml/PersonalityEditor.qml` (new) — add to the `qt_add_qml_module`
  QML_FILES list in `src/ui/CMakeLists.txt` (mirrors
  `PersonalitiesView.qml`'s entry).

No other files need new CMake entries — `PersonalitiesView.qml` and
`personality_manager.{h,cpp}` already exist in the build.

## Behavioral notes for EV / F1 review (not stub-mechanical, but worth knowing)

- `PersonalityManager::createBundle/saveBundle/setAvatar/deleteBundle` all end
  by calling `scanBundles()` synchronously, which emits `personalitiesChanged()`
  (same-thread direct connection since `PersonalityManager` and
  `PersonalityModel` both live on the UI thread) — so `AppController`'s
  pass-throughs don't need to manually refresh `personalityModel`; it's always
  current by the time the QML call returns.
- `deleteBundle` refuses (returns `false`, no exception/crash) when asked to
  delete the currently active persona — `PersonalitiesView.qml` also disables
  the Delete button on the active card client-side, but the C++ refusal is the
  actual guard (defense in depth, e.g. if a stale row briefly stays
  clickable during a hot-reload race).
- Avatar import has no native file-picker (no `QtQuick.Dialogs` /
  `Qt6::Widgets` module linked in `src/ui/CMakeLists.txt`); the editor uses a
  paste-a-path `PmTextField` + "Import" button instead of a real Explorer
  picker. If a future node links `QtQuick.Dialogs`, swapping in a real
  `FileDialog` in `PersonalityEditor.qml`'s avatar row is a small, isolated
  change.

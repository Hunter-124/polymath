# D1 — Tasks ▸ Scheduled: stub notes for EV

D1 added a "Scheduled" section to `TaskQueueView.qml` (the file the DAG card calls
`TasksView.qml`) backed by a new `ScheduledGoalsModel` (`src/ui/models/task_model.h/.cpp`,
same file as the existing `TaskModel` — no CMakeLists change needed, it's already compiled
into `pm_ui`). Per the global rule, `capture_views.cpp` is EV-only; `app_controller.*` isn't
in D1's owned-file list either (only `task_model.*`/`TaskQueueView.qml` are), so both need
the additions below before `capture_views` (and the real app) build clean again.

## 1. `src/app/app_controller.h`

Mirror the existing `taskModel` wiring exactly:

```cpp
class ScheduledGoalsModel;   // add next to `class TaskModel;` forward decl (~line 38)

Q_PROPERTY(QObject* scheduledGoalsModel READ scheduledGoalsModel CONSTANT)  // next to taskModel Q_PROPERTY (~line 62)

QObject* scheduledGoalsModel() const;   // next to taskModel() accessor decl (~line 98)

Q_INVOKABLE void refreshSchedules();    // next to refreshTasks() (~line 139)

std::unique_ptr<ScheduledGoalsModel> scheduled_goals_model_;   // next to task_model_ (~line 211)
```

## 2. `src/app/app_controller.cpp`

```cpp
// next to: task_model_ = std::make_unique<TaskModel>(db_, this);   (~line 285)
scheduled_goals_model_ = std::make_unique<ScheduledGoalsModel>(db_, this);

// next to: ctx->setContextProperty("taskModel", task_model_.get());   (~line 386)
ctx->setContextProperty("scheduledGoalsModel", scheduled_goals_model_.get());

// next to: QObject* AppController::taskModel() const { return task_model_.get(); }  (~line 407)
QObject* AppController::scheduledGoalsModel() const { return scheduled_goals_model_.get(); }

// next to: void AppController::refreshTasks() { if (task_model_) task_model_->refresh(); }  (~line 564)
void AppController::refreshSchedules() { if (scheduled_goals_model_) scheduled_goals_model_->refresh(); }
```

`#include "models/task_model.h"` already pulls in `ScheduledGoalsModel` (same header as
`TaskModel`), so no new include is needed in app_controller.cpp — verify it already includes
that header for `TaskModel` and reuse it.

No EventBus wiring needed: unlike `TaskModel` (which listens to `taskUpdated` for live
push-updates), `ScheduledGoalsModel` is refresh-pull only today — `app.refreshSchedules()` on
`Component.onCompleted` is enough for now. (Optional follow-up, not required for D1's accept
criteria: subscribe to `EventBus::goalUpdated` and re-`refresh()` on schedule-originated
terminal goals so `last_fire`/`next_fire` update live without a manual page revisit.)

## 3. `src/ui/tools/capture_views.cpp`

a) StubApp (~line 172, next to `refreshTasks`): add `Q_INVOKABLE void refreshSchedules() {}`.

b) A `scheduledGoalsModel` StubListModel, mirroring the `tasks` one immediately above it
   (~line 371-378), with roles matching `ScheduledGoalsModel::roleNames()`:
   `scheduleId, title, kind, spec, nextFire, lastFire, enabled, deliver, skill, prompt`.

   ```cpp
   auto schedules = empty
       ? new StubListModel(
             {"scheduleId","title","kind","spec","nextFire","lastFire","enabled",
              "deliver","skill","prompt"}, {}, &stub)
       : new StubListModel(
             {"scheduleId","title","kind","spec","nextFire","lastFire","enabled",
              "deliver","skill","prompt"},
             QVariantList{
                 QVariantMap{{"scheduleId",1},{"title","Morning briefing"},
                             {"kind","rrule"},{"spec","FREQ=DAILY"},
                             {"nextFire",2000000000},{"lastFire",0},
                             {"enabled",true},{"deliver","voice"},
                             {"skill",""},{"prompt","Give me my morning briefing"}},
                 QVariantMap{{"scheduleId",2},{"title","Check the oven timer"},
                             {"kind","at"},{"spec","1999999999"},
                             {"nextFire",1999999999},{"lastFire",0},
                             {"enabled",true},{"deliver","chat"},
                             {"skill",""},{"prompt","Remind me to check the oven"}},
                 QVariantMap{{"scheduleId",3},{"title","Weekly session digest"},
                             {"kind","every"},{"spec","604800"},
                             {"nextFire",2000100000},{"lastFire",1999500000},
                             {"enabled",false},{"deliver","notify"},
                             {"skill","session_digest"},{"prompt",""}},
             }, &stub);
   ```

c) Register it alongside the other context properties (~line 441, next to
   `ctx->setContextProperty("taskModel", tasks);`):
   `ctx->setContextProperty("scheduledGoalsModel", schedules);`

`TaskQueueView.qml` is already in the `views[]` capture list (`05-tasks`) — no new entry
needed, it just needs `scheduledGoalsModel` to exist in the context for that render to
succeed (currently it would fail/blank on the "Scheduled" `ListView`'s `model:` binding).

## Roles reference (`ScheduledGoalsModel::roleNames()`, task_model.h)

`scheduleId` (int64), `title` (string), `kind` (string: at|every|rrule), `spec` (string),
`nextFire` (int64 unix, 0 = not scheduled), `lastFire` (int64 unix, 0 = never fired),
`enabled` (bool), `deliver` (string: chat|voice|notify), `skill` (string), `prompt` (string).

Invokables: `refresh()`, `setEnabled(int row, bool enabled)`, `removeItem(int row)` (hard
delete — the QML "trash" button).

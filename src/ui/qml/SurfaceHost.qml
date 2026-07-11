import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// SurfaceHost — overlay region for live surfaces (02 §F5).
// Listens to app.onSurfaceRequested; C1 places this over Main.
Item {
    id: root
    anchors.fill: parent
    z: 40

    // Live surface entries: { id, type, title, argsJson, item }
    property var surfaces: []

    // Type → component map (Loader sources under surfaces/)
    readonly property var typeMap: ({
        "placeholder":  "surfaces/PlaceholderSurface.qml",
        "image":        "surfaces/ImageSurface.qml",
        "web":          "surfaces/WebSurface.qml",
        // video → WebSurface (YouTube clean-mode via args.mode=video)
        "video":        "surfaces/WebSurface.qml",
        // video_picker (B2): "model searched, user selects" results grid.
        "video_picker": "surfaces/VideoPickerSurface.qml",
        // note (E3): research-board markdown card.
        "note":         "surfaces/NoteSurface.qml",
        "monitor":      "surfaces/PlaceholderSurface.qml"
    })

    // Media-ish types that a `board` layout groups next to a group's notes.
    readonly property var boardMediaTypes: ({
        "image": true, "web": true, "video": true, "video_picker": true
    })

    // E3: last layout name explicitly requested (tile/stack/split-*/board);
    // "full" is a transient per-surface focus state layered on top of this
    // so toggling focus off restores whatever the user/AI had picked.
    property string currentLayout: "tile"
    // E3: id of the "focused" surface — the most recently spawned/clicked
    // one, or the one currently full-screened via ImageSurface's click-to-
    // focus. Esc closes this surface.
    property string focusedId: ""
    // E3: true while focusedId is currently shown full-screen (as opposed
    // to just being the last-touched surface) — lets a resize re-apply the
    // full-screen geometry instead of silently dropping back to
    // currentLayout.
    property bool focusIsFull: false
    // E3: board-layout group frame rects, drawn behind the surface cards.
    property var groupFrames: []

    function parseArgs(json) {
        if (!json || json.length === 0) return ({})
        try { return JSON.parse(json) } catch (e) { return ({}) }
    }

    function indexOfId(id) {
        for (var i = 0; i < surfaces.length; ++i)
            if (surfaces[i].id === id) return i
        return -1
    }

    // Extended args (A3): caption/md/x/y/w/h/group are optional passthrough —
    // surfaces that understand them (NoteSurface/ImageSurface/board layout,
    // E3) read them off the model entry; others simply ignore them. Spawning
    // with an id that already exists on the host updates that entry IN PLACE
    // (no arrange() call) so its layout slot/geometry is preserved — this is
    // the mechanism VideoPickerSurface uses to hand its slot to a playing
    // "video" surface without restacking the whole board.
    function spawn(id, type, title, argsJson, caption, md, x, y, w, h, group) {
        var existing = indexOfId(id)
        if (existing >= 0) {
            // Update in place
            var copy = surfaces.slice()
            copy[existing] = {
                id: id,
                type: type || "placeholder",
                title: title || copy[existing].title,
                argsJson: argsJson || copy[existing].argsJson,
                caption: caption !== undefined ? caption : copy[existing].caption,
                md: md !== undefined ? md : copy[existing].md,
                x: x !== undefined ? x : copy[existing].x,
                y: y !== undefined ? y : copy[existing].y,
                w: w !== undefined ? w : copy[existing].w,
                h: h !== undefined ? h : copy[existing].h,
                group: group !== undefined ? group : copy[existing].group
            }
            surfaces = copy
            root.focusedId = id
            return
        }
        var next = surfaces.slice()
        next.push({
            id: id || ("surf-" + Date.now()),
            type: type || "placeholder",
            title: title || "Surface",
            argsJson: argsJson || "",
            caption: caption || "",
            md: md || "",
            x: x !== undefined ? x : -1,
            y: y !== undefined ? y : -1,
            w: w !== undefined ? w : -1,
            h: h !== undefined ? h : -1,
            group: group || ""
        })
        surfaces = next
        root.focusedId = id || next[next.length - 1].id
        arrange(root.currentLayout || "tile")
    }

    function closeSurface(id) {
        var next = []
        for (var i = 0; i < surfaces.length; ++i)
            if (surfaces[i].id !== id) next.push(surfaces[i])
        surfaces = next
        if (root.focusedId === id) {
            root.focusedId = ""
            root.focusIsFull = false
        }
        arrange(root.currentLayout || "tile")
    }

    // E3: closes whichever surface Esc should act on — the tracked focus,
    // falling back to the most recently spawned surface if focus is stale
    // (e.g. after an external close cleared it).
    function closeFocused() {
        if (root.surfaces.length === 0) return
        var id = root.focusedId
        if (id.length === 0 || root.indexOfId(id) < 0)
            id = root.surfaces[root.surfaces.length - 1].id
        root.closeSurface(id)
    }

    // E3: click-to-focus (ImageSurface). Toggles a single surface full-
    // screen, restoring currentLayout on the second click / re-click of a
    // different surface.
    function focusFull(id) {
        if (root.focusedId === id && root.focusIsFull) {
            root.focusedId = ""
            root.focusIsFull = false
            arrange(root.currentLayout || "tile")
        } else {
            root.focusedId = id
            root.focusIsFull = true
            arrange("full", id)
        }
    }

    // layout: tile|stack|split-left|split-right|full|board. focusId is only
    // consulted for "full" (E3 click-to-focus); when omitted, "full"
    // preserves its original behavior of full-screening the most-recently-
    // spawned surface.
    function arrange(layout, focusId) {
        if (layout && layout !== "full")
            root.currentLayout = layout
        var n = surfaces.length
        if (n === 0) {
            layoutCache = ({})
            groupFrames = []
            return
        }
        if (layout === "board") {
            arrangeBoard()
            return
        }
        groupFrames = []
        var cols = Math.ceil(Math.sqrt(n))
        var rows = Math.ceil(n / cols)
        var gap = Style.gap
        var margin = Style.pad
        var cellW = Math.max(200, (width - margin * 2 - gap * (cols - 1)) / cols)
        var cellH = Math.max(140, (height - margin * 2 - gap * (rows - 1) - 40) / rows)
        var cache = ({})
        for (var i = 0; i < n; ++i) {
            var c = i % cols
            var r = Math.floor(i / cols)
            var isFullTarget = focusId ? (surfaces[i].id === focusId) : (i === n - 1)
            if (layout === "stack") {
                cache[surfaces[i].id] = {
                    x: margin + i * 16,
                    y: margin + 40 + i * 16,
                    w: Math.min(360, width - margin * 2),
                    h: Math.min(240, height - margin * 2 - 40)
                }
            } else if (layout === "full" && isFullTarget) {
                cache[surfaces[i].id] = {
                    x: margin, y: margin + 40,
                    w: width - margin * 2, h: height - margin * 2 - 40
                }
            } else if (layout === "split-left") {
                cache[surfaces[i].id] = {
                    x: margin,
                    y: margin + 40 + i * (cellH + gap),
                    w: (width - margin * 2 - gap) / 2,
                    h: cellH
                }
            } else if (layout === "split-right") {
                cache[surfaces[i].id] = {
                    x: margin + (width - margin * 2 - gap) / 2 + gap,
                    y: margin + 40 + i * (cellH + gap),
                    w: (width - margin * 2 - gap) / 2,
                    h: cellH
                }
            } else {
                // tile (default; also used for non-focus-target items when
                // layout === "full")
                cache[surfaces[i].id] = {
                    x: margin + c * (cellW + gap),
                    y: margin + 40 + r * (cellH + gap),
                    w: cellW,
                    h: cellH
                }
            }
        }
        layoutCache = cache
    }

    // E3 — "research board": surfaces sharing args.group get a labeled
    // group frame; within a frame, note-type surfaces stack in a column and
    // media-type surfaces (image/web/video/video_picker) tile beside them —
    // the owner's "clean bounding boxes with pictures alongside
    // information". Ungrouped surfaces fall into a synthetic trailing
    // group with no frame drawn. A3 x/y/w/h hints (when >= 0) place a
    // surface explicitly (x/y relative to the group's content origin);
    // otherwise it is auto-placed in the notes column / media grid flow.
    function arrangeBoard() {
        var margin = Style.pad
        var gap = Style.gap
        var top = margin + 40
        var contentW = Math.max(320, width - margin * 2)

        var order = []
        var byGroup = ({})
        for (var i = 0; i < surfaces.length; ++i) {
            var s = surfaces[i]
            var g = s.group || ""
            if (byGroup[g] === undefined) { byGroup[g] = []; order.push(g) }
            byGroup[g].push(s)
        }
        // Ungrouped ("") surfaces render last, after every named group.
        order.sort(function(a, b) {
            if (a === "" && b !== "") return 1
            if (b === "" && a !== "") return -1
            return 0
        })

        var cache = ({})
        var frames = []
        var y = top

        for (var gi = 0; gi < order.length; ++gi) {
            var gname = order[gi]
            var items = byGroup[gname]
            var notes = items.filter(function(it) { return it.type === "note" })
            var media = items.filter(function(it) { return root.boardMediaTypes[it.type] === true })
            var others = items.filter(function(it) {
                return it.type !== "note" && root.boardMediaTypes[it.type] !== true
            })

            var notesColW = notes.length > 0 ? Math.min(280, contentW * 0.32) : 0
            var mediaX = margin + (notesColW > 0 ? notesColW + gap : 0)
            var mediaW = contentW - (notesColW > 0 ? notesColW + gap : 0)
            var innerTop = y + (gname.length > 0 ? 34 : Style.gapSm)

            // Notes column
            var noteCursorY = innerTop
            for (var ni = 0; ni < notes.length; ++ni) {
                var note = notes[ni]
                var nw = note.w > 0 ? note.w : notesColW
                var nh = note.h > 0 ? note.h : 240
                var nx = note.x >= 0 ? (margin + note.x) : margin
                var ny = note.y >= 0 ? (innerTop + note.y) : noteCursorY
                cache[note.id] = { x: nx, y: ny, w: nw, h: nh }
                noteCursorY = Math.max(noteCursorY, ny + nh + gap)
            }

            // Media grid (+ any other surface types, tiled after media)
            var mCols = Math.max(1, Math.floor(mediaW / 260))
            var mCellW = Math.max(160, (mediaW - gap * (mCols - 1)) / mCols)
            var mCellH = 200
            var mAutoIdx = 0
            var mediaBottom = innerTop
            var flowItems = media.concat(others)
            for (var mi = 0; mi < flowItems.length; ++mi) {
                var m = flowItems[mi]
                var mw = m.w > 0 ? m.w : mCellW
                var mh = m.h > 0 ? m.h : mCellH
                var mx, my
                if (m.x >= 0 && m.y >= 0) {
                    mx = mediaX + m.x
                    my = innerTop + m.y
                } else {
                    var c = mAutoIdx % mCols
                    var r = Math.floor(mAutoIdx / mCols)
                    mx = mediaX + c * (mCellW + gap)
                    my = innerTop + r * (mCellH + gap)
                    mAutoIdx++
                }
                cache[m.id] = { x: mx, y: my, w: mw, h: mh }
                mediaBottom = Math.max(mediaBottom, my + mh + gap)
            }

            var groupBottom = Math.max(noteCursorY, mediaBottom, innerTop + 40)
            if (gname.length > 0) {
                frames.push({
                    group: gname,
                    x: margin - Style.gapSm,
                    y: y,
                    w: contentW + Style.gapSm * 2,
                    h: (groupBottom - y)
                })
            }
            y = groupBottom + Style.gapLg
        }

        layoutCache = cache
        groupFrames = frames
    }

    property var layoutCache: ({})

    onWidthChanged: arrange(root.focusIsFull ? "full" : (root.currentLayout || "tile"), root.focusedId)
    onHeightChanged: arrange(root.focusIsFull ? "full" : (root.currentLayout || "tile"), root.focusedId)

    Connections {
        target: typeof app !== "undefined" ? app : null
        ignoreUnknownSignals: true
        // A3 extended surfaceRequested with caption/md/x/y/w/h/group trailing
        // params (backward-compatible: old emitters with only 5 args still
        // work, the extra formal params just arrive undefined).
        function onSurfaceRequested(id, action, type, title, argsJson,
                                     caption, md, x, y, w, h, group) {
            if (action === "spawn" || action === "spawn_surface")
                root.spawn(id, type, title, argsJson, caption, md, x, y, w, h, group)
            else if (action === "close" || action === "close_surface")
                root.closeSurface(id)
            else if (action === "arrange")
                root.arrange(type || "tile")  // layout name may arrive in type or title
            else if (action === "open_page") {
                // Page navigation is C1 / shell concern; no-op here.
            }
        }
    }

    // Empty when no surfaces — fully transparent hit-test passthrough
    // (only surface cards capture mouse).

    // E3: Esc closes the focused surface. Scoped to WindowShortcut (not
    // ApplicationShortcut) and only enabled while a surface exists, so it
    // doesn't compete with other Esc consumers (e.g. CommandPalette) when
    // the board is empty.
    Shortcut {
        sequence: "Esc"
        context: Qt.WindowShortcut
        enabled: root.surfaces.length > 0
        onActivated: root.closeFocused()
    }

    // Toolbar strip when surfaces present
    Rectangle {
        visible: root.surfaces.length > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 36
        color: Style.tint(Style.surface, 0.72)
        border.width: 1
        border.color: Style.glassBorder
        z: 2

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Style.gap
            anchors.rightMargin: Style.gapSm
            spacing: Style.gapSm
            Text {
                text: root.surfaces.length + " surface" + (root.surfaces.length === 1 ? "" : "s")
                color: Style.textDim
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
            }
            Item { Layout.fillWidth: true }
            PmButton {
                text: "Tile"
                flat: true
                onClicked: root.arrange("tile")
            }
            PmButton {
                text: "Stack"
                flat: true
                onClicked: root.arrange("stack")
            }
            PmButton {
                text: "Board"
                flat: true
                onClicked: root.arrange("board")
            }
            PmButton {
                text: "Close all"
                flat: true
                onClicked: root.surfaces = []
            }
        }
    }

    // E3 — research-board group frames: labeled panels behind the surface
    // cards for whichever args.group values are present. Declared before
    // the surfaces Repeater (and left at default z) so it always paints
    // behind the cards (z: 1).
    Repeater {
        model: root.groupFrames
        delegate: Rectangle {
            required property var modelData
            x: modelData.x
            y: modelData.y
            width: modelData.w
            height: modelData.h
            radius: Style.radiusLg
            color: Style.tint(Style.sectionColor("Chat"), 0.05)
            border.width: 1
            border.color: Style.tint(Style.sectionColor("Chat"), 0.28)

            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: Style.gapSm
                text: modelData.group
                color: Style.textDim
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
                font.bold: true
                elide: Text.ElideRight
                width: Math.max(0, parent.width - Style.gapSm * 2)
            }
        }
    }

    Repeater {
        model: root.surfaces
        delegate: Item {
            id: surfWrap
            required property var modelData
            required property int index

            property var geom: root.layoutCache[modelData.id] || { x: 40, y: 60, w: 280, h: 180 }

            x: geom.x
            y: geom.y
            width: geom.w
            height: geom.h
            z: 1

            Behavior on x { NumberAnimation { duration: Style.durBase; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: Style.durBase; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: Style.durBase; easing.type: Easing.OutCubic } }
            Behavior on height { NumberAnimation { duration: Style.durBase; easing.type: Easing.OutCubic } }

            // E3: click-anywhere-non-interactive-on-the-card focus tracking
            // (Esc target). Sits below the Loader's own content in paint
            // order and doesn't accept the press, so it never steals clicks
            // from a surface's real buttons/controls — it only ever fires
            // for background/padding areas that nothing else handled.
            MouseArea {
                anchors.fill: parent
                z: -1
                onPressed: function(mouse) {
                    root.focusedId = surfWrap.modelData.id
                    mouse.accepted = false
                }
            }

            Loader {
                id: surfLoader
                anchors.fill: parent
                source: root.typeMap[surfWrap.modelData.type] || root.typeMap["placeholder"]
                onLoaded: {
                    if (!item) return
                    if (item.title !== undefined)
                        item.title = surfWrap.modelData.title || "Surface"
                    if (item.argsJson !== undefined)
                        item.argsJson = surfWrap.modelData.argsJson || ""
                    var args = root.parseArgs(surfWrap.modelData.argsJson)
                    if (item.source !== undefined && (args.url || args.path || args.source))
                        item.source = args.url || args.path || args.source || ""
                    if (item.url !== undefined && args.url)
                        item.url = args.url

                    // Extended args passthrough (A3): caption/md/group land on
                    // the item only when the loaded surface declares those
                    // properties (NoteSurface/ImageSurface/board layout — E3);
                    // harmless no-op for surfaces that don't.
                    if (item.caption !== undefined && surfWrap.modelData.caption)
                        item.caption = surfWrap.modelData.caption
                    if (item.md !== undefined && surfWrap.modelData.md)
                        item.md = surfWrap.modelData.md
                    if (item.group !== undefined && surfWrap.modelData.group)
                        item.group = surfWrap.modelData.group

                    // B2: id + host-handled signals for surfaces that need to
                    // close themselves or replace their own slot (picker →
                    // video). Surfaces can't reach AppController/EventBus
                    // directly, so this plain-signal contract is the relay.
                    if (item.surfaceId !== undefined)
                        item.surfaceId = surfWrap.modelData.id
                    if (item.requestSpawn !== undefined)
                        item.requestSpawn.connect(function(id, type, title, argsJson) {
                            root.spawn(id, type, title, argsJson)
                        })
                    if (item.requestClose !== undefined)
                        item.requestClose.connect(function() {
                            root.closeSurface(surfWrap.modelData.id)
                        })
                    // E3: ImageSurface click-to-focus → full layout for just
                    // this surface (toggles back on a second click).
                    if (item.requestFocus !== undefined)
                        item.requestFocus.connect(function(id) {
                            root.focusFull(id || surfWrap.modelData.id)
                        })
                }
            }

            // Close chrome
            PmToolButton {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 4
                z: 5
                iconName: "x"
                onClicked: root.closeSurface(surfWrap.modelData.id)
            }
        }
    }
}

import QtQuick
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
        "monitor":      "surfaces/PlaceholderSurface.qml"
    })

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
        arrange("tile")
    }

    function closeSurface(id) {
        var next = []
        for (var i = 0; i < surfaces.length; ++i)
            if (surfaces[i].id !== id) next.push(surfaces[i])
        surfaces = next
        arrange("tile")
    }

    function arrange(layout) {
        // Minimal tile layout: compute positions into layoutCache keyed by id
        var n = surfaces.length
        if (n === 0) {
            layoutCache = ({})
            return
        }
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
            if (layout === "stack") {
                cache[surfaces[i].id] = {
                    x: margin + i * 16,
                    y: margin + 40 + i * 16,
                    w: Math.min(360, width - margin * 2),
                    h: Math.min(240, height - margin * 2 - 40)
                }
            } else if (layout === "full" && i === n - 1) {
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
                // tile (default)
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

    property var layoutCache: ({})

    onWidthChanged: arrange("tile")
    onHeightChanged: arrange("tile")

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
                text: "Close all"
                flat: true
                onClicked: root.surfaces = []
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

                    // Extended args passthrough (A3): caption/group land on the
                    // item only when the loaded surface declares those
                    // properties (NoteSurface/ImageSurface/board layout — E3);
                    // harmless no-op for surfaces that don't.
                    if (item.caption !== undefined && surfWrap.modelData.caption)
                        item.caption = surfWrap.modelData.caption
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

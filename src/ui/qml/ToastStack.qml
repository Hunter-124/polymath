import QtQuick
import QtQuick.Layouts
import Polymath

// ToastStack — bottom-anchored stack, max 3, severity bar (02 §F3).
// Fed by app.onNoticePosted. C1 places this over Main and can remove interim toast.
Item {
    id: root
    anchors.fill: parent
    z: 50

    readonly property int maxVisible: 3
    readonly property int toastDuration: 5000

    ListModel { id: toasts }

    function severityColor(level) {
        if (level === "error") return Style.bad
        if (level === "warn")  return Style.warn
        if (level === "good")  return Style.good
        return Style.accent
    }

    function pushToast(level, source, message) {
        // Cap model growth; drop oldest beyond a buffer
        while (toasts.count >= 8)
            toasts.remove(0)
        toasts.append({
            level: level || "info",
            source: source || "",
            message: message || "",
            stamp: Date.now()
        })
        // Keep only newest maxVisible effectively shown via view
    }

    Connections {
        target: typeof app !== "undefined" ? app : null
        function onNoticePosted(level, source, message) {
            root.pushToast(level, source, message)
        }
    }

    Column {
        id: stackCol
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Style.pad
        spacing: Style.gapSm
        width: Math.min(parent.width - Style.pad * 2, 480)

        // Show last maxVisible items
        Repeater {
            model: {
                // Build a thin JS model of the tail — ListModel slice via indices
                var start = Math.max(0, toasts.count - root.maxVisible)
                var rows = []
                for (var i = start; i < toasts.count; ++i)
                    rows.push({
                        idx: i,
                        level: toasts.get(i).level,
                        source: toasts.get(i).source,
                        message: toasts.get(i).message,
                        stamp: toasts.get(i).stamp
                    })
                return rows
            }

            delegate: Rectangle {
                id: toastCard
                required property var modelData
                width: stackCol.width
                height: toastInner.implicitHeight + Style.gap
                radius: Style.radiusSm
                color: Style.surface3
                border.width: 1
                border.color: Style.border
                opacity: 0
                // Slide up from below unless reduceMotion
                property real slideY: Style.reduceMotion ? 0 : 16
                transform: Translate { y: toastCard.slideY }

                Component.onCompleted: {
                    opacity = 1
                    slideY = 0
                    dismissTimer.start()
                }
                Behavior on opacity {
                    NumberAnimation {
                        duration: Style.reduceMotion ? Style.durFast : Style.durBase
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on slideY {
                    enabled: !Style.reduceMotion
                    NumberAnimation { duration: Style.durBase; easing.type: Easing.OutCubic }
                }

                Timer {
                    id: dismissTimer
                    interval: root.toastDuration
                    onTriggered: toastCard.dismiss()
                }

                function dismiss() {
                    opacity = 0
                    slideY = Style.reduceMotion ? 0 : 8
                    removeTimer.start()
                }
                Timer {
                    id: removeTimer
                    interval: Style.durBase
                    onTriggered: {
                        // Remove by matching stamp among original model
                        for (var i = 0; i < toasts.count; ++i) {
                            if (toasts.get(i).stamp === toastCard.modelData.stamp) {
                                toasts.remove(i)
                                break
                            }
                        }
                    }
                }

                RowLayout {
                    id: toastInner
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Style.gapSm
                    spacing: Style.gapSm

                    Rectangle {
                        width: 4
                        Layout.fillHeight: true
                        Layout.preferredHeight: 22
                        radius: 2
                        color: root.severityColor(toastCard.modelData.level)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            visible: toastCard.modelData.source.length > 0
                            text: toastCard.modelData.source
                            color: Style.textFaint
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsTiny
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: toastCard.modelData.message
                            color: Style.text
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsBody
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    PmToolButton {
                        iconName: "x"
                        onClicked: toastCard.dismiss()
                    }
                }

                // Soft shadow
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -2
                    anchors.topMargin: 2
                    z: -1
                    radius: parent.radius + 2
                    color: Style.shadowA1
                }
            }
        }
    }
}

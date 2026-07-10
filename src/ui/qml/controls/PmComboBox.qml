import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmComboBox — glass dropdown. Preserves model/textRole/displayText/delegate.
ComboBox {
    id: control
    property color tone: Style.accent

    implicitHeight: Style.controlH
    font.family: Style.fontFamily
    font.pixelSize: Style.fsBody
    leftPadding: 12
    rightPadding: 30

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: Style.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Item {
        x: control.width - width - 10
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 16; height: 16
        PmIcon {
            anchors.centerIn: parent
            width: 14; height: 14
            name: "chevron-down"
            color: Style.textDim
        }
    }

    background: Rectangle {
        radius: Style.radiusSm
        color: control.down ? Style.surface3 : Qt.rgba(1, 1, 1, 0.04)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.tone : Style.glassBorder
    }

    delegate: ItemDelegate {
        required property var model
        required property int index
        width: control.width
        height: Style.controlH
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: model[control.textRole] !== undefined ? model[control.textRole] : model.modelData
            color: highlighted ? control.tone : Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsBody
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
        }
        background: Rectangle {
            color: parent.highlighted ? Style.tint(control.tone, 0.14) : "transparent"
            radius: Style.radiusXs
        }
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 280)
        padding: 1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: Style.radiusSm
            color: Style.surface
            border.color: Style.glassBorder
            border.width: 1
        }
    }
}

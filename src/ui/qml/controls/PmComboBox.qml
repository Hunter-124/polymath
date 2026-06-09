import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmComboBox — dark dropdown.  Styles the field, the indicator, the popup and
// the per-item delegate so it reads as part of the dark shell (Basic leaves all
// of these system-default / light otherwise).
ComboBox {
    id: control
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

    indicator: Canvas {
        x: control.width - width - 12
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 10; height: 6
        contextType: "2d"
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.moveTo(0, 0); ctx.lineTo(width, 0); ctx.lineTo(width / 2, height)
            ctx.closePath()
            ctx.fillStyle = Style.textDim
            ctx.fill()
        }
    }

    background: Rectangle {
        radius: Style.radiusSm
        color: control.down ? Style.surface3 : Style.surface2
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Style.accent : Style.border
    }

    delegate: ItemDelegate {
        required property var model
        required property int index
        width: control.width
        height: Style.controlH
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: model[control.textRole] !== undefined ? model[control.textRole] : model.modelData
            color: highlighted ? Style.accent : Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsBody
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
        }
        background: Rectangle {
            color: parent.highlighted ? Style.surface3 : "transparent"
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
            border.color: Style.border
        }
    }
}

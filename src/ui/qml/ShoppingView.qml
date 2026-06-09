import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    Component.onCompleted: app.refreshShopping()

    ColumnLayout {
        anchors.fill: parent; anchors.margins: Style.pad; spacing: Style.gap

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Shopping List"; color: Style.text
                font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
            }
            Item { Layout.fillWidth: true }
            PmButton { text: "Clear bought"; onClicked: shoppingModel.clearDone() }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: Style.gap
            PmTextField {
                id: item; Layout.fillWidth: true; placeholderText: "Add item…"
                onAccepted: add()
            }
            PmTextField {
                id: qty; Layout.preferredWidth: 130; placeholderText: "Qty (optional)"
                onAccepted: add()
            }
            PmButton { text: "Add"; accent: true; onClicked: add() }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft

            ListView {
                id: list
                anchors.fill: parent; anchors.margins: 8; clip: true; spacing: 2
                model: shoppingModel
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string item
                    required property string quantity
                    required property bool done
                    width: ListView.view ? ListView.view.width : 0
                    height: 44
                    radius: Style.radiusSm
                    color: rowHover.hovered ? Style.surface2 : "transparent"
                    HoverHandler { id: rowHover }

                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 10
                        PmCheckBox {
                            checked: row.done
                            onToggled: shoppingModel.setDone(row.index, checked)
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.quantity.length > 0 ? (row.item + "   ·   " + row.quantity) : row.item
                            color: row.done ? Style.textFaint : Style.text
                            font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                            font.strikeout: row.done
                            wrapMode: Text.WordWrap
                        }
                        PmToolButton {
                            text: "×"   // Latin-1 multiplication sign (Inter has it; ✕ is tofu)
                            onClicked: shoppingModel.removeItem(row.index)
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: "+"
                    title: "Your shopping list is empty"
                    hint: "Add an item above, or just say \"add milk to the shopping list\" — it lands here."
                }
            }
        }
    }

    function add() {
        if (item.text.length === 0) return
        shoppingModel.addItem(item.text, qty.text)
        item.text = ""
        qty.text = ""
    }
}

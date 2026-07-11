import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Shopping — green glass rows (01 §5.6).
Item {
    Component.onCompleted: app.refreshShopping()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Shopping List"
            section: "Shopping"
            PmButton {
                text: "Clear bought"
                onClicked: shoppingModel.clearDone()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gap
            PmTextField {
                id: item
                Layout.fillWidth: true
                tone: Style.sectionColor("Shopping")
                placeholderText: "Add item…"
                onAccepted: add()
            }
            PmTextField {
                id: qty
                Layout.preferredWidth: 130
                tone: Style.sectionColor("Shopping")
                placeholderText: "Qty (optional)"
                onAccepted: add()
            }
            PmButton {
                text: "Add"
                accent: true
                tone: Style.sectionColor("Shopping")
                onClicked: add()
            }
        }

        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Shopping"

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Style.gapSm
                clip: true
                spacing: 2
                model: shoppingModel
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Shopping") }

                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string item
                    required property string quantity
                    required property bool done
                    width: ListView.view ? ListView.view.width : 0
                    height: 44
                    radius: Style.radiusSm
                    color: rowHover.hovered ? Style.tint(Style.sectionColor("Shopping"), 0.08)
                                            : "transparent"
                    border.width: rowHover.hovered ? 1 : 0
                    border.color: Style.glassBorder
                    HoverHandler { id: rowHover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Style.gapSm
                        anchors.rightMargin: Style.gapSm
                        spacing: Style.gapSm
                        PmCheckBox {
                            checked: row.done
                            onToggled: shoppingModel.setDone(row.index, checked)
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.quantity.length > 0
                                  ? (row.item + "   ·   " + row.quantity)
                                  : row.item
                            color: row.done ? Style.textFaint : Style.text
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsBody
                            font.strikeout: row.done
                            wrapMode: Text.WordWrap
                        }
                        PmToolButton {
                            iconName: "trash"
                            onClicked: shoppingModel.removeItem(row.index)
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: "+"
                    iconName: "cart"
                    glyphColor: Style.sectionColor("Shopping")
                    title: "Your shopping list is empty"
                    hint: "Add anything you need to buy — say 'add AA batteries to my list'."
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

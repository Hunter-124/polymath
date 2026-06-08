import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    // Reload from SQLite whenever the page is shown (cheap, bounded query).
    Component.onCompleted: app.refreshShopping()

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Shopping List"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }
            Item { Layout.fillWidth: true }
            Button { text: "Clear bought"; onClicked: shoppingModel.clearDone() }
        }

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: item; Layout.fillWidth: true; placeholderText: "Add item…"
                onAccepted: add()
            }
            TextField {
                id: qty; Layout.preferredWidth: 120; placeholderText: "Qty (opt.)"
                onAccepted: add()
            }
            Button { text: "Add"; onClicked: add() }
        }

        // Backed by the C++ ShoppingModel over the shopping_items table.
        ListView {
            id: list
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
            model: shoppingModel
            delegate: RowLayout {
                id: row
                required property int index
                required property string item
                required property string quantity
                required property bool done
                width: ListView.view ? ListView.view.width : 0
                spacing: 8

                CheckBox {
                    checked: row.done
                    onToggled: shoppingModel.setDone(row.index, checked)
                }
                Label {
                    Layout.fillWidth: true
                    text: row.quantity.length > 0 ? (row.item + "  ·  " + row.quantity) : row.item
                    color: row.done ? "#565f89" : "#c0caf5"
                    font.strikeout: row.done
                    wrapMode: Text.WordWrap
                }
                ToolButton {
                    text: "✕"
                    onClicked: shoppingModel.removeItem(row.index)
                }
            }

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "Your shopping list is empty."
                color: "#565f89"
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

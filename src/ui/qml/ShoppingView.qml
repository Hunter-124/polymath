import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12

        Label { text: "Shopping List"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: item; Layout.fillWidth: true; placeholderText: "Add item…"
                onAccepted: add()
            }
            Button { text: "Add"; onClicked: add() }
        }

        // Wave-3 UI agent backs this with a C++ QAbstractListModel over the
        // shopping_items table; placeholder local model for now.
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
            model: ListModel { id: localItems }
            delegate: CheckBox {
                required property string label
                text: label; palette.windowText: "#c0caf5"
            }
        }
    }
    function add() {
        if (item.text.length === 0) return
        app.addShoppingItem(item.text)
        localItems.append({ label: item.text })
        item.text = ""
    }
}

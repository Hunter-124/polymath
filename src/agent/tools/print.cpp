#include "print.h"
#include "logging.h"

#include <algorithm>

#include <QFileInfo>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPrinter>
#include <QPrinterInfo>
#include <QString>
#include <QTextDocument>

#ifdef Q_OS_WIN
#  include <QProcess>
#endif

// print_document / print_image — send a file to a printer via QPrinter (the
// Windows print subsystem). Runs on the agent worker thread (blocking is fine
// there). When `printer` is omitted the system default printer is used.
//
// Renderable text formats (.txt/.md/.html/.htm) are laid out with QTextDocument
// and painted to the printer. Binary documents we can't render in-process
// (notably .docx) are handed to the OS "print" shell verb on Windows.

namespace polymath {

namespace {

// Resolve a QPrinter for the requested printer name (or the system default).
// Returns false if no usable printer exists.
bool makePrinter(const std::string& name, QPrinter& printer) {
    if (!name.empty()) {
        QPrinterInfo info = QPrinterInfo::printerInfo(QString::fromStdString(name));
        if (info.isNull()) {
            PM_WARN("print: printer '{}' not found; falling back to default", name);
        } else {
            printer.setPrinterName(info.printerName());
            return true;
        }
    }
    QPrinterInfo def = QPrinterInfo::defaultPrinter();
    if (def.isNull()) {
        // No default either — see if *any* printer is installed.
        const auto all = QPrinterInfo::availablePrinters();
        if (all.isEmpty()) return false;
        printer.setPrinterName(all.front().printerName());
        return true;
    }
    printer.setPrinterName(def.printerName());
    return true;
}

bool isRenderableText(const QString& suffix) {
    const QString s = suffix.toLower();
    return s == "txt" || s == "md" || s == "html" || s == "htm" || s == "log";
}

// Print a renderable text/HTML file via QTextDocument.
ToolResult printTextDocument(const QString& path, QPrinter& printer, const std::string& label) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {false, {{"error", "cannot read file"}}, "print_document: cannot read " + label};
    const QString text = QString::fromUtf8(f.readAll());

    QTextDocument doc;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "html" || suffix == "htm") doc.setHtml(text);
    else                                     doc.setPlainText(text);

    doc.print(&printer);
    return {true, {{"printed", path.toStdString()}, {"printer", printer.printerName().toStdString()}},
            "Sent \"" + label + "\" to " + printer.printerName().toStdString()};
}

#ifdef Q_OS_WIN
// Hand a non-renderable document to the Windows shell "print" verb (uses the
// file's associated app, e.g. Word for .docx). Best-effort, fire-and-forget.
ToolResult printViaShell(const QString& path, const std::string& label) {
    // rundll32 + the file's print association is the most portable shell hook.
    const bool ok = QProcess::startDetached(
        QStringLiteral("rundll32.exe"),
        {QStringLiteral("shell32.dll,ShellExec_RunDLL"), path, QStringLiteral("print")});
    if (!ok)
        return {false, {{"error", "shell print failed"}}, "print_document: shell print failed"};
    return {true, {{"printed", path.toStdString()}, {"via", "shell"}},
            "Sent \"" + label + "\" to the printer via its associated app"};
}
#endif

} // namespace

// --- print_document ---------------------------------------------------------

std::string PrintDocumentTool::name() const { return "print_document"; }
std::string PrintDocumentTool::description() const {
    return "Print a document file (text/markdown/HTML rendered in-app; other formats such as "
           ".docx via their associated application) to a printer.";
}

nlohmann::json PrintDocumentTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path",    {{"type", "string"}, {"description", "Absolute path to the document"}}},
            {"printer", {{"type", "string"}, {"description", "Printer name (default: system default)"}}},
        }},
        {"required", {"path"}},
    };
}

ToolResult PrintDocumentTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string pathS = args.value("path", "");
    const std::string printerName = args.value("printer", "");
    if (pathS.empty())
        return {false, {{"error", "path required"}}, "print_document: missing path"};

    const QString path = QString::fromStdString(pathS);
    if (!QFileInfo::exists(path))
        return {false, {{"error", "file not found"}}, "print_document: not found: " + pathS};

    const std::string label = QFileInfo(path).fileName().toStdString();
    const QString suffix = QFileInfo(path).suffix();

    if (isRenderableText(suffix)) {
        QPrinter printer(QPrinter::HighResolution);
        if (!makePrinter(printerName, printer))
            return {false, {{"error", "no printer available"}}, "print_document: no printer"};
        return printTextDocument(path, printer, label);
    }

#ifdef Q_OS_WIN
    return printViaShell(path, label);
#else
    return {false, {{"error", "unsupported document format for in-app printing"}},
            "print_document: cannot render " + label};
#endif
}

// --- print_image ------------------------------------------------------------

std::string PrintImageTool::name() const { return "print_image"; }
std::string PrintImageTool::description() const {
    return "Print an image file (PNG/JPEG/etc.), scaled to fit the page, to a printer.";
}

nlohmann::json PrintImageTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path",    {{"type", "string"}, {"description", "Absolute path to the image"}}},
            {"printer", {{"type", "string"}, {"description", "Printer name (default: system default)"}}},
        }},
        {"required", {"path"}},
    };
}

ToolResult PrintImageTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string pathS = args.value("path", "");
    const std::string printerName = args.value("printer", "");
    if (pathS.empty())
        return {false, {{"error", "path required"}}, "print_image: missing path"};

    const QString path = QString::fromStdString(pathS);
    QImageReader reader(path);
    QImage image = reader.read();
    if (image.isNull())
        return {false, {{"error", "cannot load image"}},
                "print_image: cannot load " + pathS + " (" + reader.errorString().toStdString() + ")"};

    QPrinter printer(QPrinter::HighResolution);
    if (!makePrinter(printerName, printer))
        return {false, {{"error", "no printer available"}}, "print_image: no printer"};

    QPainter painter;
    if (!painter.begin(&printer))
        return {false, {{"error", "cannot start print job"}}, "print_image: painter begin failed"};

    // Scale the image to the printable page rect, preserving aspect ratio.
    // Qt6: derive the paintable area in device pixels from the page layout.
    const QRect page = printer.pageLayout().paintRectPixels(printer.resolution());
    const double sx = static_cast<double>(page.width())  / std::max(1, image.width());
    const double sy = static_cast<double>(page.height()) / std::max(1, image.height());
    const double scale = std::min(sx, sy);   // fit within the page
    const double w = image.width()  * scale;
    const double h = image.height() * scale;
    const QRectF target(page.left() + (page.width()  - w) / 2.0,
                        page.top()  + (page.height() - h) / 2.0, w, h);
    painter.drawImage(target, image);
    painter.end();

    const std::string label = QFileInfo(path).fileName().toStdString();
    return {true, {{"printed", pathS}, {"printer", printer.printerName().toStdString()}},
            "Printed image \"" + label + "\" on " + printer.printerName().toStdString()};
}

} // namespace polymath

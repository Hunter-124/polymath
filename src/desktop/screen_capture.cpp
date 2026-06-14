#include "screen_capture.h"
#include "logging.h"

#include <QImage>
#include <QByteArray>
#include <QBuffer>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace polymath {

ScreenShot ScreenCapture::grab(int maxLongEdge, int jpegQuality) {
    ScreenShot out;
#ifdef _WIN32
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) {
        PM_WARN("screen_capture: invalid virtual-screen metrics ({}x{})", vw, vh);
        return out;
    }
    out.screenW = vw; out.screenH = vh; out.originX = vx; out.originY = vy;

    HDC screenDC = GetDC(nullptr);
    if (!screenDC) { PM_WARN("screen_capture: GetDC failed"); return out; }
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bmp = CreateCompatibleBitmap(screenDC, vw, vh);
    HGDIOBJ oldObj = SelectObject(memDC, bmp);

    // CAPTUREBLT also grabs layered/transparent windows (menus, tooltips).
    const BOOL blit = BitBlt(memDC, 0, 0, vw, vh, screenDC, vx, vy, SRCCOPY | CAPTUREBLT);

    QImage img(vw, vh, QImage::Format_RGB32);
    bool gotBits = false;
    if (blit && !img.isNull()) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = vw;
        bi.bmiHeader.biHeight      = -vh;   // negative => top-down rows
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;    // BGRA in memory == QImage::Format_RGB32
        bi.bmiHeader.biCompression = BI_RGB;
        gotBits = GetDIBits(memDC, bmp, 0, static_cast<UINT>(vh),
                            img.bits(), &bi, DIB_RGB_COLORS) != 0;
    }

    SelectObject(memDC, oldObj);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    if (!gotBits) { PM_WARN("screen_capture: BitBlt/GetDIBits failed"); return out; }

    // Downscale only the copy fed to the VLM; the mapping geometry stays full-res.
    QImage scaled = img;
    if (maxLongEdge > 0 && (vw > maxLongEdge || vh > maxLongEdge))
        scaled = img.scaled(maxLongEdge, maxLongEdge, Qt::KeepAspectRatio,
                            Qt::SmoothTransformation);

    QByteArray jpg;
    QBuffer buf(&jpg);
    buf.open(QIODevice::WriteOnly);
    if (!scaled.save(&buf, "JPG", jpegQuality)) {
        PM_WARN("screen_capture: JPEG encode failed (is the qjpeg image plugin deployed?)");
        return out;
    }

    out.frame.width  = scaled.width();
    out.frame.height = scaled.height();
    out.frame.jpeg.assign(jpg.constBegin(), jpg.constEnd());
    out.frame.ts     = Clock::now();
    out.ok           = !out.frame.jpeg.empty();
    return out;
#else
    (void)maxLongEdge; (void)jpegQuality;
    PM_WARN("screen_capture: only implemented on Windows");
    return out;
#endif
}

} // namespace polymath

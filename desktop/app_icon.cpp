#include "app_icon.hpp"

#include <algorithm>
#include <cmath>

#include <wx/bmpbndl.h>
#include <wx/image.h>

#include "logo_svg.hpp"

// The logo is a black glyph on a transparent background, which disappears on a
// dark dock or taskbar. Put it on a white rounded square, macOS-icon style.
wxBitmap render_app_icon(int size) {
    const int inner = std::max(1, static_cast<int>(std::lround(size * 0.86)));
    const int offset = (size - inner) / 2;
    wxImage logo = wxBitmapBundle::FromSVG(OPENSTINT_LOGO_SVG, wxSize(inner, inner))
                       .GetBitmap(wxSize(inner, inner))
                       .ConvertToImage();
    if (!logo.HasAlpha()) {
        logo.InitAlpha();
    }

    wxImage icon(size, size);
    icon.InitAlpha();
    const double radius = size * 0.22;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // rounded square coverage, anti-aliased over one pixel at the corners
            const double px = x + 0.5, py = y + 0.5;
            const double cx = std::clamp(px, radius, size - radius);
            const double cy = std::clamp(py, radius, size - radius);
            const double dist = std::hypot(px - cx, py - cy);
            const double coverage = std::clamp(radius - dist + 0.5, 0.0, 1.0);

            // white background, logo blended on top
            double r = 255, g = 255, b = 255;
            const int lx = x - offset, ly = y - offset;
            if (lx >= 0 && ly >= 0 && lx < inner && ly < inner) {
                const double a = logo.GetAlpha(lx, ly) / 255.0;
                r = r * (1 - a) + logo.GetRed(lx, ly) * a;
                g = g * (1 - a) + logo.GetGreen(lx, ly) * a;
                b = b * (1 - a) + logo.GetBlue(lx, ly) * a;
            }
            icon.SetRGB(x, y, static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                        static_cast<unsigned char>(b));
            icon.SetAlpha(x, y, static_cast<unsigned char>(std::lround(coverage * 255)));
        }
    }
    return wxBitmap(icon);
}

wxIconBundle app_icon_bundle() {
    wxIconBundle icons;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        wxIcon icon;
        icon.CopyFromBitmap(render_app_icon(size));
        icons.AddIcon(icon);
    }
    return icons;
}

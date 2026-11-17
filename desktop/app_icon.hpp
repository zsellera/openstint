#pragma once

#include <wx/bitmap.h>
#include <wx/iconbndl.h>

// docs/logo.svg (embedded at build time) on a white rounded square, size x size pixels
wxBitmap render_app_icon(int size);

// the app icon at the usual window/taskbar sizes
wxIconBundle app_icon_bundle();

// Coordonnées de l'image (UV, bordures, zoom) et propriété des clics hôtes.
#pragma once
#include <algorithm>
#include <cmath>
#include <deque>

namespace neost {
struct DesktopPointer {
    struct Action { int x, y; bool left, right; };
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    double barrel = 0;
    int bufferW = 0, bufferH = 0, left = 0, top = 0, width = 0, height = 0;
    bool hovered = false;
    bool leftHeld = false, rightHeld = false;
    std::deque<Action> clicks;

    bool point(double mx, double my, int& x, int& y, bool clamp = false) const {
        if (!std::isfinite(mx) || !std::isfinite(my) || x1 <= x0 || y1 <= y0
            || width <= 0 || height <= 0 || bufferW <= 0 || bufferH <= 0) return false;
        if (!clamp && (mx < x0 || mx >= x1 || my < y0 || my >= y1)) return false;
        double u = u0 + (u1-u0) * std::clamp((mx-x0)/(x1-x0), 0.0, 1.0);
        double v = v0 + (v1-v0) * std::clamp((my-y0)/(y1-y0), 0.0, 1.0);
        // Même transformation destination→source que CrtEffectStack (buv).
        const double cx=2*u-1, cy=2*v-1, scale=1+barrel*(cx*cx+cy*cy);
        u=(cx*scale+1)/2; v=(cy*scale+1)/2;
        const double px = u * bufferW - left, py = v * bufferH - top;
        if (!clamp && (px < 0 || py < 0 || px >= width || py >= height)) return false;
        x = int(std::clamp(px, 0.0, double(width-1)));
        y = int(std::clamp(py, 0.0, double(height-1)));
        return true;
    }
    void button(bool right, bool pressed, double mx, double my) {
        bool& held = right ? rightHeld : leftHeld;
        int x, y;
        if (pressed && (!hovered || !point(mx, my, x, y))) return;
        if (!pressed && (!held || !point(mx, my, x, y, true))) return;
        if (pressed == held) return;
        held = pressed;
        clicks.push_back({x, y, leftHeld, rightHeld});
    }
    void reset() { clicks.clear(); leftHeld = rightHeld = hovered = false; }
};
}

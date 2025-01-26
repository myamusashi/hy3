#pragma once
#include <hyprland/src/helpers/Color.hpp>
#include <hyprutils/math/Box.hpp>

class Hy3Render {
public:
	static void renderTab(
	    const Hyprutils::Math::CBox& box,
	    float opacity,
	    bool blur,
	    const CHyprColor& fillColor,
	    const CHyprColor& borderColor,
	    int borderWidth,
	    int radius
	);
};

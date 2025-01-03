#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigDataValues.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/version.h>
#include <hyprlang.hpp>

#include "SelectionHook.hpp"
#include "dispatchers.hpp"
#include "globals.hpp"

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
	PHANDLE = handle;

#ifndef HY3_NO_VERSION_CHECK
	if (GIT_COMMIT_HASH != std::string(__hyprland_api_get_hash())) {
		HyprlandAPI::addNotification(
		    PHANDLE,
		    "[hy3] hy3 was compiled for a different version of hyprland; refusing to load.",
		    CHyprColor {1.0, 0.2, 0.2, 1.0},
		    10000
		);

		throw std::runtime_error("[hy3] target hyprland version mismatch");
	}
#endif

	selection_hook::init();

#define CONF(NAME, TYPE, VALUE)                                                                    \
	HyprlandAPI::addConfigValue(PHANDLE, "plugin:hy3:" NAME, Hyprlang::CConfigValue((TYPE) VALUE))

	using Hyprlang::FLOAT;
	using Hyprlang::INT;
	using Hyprlang::STRING;

	// general
	CONF("no_gaps_when_only", INT, 0);
	CONF("node_collapse_policy", INT, 2);
	CONF("group_inset", INT, 10);
	CONF("tab_first_window", INT, 0);

	// tabs
	CONF("tabs:height", INT, 22);
	CONF("tabs:padding", INT, 5);
	CONF("tabs:from_top", INT, 0);
	CONF("tabs:radius", INT, 6);
	CONF("tabs:border_width", INT, 2);
	CONF("tabs:render_text", INT, 1);
	CONF("tabs:text_center", INT, 1);
	CONF("tabs:text_font", STRING, "Sans");
	CONF("tabs:text_height", INT, 8);
	CONF("tabs:text_padding", INT, 3);
	CONF("tabs:opacity", FLOAT, 1.0);
	CONF("tabs:blur", INT, 1);
	CONF("tabs:col.active", INT, 0xff50a0e0);
	CONF("tabs:col.border.active", INT, 0xff70b0c0);
	CONF("tabs:col.urgent", INT, 0xffff4f4f);
	CONF("tabs:col.border.urgent", INT, 0xffff8080);
	CONF("tabs:col.inactive", INT, 0x50303030);
	CONF("tabs:col.border.inactive", INT, 0x90505050);
	CONF("tabs:col.text.active", INT, 0xff000000);
	CONF("tabs:col.text.urgent", INT, 0xff000000);
	CONF("tabs:col.text.inactive", INT, 0xffffffff);

	// autotiling
	CONF("autotile:enable", INT, 0);
	CONF("autotile:ephemeral_groups", INT, 1);
	CONF("autotile:trigger_height", INT, 0);
	CONF("autotile:trigger_width", INT, 0);
	CONF("autotile:workspaces", STRING, "all");

#undef CONF

	g_Hy3Layout = std::make_unique<Hy3Layout>();
	HyprlandAPI::addLayout(PHANDLE, "hy3", g_Hy3Layout.get());

	registerDispatchers();

	HyprlandAPI::reloadConfig();

	return {"hy3", "i3 like layout for hyprland", "outfoxxed", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {}

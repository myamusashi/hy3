#include "TabGroup.hpp"

#include <GLES2/gl2.h>
#include <cairo/cairo.h>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <pango/pangocairo.h>
#include <pixman.h>

#include "globals.hpp"
#include "log.hpp"
#include "render.hpp"

// This is a workaround CHyprColor not having working arithmetic operator...
static inline CHyprColor
merge_colors(float f1, CHyprColor c1, float f2, CHyprColor c2, float f3, CHyprColor c3) {
	Hyprgraphics::CColor::SOkLab oklab_out;
	float f[3] = {f1, f2, f3};
	Hyprgraphics::CColor::SOkLab c[3] = {c1.asOkLab(), c2.asOkLab(), c3.asOkLab()};

	oklab_out.l = c[0].l * f[0] + c[1].l * f[1] + c[2].l * f[2];
	oklab_out.a = c[0].a * f[0] + c[1].a * f[1] + c[2].a * f[2];
	oklab_out.b = c[0].b * f[0] + c[1].b * f[1] + c[2].b * f[2];
	float alpha = c1.a * f[0] + c2.a * f[1] + c3.a * f[2];

	// the alpha is linear, otherwise use the fact that CColor can take an OkLab and do the correct
	// conversion to an rgb
	return (CHyprColor(Hyprgraphics::CColor(oklab_out), alpha));
}

Hy3TabBarEntry::Hy3TabBarEntry(Hy3TabBar& tab_bar, Hy3Node& node): tab_bar(tab_bar), node(node) {
	g_pAnimationManager->createAnimation(
	    0.0F,
	    this->focused,
	    g_pConfigManager->getAnimationPropertyConfig("fadeSwitch"),
	    AVARDAMAGE_NONE
	);

	g_pAnimationManager->createAnimation(
	    0.0F,
	    this->urgent,
	    g_pConfigManager->getAnimationPropertyConfig("fadeSwitch"),
	    AVARDAMAGE_NONE
	);

	g_pAnimationManager->createAnimation(
	    -1.0F,
	    this->offset,
	    g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
	    AVARDAMAGE_NONE
	);

	g_pAnimationManager->createAnimation(
	    -1.0F,
	    this->width,
	    g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
	    AVARDAMAGE_NONE
	);

	g_pAnimationManager->createAnimation(
	    1.0F,
	    this->vertical_pos,
	    g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
	    AVARDAMAGE_NONE
	);

	g_pAnimationManager->createAnimation(
	    0.0F,
	    this->fade_opacity,
	    g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
	    AVARDAMAGE_NONE
	);

	auto update_callback = [this](auto) { this->tab_bar.dirty = true; };

	this->focused->setUpdateCallback(update_callback);
	this->urgent->setUpdateCallback(update_callback);
	this->offset->setUpdateCallback(update_callback);
	this->width->setUpdateCallback(update_callback);
	this->vertical_pos->setUpdateCallback(update_callback);
	this->fade_opacity->setUpdateCallback(update_callback);

	this->window_title = node.getTitle();
	*this->urgent = node.isUrgent();

	*this->vertical_pos = 0.0;
	*this->fade_opacity = 1.0;
}

bool Hy3TabBarEntry::operator==(const Hy3Node& node) const { return this->node == node; }

bool Hy3TabBarEntry::operator==(const Hy3TabBarEntry& entry) const {
	return this->node == entry.node;
}

void Hy3TabBarEntry::setFocused(bool focused) {
	if (this->focused->goal() != focused) {
		*this->focused = focused;
	}
}

void Hy3TabBarEntry::setUrgent(bool urgent) {
	if (urgent && this->focused->goal() == 1.0) urgent = false;
	if (this->urgent->goal() != urgent) {
		*this->urgent = urgent;
	}
}

void Hy3TabBarEntry::setWindowTitle(std::string title) {
	if (this->window_title != title) {
		this->window_title = title;
		this->tab_bar.dirty = true;
	}
}

void Hy3TabBarEntry::beginDestroy() {
	this->destroying = true;
	*this->vertical_pos = 1.0;
	*this->fade_opacity = 0.0;
}

void Hy3TabBarEntry::unDestroy() {
	this->destroying = false;
	*this->vertical_pos = 0.0;
	*this->fade_opacity = 1.0;
}

bool Hy3TabBarEntry::shouldRemove() {
	return this->destroying && (this->vertical_pos->value() == 1.0 || this->width->value() == 0.0);
}

void Hy3TabBarEntry::render(float scale, CBox& box, float opacity_mul) {
	auto opacity = opacity_mul * this->fade_opacity->value();

	// clang-format off
	static const auto s_radius = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:radius");
	static const auto border_width = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:border_width");
	static const auto s_opacity = ConfigValue<Hyprlang::FLOAT>("plugin:hy3:tabs:opacity");
	static const auto blur = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:blur");
	static const auto col_active = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:col.active");
	static const auto col_border_active = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:col.border.active");
	static const auto col_urgent = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:col.urgent");
	static const auto col_border_urgent = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:col.border.urgent");
	static const auto col_inactive = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:col.inactive");
	static const auto col_border_inactive = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:col.border.inactive");
	// clang-format on

	auto radius = std::min((double) *s_radius * scale, std::min(box.width * 0.5, box.height * 0.5));

	auto focused = this->focused->value();
	auto urgent = this->urgent->value();
	auto inactive = 1.0f - (focused + urgent);

	auto color = merge_colors(
	    focused,
	    CHyprColor(*col_active),
	    urgent,
	    CHyprColor(*col_urgent),
	    inactive,
	    CHyprColor(*col_inactive)
	);

	auto border_color = merge_colors(
	    focused,
	    CHyprColor(*col_border_active),
	    urgent,
	    CHyprColor(*col_border_urgent),
	    inactive,
	    CHyprColor(*col_border_inactive)
	);

	box.round();

	// sometimes enabled before our renderer is called
	glDisable(GL_SCISSOR_TEST);

	Hy3Render::renderTab(
	    box,
	    opacity * *s_opacity,
	    *blur,
	    color,
	    border_color,
	    *border_width,
	    radius
	);

	this->renderText(scale, box, opacity);
}

void Hy3TabBarEntry::renderText(float scale, CBox& box, float opacity) {
	// clang-format off
	static const auto render_text = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:render_text");
	static const auto text_center = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:text_center");
	static const auto text_font = ConfigValue<Hyprlang::STRING>("plugin:hy3:tabs:text_font");
	static const auto text_height = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:text_height");
	static const auto text_padding = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:text_padding");
	static const auto col_text_active = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:col.text.active");
	static const auto col_text_urgent = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:col.text.urgent");
	static const auto col_text_inactive = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:col.text.inactive");
	// clang-format on

	if (!*render_text) {
		if (this->texture) this->texture.reset();
		return;
	}

	auto padding = *text_padding * scale;
	auto width = box.width - padding * 2;

	if (!this->texture
	    // clang-format off
	    || this->last_render.window_title != this->window_title
			|| this->last_render.text_font != *text_font
	    || this->last_render.font_height != *text_height
			|| this->last_render.scale != scale
	    // clang-format on
	    // If render width was smaller than full render width and size changed,
	    // the text is probably ellipsized and needs to be recalculated.
	    || (width != this->last_render.render_width
	        && (width < this->last_render.full_logical_width
	            || this->last_render.logical_width != this->last_render.full_logical_width)))
	{
		this->last_render.window_title = this->window_title;
		this->last_render.text_font = *text_font;
		this->last_render.font_height = *text_height;
		this->last_render.scale = scale;
		this->last_render.render_width = width;

		auto* font_map = pango_cairo_font_map_get_default();
		auto* context = pango_font_map_create_context(font_map);
		auto* layout = pango_layout_new(context);
		pango_layout_set_text(layout, this->window_title.c_str(), -1);

		auto* font_desc = pango_font_description_from_string(*text_font);
		pango_font_description_set_size(font_desc, *text_height * scale * PANGO_SCALE);
		pango_layout_set_font_description(layout, font_desc);
		pango_font_description_free(font_desc);

		PangoRectangle ink_extents;
		PangoRectangle logical_extents;

		pango_layout_get_extents(layout, &ink_extents, &logical_extents);
		this->last_render.full_logical_width = PANGO_PIXELS(logical_extents.width);

		pango_layout_set_width(layout, width * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

		pango_layout_get_extents(layout, &ink_extents, &logical_extents);

		auto ink_x = PANGO_PIXELS(ink_extents.x);
		auto ink_y = PANGO_PIXELS(ink_extents.y);
		auto ink_width = PANGO_PIXELS(ink_extents.width);
		auto ink_height = PANGO_PIXELS(ink_extents.height);

		this->last_render.logical_width = PANGO_PIXELS(logical_extents.width);
		this->last_render.logical_height = PANGO_PIXELS(logical_extents.height);

		this->last_render.texture_x_offset = ink_x;
		this->last_render.texture_y_offset = ink_y;
		this->last_render.texture_width = ink_width;
		this->last_render.texture_height = ink_height;

		auto cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ink_width, ink_height);
		auto cairo = cairo_create(cairo_surface);

		cairo_save(cairo);
		cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cairo);
		cairo_restore(cairo);

		cairo_set_source_rgba(cairo, 1, 1, 1, 1);
		cairo_move_to(cairo, -ink_x, -ink_y);

		pango_cairo_update_layout(cairo, layout);
		pango_cairo_show_layout(cairo, layout);

		cairo_surface_flush(cairo_surface);

		g_object_unref(layout);
		g_object_unref(context);

		auto data = cairo_image_surface_get_data(cairo_surface);

		if (!this->texture) this->texture = makeShared<CTexture>();
		this->texture->allocate();

		glBindTexture(GL_TEXTURE_2D, this->texture->m_iTexID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

#ifdef GLES32
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
#endif

		glTexImage2D(
		    GL_TEXTURE_2D,
		    0,
		    GL_RGBA,
		    ink_width,
		    ink_height,
		    0,
		    GL_RGBA,
		    GL_UNSIGNED_BYTE,
		    data
		);

		cairo_destroy(cairo);
		cairo_surface_destroy(cairo_surface);
	}

	auto x_offset =
	    *text_center ? box.w * 0.5 - this->last_render.logical_width * 0.5 : *text_padding;

	auto y_offset = box.h * 0.5 - this->last_render.logical_height * 0.5;

	auto texture_box = CBox {
	    box.x + x_offset + this->last_render.texture_x_offset,
	    box.y + y_offset + this->last_render.texture_y_offset,
	    this->last_render.texture_width,
	    this->last_render.texture_height,
	};

	texture_box.round();

	auto focused = this->focused->value();
	auto urgent = this->urgent->value();
	auto inactive = 1.0f - (focused + urgent);

	CHyprColor c = merge_colors(
	    focused,
	    CHyprColor(*col_text_active),
	    urgent,
	    CHyprColor(*col_text_urgent),
	    inactive,
	    CHyprColor(*col_text_inactive)
	);

	glBlendFunc(GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_ALPHA);
	glBlendColor(c.r, c.g, c.b, c.a);

	g_pHyprOpenGL->renderTexture(this->texture, &texture_box, opacity);

	glBlendColor(1, 1, 1, 1);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

Hy3TabBar::Hy3TabBar() {
	g_pAnimationManager->createAnimation(
	    1.0f,
	    this->fade_opacity,
	    g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
	    AVARDAMAGE_NONE
	);

	this->fade_opacity->setUpdateCallback([this](auto) { this->dirty = true; });
}

void Hy3TabBar::beginDestroy() {
	for (auto& entry: this->entries) {
		entry.beginDestroy();
	}
}

void Hy3TabBar::tick() {
	auto iter = this->entries.begin();

	while (iter != this->entries.end()) {
		if (iter->shouldRemove()) iter = this->entries.erase(iter);
		else iter = std::next(iter);
	}

	if (this->entries.empty()) this->destroy = true;
}

void Hy3TabBar::updateNodeList(std::list<Hy3Node*>& nodes) {
	std::list<std::list<Hy3TabBarEntry>::iterator> removed_entries;

	auto entry = this->entries.begin();
	auto node = nodes.begin();

	// move any out of order entries to removed_entries
	while (node != nodes.end()) {
		while (true) {
			if (entry == this->entries.end()) goto exitloop;
			if (*entry == **node) break;
			removed_entries.push_back(entry);
			entry = std::next(entry);
		}

		node = std::next(node);
		entry = std::next(entry);
	}

exitloop:

	// move any extra entries to removed_entries
	while (entry != this->entries.end()) {
		removed_entries.push_back(entry);
		entry = std::next(entry);
	}

	entry = this->entries.begin();
	node = nodes.begin();

	// add missing entries, taking first from removed_entries
	while (node != nodes.end()) {
		if (entry == this->entries.end() || *entry != **node) {
			if (std::find(removed_entries.begin(), removed_entries.end(), entry) != removed_entries.end())
			{
				entry = std::next(entry);
				continue;
			}

			auto moved =
			    std::find_if(removed_entries.begin(), removed_entries.end(), [&node](auto entry) {
				    return **node == *entry;
			    });

			if (moved != removed_entries.end()) {
				this->entries.splice(entry, this->entries, *moved);
				entry = *moved;
				removed_entries.erase(moved);
			} else {
				entry = this->entries.emplace(entry, *this, **node);
			}
		}

		entry->unDestroy();

		// set stats from node data
		auto* parent = (*node)->parent;
		auto& parent_group = parent->data.as_group();

		entry->setFocused(
		    parent_group.focused_child == *node
		    || (parent_group.group_focused && parent->isIndirectlyFocused())
		);

		entry->setUrgent((*node)->isUrgent());
		entry->setWindowTitle((*node)->getTitle());

		node = std::next(node);
		if (entry != this->entries.end()) entry = std::next(entry);
	}

	// initiate remove animations for any removed entries
	for (auto& entry: removed_entries) {
		if (!entry->destroying) entry->beginDestroy();
		if (entry->shouldRemove()) this->entries.erase(entry);
	}
}

void Hy3TabBar::updateAnimations(bool warp) {
	int active_entries = 0;
	for (auto& entry: this->entries) {
		if (!entry.destroying) active_entries++;
	}

	float entry_width = active_entries == 0 ? 0.0 : 1.0 / active_entries;
	float offset = 0.0;

	auto entry = this->entries.begin();
	while (entry != this->entries.end()) {
		if (warp) {
			if (entry->width->goal() == 0.0) {
				// this->entries.erase(entry++);
				entry = std::next(entry);
				continue;
			}

			entry->offset->setValueAndWarp(offset);
			entry->width->setValueAndWarp(entry_width);
		} else {
			auto warp_init = entry->offset->goal() == -1.0;

			if (warp_init) {
				entry->offset->setValueAndWarp(offset);
				entry->width->setValueAndWarp(entry->vertical_pos->value() == 0.0 ? 0.0 : entry_width);
			}

			if (!entry->destroying) {
				if (entry->offset->goal() != offset) *entry->offset = offset;
				if ((warp_init || entry->width->goal() != 0.0) && entry->width->goal() != entry_width)
					*entry->width = entry_width;
			}
		}

		if (!entry->destroying) offset += entry->width->goal();
		entry = std::next(entry);
	}
}

void Hy3TabBar::setSize(Vector2D size) {
	if (size == this->size) return;
	this->size = size;
}

Hy3TabGroup::Hy3TabGroup(Hy3Node& node) {
	g_pAnimationManager->createAnimation(
	    Vector2D(0, 0),
	    this->pos,
	    g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
	    AVARDAMAGE_NONE
	);

	g_pAnimationManager->createAnimation(
	    Vector2D(0, 0),
	    this->size,
	    g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
	    AVARDAMAGE_NONE
	);

	this->updateWithGroup(node, true);
	this->pos->warp();
	this->size->warp();
}

void Hy3TabGroup::updateWithGroup(Hy3Node& node, bool warp) {
	static const auto bar_height = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:height");

	auto windowBox = node.getStandardWindowArea();

	auto tpos = windowBox.pos();
	auto tsize = Vector2D(windowBox.size().x, *bar_height);

	this->hidden = node.hidden;
	if (this->pos->goal() != tpos) {
		*this->pos = tpos;
		if (warp) this->pos->warp();
	}

	if (this->size->goal() != tsize) {
		*this->size = tsize;
		if (warp) this->size->warp();
	}

	this->bar.updateNodeList(node.data.as_group().children);
	this->bar.updateAnimations(warp);

	if (node.data.as_group().focused_child != nullptr) {
		this->updateStencilWindows(*node.data.as_group().focused_child);
	}
}

void damageBox(const Vector2D* position, const Vector2D* size) {
	auto box = CBox {position->x, position->y, size->x, size->y};
	// Either a rounding error or an issue below makes this necessary.
	box.expand(1);
	g_pHyprRenderer->damageBox(&box);
}

void Hy3TabGroup::tick() {
	static const auto enter_from_top = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:from_top");
	static const auto padding = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:padding");
	static const auto no_gaps_when_only = ConfigValue<Hyprlang::INT>("plugin:hy3:no_gaps_when_only");

	this->bar.tick();

	if (valid(this->workspace)) {
		auto has_fullscreen = this->workspace->m_bHasFullscreenWindow;

		if (!has_fullscreen && *no_gaps_when_only) {
			auto root_node = g_Hy3Layout->getWorkspaceRootGroup(this->workspace);
			has_fullscreen = root_node != nullptr && root_node->data.as_group().children.size() == 1
			              && root_node->data.as_group().children.front()->data.is_window();
		}

		if (has_fullscreen) {
			if (this->bar.fade_opacity->goal() != 0.0) *this->bar.fade_opacity = 0.0;
		} else {
			if (this->bar.fade_opacity->goal() != 1.0) *this->bar.fade_opacity = 1.0;
		}

		auto workspaceOffset = this->workspace->m_vRenderOffset->value();
		if (this->last_workspace_offset != workspaceOffset) {
			// First we damage the area where the bar was during the previous
			// tick, cleaning up after ourselves
			auto pos = this->last_pos + this->last_workspace_offset;
			auto size = this->last_size;
			damageBox(&pos, &size);

			// Then we damage the current position of the bar, to avoid seeing
			// glitches with animations disabled
			pos = this->pos->value() + workspaceOffset;
			size = this->size->value();
			damageBox(&pos, &size);

			this->bar.damaged = true;
			this->last_workspace_offset = workspaceOffset;
		}

		if (this->workspace->m_fAlpha->isBeingAnimated()) {
			auto pos = this->pos->value();
			auto size = this->size->value();
			damageBox(&pos, &size);
		}
	}

	auto pos = this->pos->value();
	auto size = this->size->value();

	if (this->last_pos != pos || this->last_size != size) {
		damageBox(&this->last_pos, &this->last_size);

		this->bar.damaged = true;
		this->last_pos = pos;
		this->last_size = size;
	}

	if (this->bar.destroy || this->bar.dirty) {
		// damage any area that could be covered by bar in/out animations
		size.y = size.y * 2 + *padding;
		if (*enter_from_top) {
			pos.y -= *padding;
		}

		damageBox(&pos, &size);

		this->bar.damaged = true;
		this->bar.dirty = false;
	}
}

void Hy3TabGroup::renderTabBar() {
	static const auto window_rounding = ConfigValue<Hyprlang::INT>("decoration:rounding");
	static const auto enter_from_top = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:from_top");
	static const auto padding = ConfigValue<Hyprlang::INT>("plugin:hy3:tabs:padding");

	PHLMONITORREF monitor = g_pHyprOpenGL->m_RenderData.pMonitor;
	auto scale = monitor->scale;

	auto monitor_size = monitor->vecSize;
	auto pos = this->pos->value() - monitor->vecPosition;
	auto size = this->size->value();

	if (valid(this->workspace)) {
		pos = pos + this->workspace->m_vRenderOffset->value();
	}

	auto scaled_pos = Vector2D(std::round(pos.x * scale), std::round(pos.y * scale));
	auto scaled_size = Vector2D(std::round(size.x * scale), std::round(size.y * scale));
	CBox box = {scaled_pos.x, scaled_pos.y, scaled_size.x, scaled_size.y};

	// monitor size is not scaled
	if (pos.x > monitor_size.x || pos.y > monitor_size.y || scaled_pos.x + scaled_size.x < 0
	    || scaled_pos.y + scaled_size.y < 0)
		return;

	if (!this->bar.damaged) {
		pixman_region32 damage;
		pixman_region32_init(&damage);

		pixman_region32_intersect_rect(
		    &damage,
		    g_pHyprOpenGL->m_RenderData.damage.pixman(),
		    box.x,
		    box.y,
		    box.width,
		    box.height
		);

		this->bar.damaged = pixman_region32_not_empty(&damage);
		pixman_region32_fini(&damage);
	}

	if (!this->bar.damaged || this->bar.destroy) return;
	this->bar.damaged = false;

	this->bar.setSize(scaled_size);

	auto render_stencil = this->bar.fade_opacity->isBeingAnimated();

	if (!render_stencil) {
		for (auto& entry: this->bar.entries) {
			if (entry.vertical_pos->isBeingAnimated()) {
				render_stencil = true;
				break;
			}
		}
	}

	if (render_stencil) {
		glEnable(GL_STENCIL_TEST);
		glClearStencil(0);
		glClear(GL_STENCIL_BUFFER_BIT);
		glStencilMask(0xff);
		glStencilFunc(GL_ALWAYS, 1, 0xff);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

		for (auto windowref: this->stencil_windows) {
			if (!valid(windowref)) continue;
			auto window = windowref.lock();

			auto wpos =
			    window->m_vRealPosition->value() - monitor->vecPosition
			    + (window->m_pWorkspace ? window->m_pWorkspace->m_vRenderOffset->value() : Vector2D());

			auto wsize = window->m_vRealSize->value();

			CBox window_box = {wpos.x, wpos.y, wsize.x, wsize.y};
			// scaleBox(&window_box, scale);
			window_box.scale(scale);

			if (window_box.width > 0 && window_box.height > 0)
				g_pHyprOpenGL->renderRect(&window_box, CHyprColor(0, 0, 0, 0), *window_rounding);
		}

		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		glStencilMask(0x00);
		glStencilFunc(GL_EQUAL, 0, 0xff);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	}

	auto fade_opacity = this->bar.fade_opacity->value()
	                  * (valid(this->workspace) ? this->workspace->m_fAlpha->value() : 1.0);

	auto render_entry = [&](Hy3TabBarEntry& entry) {
		Vector2D entry_pos = {
		    (pos.x + (entry.offset->value() * size.x) + (*padding * 0.5)) * scale,
		    scaled_pos.y
		        + ((entry.vertical_pos->value() * (size.y + *padding) * scale)
		           * (*enter_from_top ? -1 : 1)),
		};
		Vector2D entry_size = {((entry.width->value() * size.x) - *padding) * scale, scaled_size.y};
		if (entry_size.x < 0 || entry_size.y < 0 || fade_opacity == 0.0) return;

		CBox box = {
		    entry_pos.x,
		    entry_pos.y,
		    entry_size.x,
		    entry_size.y,
		};

		box.round();
		entry.render(scale, box, fade_opacity);
	};

	for (auto& entry: this->bar.entries) {
		if (entry.focused->goal() == 1.0) continue;
		render_entry(entry);
	}

	for (auto& entry: this->bar.entries) {
		if (entry.focused->goal() == 0.0) continue;
		render_entry(entry);
	}

	if (render_stencil) {
		glClearStencil(0);
		glClear(GL_STENCIL_BUFFER_BIT);
		glDisable(GL_STENCIL_TEST);
		glStencilMask(0xff);
		glStencilFunc(GL_ALWAYS, 1, 0xff);
	}
}

void Hy3TabPassElement::draw(const CRegion& damage) { this->group->renderTabBar(); }

void findOverlappingWindows(Hy3Node& node, float height, std::vector<PHLWINDOWREF>& windows) {
	switch (node.data.type()) {
	case Hy3NodeType::Window: windows.push_back(node.data.as_window()); break;
	case Hy3NodeType::Group:
		auto& group = node.data.as_group();

		switch (group.layout) {
		case Hy3GroupLayout::SplitH:
			for (auto* node: group.children) {
				findOverlappingWindows(*node, height, windows);
			}
			break;
		case Hy3GroupLayout::SplitV:
			for (auto* node: group.children) {
				findOverlappingWindows(*node, height, windows);
				height -= node->size.y;
				if (height <= 0) break;
			}
			break;
		case Hy3GroupLayout::Tabbed:
			// assume the height of that node's tab bar already pushes it out of range
			break;
		}
	}
}

void Hy3TabGroup::updateStencilWindows(Hy3Node& group) {
	this->stencil_windows.clear();
	findOverlappingWindows(group, this->size->goal().y, this->stencil_windows);
}

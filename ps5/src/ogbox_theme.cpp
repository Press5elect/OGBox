/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.1.26: the menus' look, black and green, in the overlay's two hooks (base/VulkanUIOverlay.h;
 * ps5/src/example_ps5.cpp has the weak ones that keep upstream's): the colours, and Share Tech Mono
 * (assets.json) for the type. The front end's own drawing (the code rain, the screens) is in
 * src/ogbox_main.cpp.
 */
#include <string>

#include <sys/stat.h>

#include "VulkanUIOverlay.h"
#include "ogbox_ui.h"

namespace mx {
const ImVec4 bright(0.0f, 1.0f, 0.255f, 1.0f);
const ImVec4 text(0.49f, 1.0f, 0.61f, 1.0f);
const ImVec4 label(0.24f, 0.78f, 0.40f, 1.0f);
const ImVec4 line(0.05f, 0.36f, 0.13f, 1.0f);
const ImVec4 panel(0.0f, 0.03f, 0.01f, 0.86f);
} // namespace mx

/* The overlay's colours (the base class calls it once, ps5/src/example_ps5.cpp has the weak one). */
void ps5StyleOverlay(ImGuiStyle &style)
{
	style.WindowRounding = 0.0f;
	style.ChildRounding = 0.0f;
	style.FrameRounding = 0.0f;
	style.GrabRounding = 0.0f;
	style.ScrollbarRounding = 0.0f;
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.FrameBorderSize = 1.0f;
	ImVec4 *c = style.Colors;
	c[ImGuiCol_Text] = mx::text;
	c[ImGuiCol_TextDisabled] = mx::label;
	c[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.94f);
	c[ImGuiCol_ChildBg] = mx::panel;
	c[ImGuiCol_PopupBg] = ImVec4(0.0f, 0.02f, 0.0f, 0.96f);
	c[ImGuiCol_Border] = mx::line;
	c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
	c[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.10f, 0.03f, 0.9f);
	c[ImGuiCol_FrameBgHovered] = ImVec4(0.0f, 0.25f, 0.08f, 0.9f);
	c[ImGuiCol_FrameBgActive] = ImVec4(0.0f, 0.35f, 0.10f, 0.9f);
	c[ImGuiCol_TitleBg] = ImVec4(0.0f, 0.08f, 0.02f, 1.0f);
	c[ImGuiCol_TitleBgActive] = ImVec4(0.0f, 0.16f, 0.05f, 1.0f);
	c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.08f, 0.02f, 1.0f);
	c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
	c[ImGuiCol_ScrollbarGrab] = mx::line;
	c[ImGuiCol_ScrollbarGrabHovered] = mx::label;
	c[ImGuiCol_ScrollbarGrabActive] = mx::bright;
	c[ImGuiCol_CheckMark] = mx::bright;
	c[ImGuiCol_SliderGrab] = mx::label;
	c[ImGuiCol_SliderGrabActive] = mx::bright;
	c[ImGuiCol_Button] = ImVec4(0.0f, 0.20f, 0.07f, 0.9f);
	c[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.55f, 0.16f, 1.0f);
	c[ImGuiCol_ButtonActive] = mx::bright;
	c[ImGuiCol_Header] = ImVec4(0.0f, 0.35f, 0.11f, 0.85f);
	c[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.55f, 0.16f, 0.9f);
	c[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.75f, 0.20f, 1.0f);
	c[ImGuiCol_Separator] = mx::line;
	c[ImGuiCol_NavHighlight] = mx::bright;
	c[ImGuiCol_NavWindowingHighlight] = mx::bright;
	c[ImGuiCol_PlotLines] = mx::bright;
	c[ImGuiCol_PlotHistogram] = mx::bright;
	c[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.45f, 0.13f, 0.6f);
}

/* 0.1.52 to 0.0.2 gave the Emerald theme Roboto here; 0.0.3 has one look (ogbox_ui), no second face. */
bool ps5OverlayExtraFont(std::string &, float &)
{
	return false;
}

/* The menus' typeface: 0.0.3 Inter Medium (assets.json), Share Tech Mono before it. */
bool ps5OverlayFont(std::string &path, float &size)
{
	const size_t slash = path.find_last_of('/');
	const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
	struct stat st;
	if (stat((dir + "Inter-Medium.ttf").c_str(), &st) == 0) {
		path = dir + "Inter-Medium.ttf";
		size = 17.0f;
		return true;
	}
	if (stat((dir + "ShareTechMono-Regular.ttf").c_str(), &st) == 0) {
		path = dir + "ShareTechMono-Regular.ttf";
		size = 18.0f;
		return true;
	}
	return false;
}

/* 0.0.3: the new screens' typefaces (ogbox_ui), baked for the screen: 2160 lines on the PS5. */
void ps5OverlayMoreFonts(ImFontAtlas *atlas, const std::string &dir)
{
	ui::setScale(2160.0f / 1080.0f);
	ui::loadFonts(atlas, dir);
}

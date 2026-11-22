#pragma once
// Build-only registry: the flat HUD's bloom feed pass (src/d3d11/fixed_shader_source.h kUiGlowFeedPsHlsl, ui_layer.cpp uiLayerGlowFeed) and the star
// light's pass that follows it (kStarGlowAddPsHlsl, visual.star_glow). New, so held to their own contracts (symbol, source name, entry, profile, the
// text they compile) rather than a former runtime call's. Both reuse the coverage pass's fullscreen vertex shader (kUiLayerCoverageVsBytecode).
static std::vector<Variant> glowFeedVariants() { return {
    {"kUiGlowFeedPsBytecode","ui_glow_feed_ps","main",nullptr,{},false,edvr::kUiGlowFeedPsHlsl,"ps_5_0"},
    {"kStarGlowAddPsBytecode","star_glow_add_ps","main",nullptr,{},false,edvr::kStarGlowAddPsHlsl,"ps_5_0"},
}; }

// The HUD layer's FXAA (fix.hud_antialiasing = light; src/d3d11/ui_layer_fxaa.h, fixed_shader_source.h kUiLayerFxaaHlsl). Its own list, registered LAST,
// so the indexed contracts of the lists before it keep their places.
static std::vector<Variant> hudAaFxaaVariants() { return {
    {"kUiLayerFxaaBytecode","ui_layer_fxaa_cs","main",nullptr,{},false,edvr::kUiLayerFxaaHlsl,"cs_5_0"},
}; }

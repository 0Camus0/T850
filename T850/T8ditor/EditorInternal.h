/*********************************************************
 * T8ditor — internal shared helpers.
 *
 * Declarations for editor-private helpers that are defined in
 * EditorApp.cpp but also used by the editor's other translation
 * units (panels, renderer). These operate on the shared
 * EditorWorld and editor state.
 *********************************************************/

#ifndef T8DITOR_EDITOR_INTERNAL_H
#define T8DITOR_EDITOR_INTERNAL_H

#include "SceneObject.h"             // t8ditor::SceneObject, t850::AABB (via Picking.h)
#include <scene/SceneDescriptor.h>   // t850::SelectorDesc, t850::SandboxProfileDesc
#include <imgui.h>                   // ImGuiViewport

#include <string>
#include <vector>

namespace t850 { class RenderSkinnedMesh; }

namespace t8ditor {

// ── Selection ────────────────────────────────────────
void ClearMixedSelection();
void AddMixedSelection(int type, int index);

// ── Transform snapshots ──────────────────────────────
void InvalidateSceneObjectTransformSnapshots();

// ── Scene-object queries ─────────────────────────────
t850::RenderSkinnedMesh* GetSkinnedMesh(SceneObject& obj);
bool GetEditorObjectWorldAABB(const SceneObject& object,
                              t850::AABB& outBounds,
                              t850::AABB* outWireBounds = nullptr,
                              t850::AABB* outSkeletonBounds = nullptr,
                              bool* outHasSkeletonBounds = nullptr);

// ── Cubemap selector helpers ─────────────────────────
const t850::SelectorDesc* FindEditorSelectorDesc(const std::vector<t850::SelectorDesc>& selectors,
                                                 const std::string& name);
std::string EditorCubemapPathForSelectorIndex(const t850::SelectorDesc& selector, int selectedIndex);
int EditorCubemapSelectorIndexForPath(const t850::SelectorDesc& selector, const std::string& resourcePath);
int EditorCubemapSelectorIndexFromProfile(const t850::SandboxProfileDesc& profile);

// ── Hosted-window chrome (native ImGui viewports) ────
void ApplyNativeWindowChrome(ImGuiViewport* viewport, const char* title);
void* NativeHandleFromImGuiViewport(ImGuiViewport* viewport);

} // namespace t8ditor

#endif // T8DITOR_EDITOR_INTERNAL_H

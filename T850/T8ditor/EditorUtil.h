/*********************************************************
 * T8ditor — shared editor string / path / override helpers.
 *
 * Stateless utilities for normalising editor resource paths
 * and reading/writing profile override descriptors. Extracted
 * from EditorApp.cpp so the future split translation units can
 * share one implementation.
 *********************************************************/

#ifndef T8DITOR_EDITOR_UTIL_H
#define T8DITOR_EDITOR_UTIL_H

#include <scene/SceneDescriptor.h>   // t850::FloatOverrideDesc / BoolOverrideDesc / IntOverrideDesc

#include <string>
#include <vector>

namespace t8ditor {

// ── String / path ────────────────────────────────────
std::string ToLowerCopy(std::string value);
// Strip a leading (possibly embedded) "Assets/" prefix and normalise slashes.
std::string NormalizeEditorResourcePath(std::string path);
// Lowercased file name (no directory) of a normalised resource path.
std::string MeshEditorProfileModelKey(const std::string& path);
// Case/slash-insensitive resource-path comparison.
bool EditorResourcePathEquals(const std::string& lhs, const std::string& rhs);
// File stem (no directory, no extension) of a forward-slash path.
std::string FileStemFromResourcePath(const std::string& path);
// Returns the solution directory (parent of the bin/ output folder) derived
// from the executable path. Falls back to current working directory.
std::string GetSolutionDir();

// ── Profile override descriptors (upsert / lookup) ────
void SetFloatOverride(std::vector<t850::FloatOverrideDesc>& values, std::string name, float value);
void SetBoolOverride(std::vector<t850::BoolOverrideDesc>& values, std::string name, bool value);
void SetIntOverride(std::vector<t850::IntOverrideDesc>& values, std::string name, int value);
const t850::FloatOverrideDesc* FindEditorFloatOverride(const std::vector<t850::FloatOverrideDesc>& values,
                                                       const std::string& name);
const t850::BoolOverrideDesc* FindEditorBoolOverride(const std::vector<t850::BoolOverrideDesc>& values,
                                                     const std::string& name);
const t850::IntOverrideDesc* FindEditorIntOverride(const std::vector<t850::IntOverrideDesc>& values,
                                                   const std::string& name);

} // namespace t8ditor

#endif // T8DITOR_EDITOR_UTIL_H

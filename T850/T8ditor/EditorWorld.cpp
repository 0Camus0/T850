/*********************************************************
 * T8ditor — EditorWorld instance accessor. See header.
 *********************************************************/

#include "EditorWorld.h"

namespace t8ditor {

EditorWorld& GetEditorWorld() {
  static EditorWorld world;
  return world;
}

} // namespace t8ditor

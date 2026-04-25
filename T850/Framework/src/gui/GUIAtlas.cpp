#include <pch.h>
#include <gui/GUIAtlas.h>
#include <utils/GUIAtlasGenerator.h>

#include <string>

namespace t800 {
namespace {

struct GUIAtlasTextureDef {
  const char* name;
  const char* file;
};

constexpr const char* kDefaultTexturePath = "Assets/Layouts/Textures/";
constexpr const char* kDefaultAtlasPng = "Assets/Layouts/gui_atlas.png";
constexpr const char* kDefaultAtlasJson = "Assets/Layouts/gui_atlas.json";
constexpr int kDefaultAtlasMaxSize = 4096;
constexpr int kDefaultAtlasPadding = 2;

constexpr GUIAtlasTextureDef kDefaultTextures[] = {
  {"SliderBar",              "SliderBar.png"},
  {"SliderKnob",             "SliderKnob.png"},
  {"GUI_CheckBox_Box",       "GUI_CheckBox_Box.png"},
  {"GUI_Checkbox_Check",     "GUI_Checkbox_Check.png"},
  {"GUI_DropBar",            "GUI_DropBar.png"},
  {"GUI_DropNonPressedLeft",  "GUI_DropNonPressedLeft.png"},
  {"GUI_DropNonPressedRight", "GUI_DropNonPressedRight.png"},
  {"GUI_DropPressedLeft",     "GUI_DropPressedLeft.png"},
  {"GUI_DropPressedRight",    "GUI_DropPressedRight.png"},
  {"PopupBackground",        "PopupBackground.png"},
  {"PopUpOKNonPressed",      "PopUpOKNonPressed.png"},
  {"PopUpOkPressed",         "PopUpOkPressed.png"},
  {"PopUpCancelNonPressed",  "PopUpCancelNonPressed.png"},
  {"PopUpCancelPressed",     "PopUpCancelPressed.png"},
};

} // namespace

bool GUIAtlas::RecreateDefault(int maxSpriteSize, int& width, int& height) {
  width = 0;
  height = 0;

  GUIAtlasGenerator generator;
  for (const auto& texture : kDefaultTextures) {
    generator.AddImage(texture.name, std::string(kDefaultTexturePath) + texture.file);
  }

  if (!generator.Generate(kDefaultAtlasMaxSize, maxSpriteSize, kDefaultAtlasPadding)) {
    return false;
  }

  if (!generator.Save(kDefaultAtlasPng, kDefaultAtlasJson)) {
    return false;
  }

  width = generator.GetWidth();
  height = generator.GetHeight();
  return true;
}

} // namespace t800
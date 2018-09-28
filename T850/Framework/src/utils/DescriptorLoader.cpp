#include <utils/DescriptorLoader.h>

#include <json.hpp>

#include <iostream>
#include <fstream>
#include <streambuf>

using namespace std;
using json::JSON;

bool LoadDescriptor(t800::ApplicationDesc& descOut) {
    string contents;
    ifstream input("config.txt");

    if (!input.good()) {
        input.close();
        return false;
    }

    input.seekg(0, ios::end);
    contents.reserve(input.tellg());
    input.seekg(0, ios::beg);
      

    contents.assign((istreambuf_iterator<char>(input)),
        istreambuf_iterator<char>());

    input.close();

    JSON obj = JSON::Load(contents);

    descOut.api = t800::GRAPHICS_API::OPENGL;
    descOut.videoMode = t800::T8_VIDEO_MODE::WINDOWED;
    if(obj["settings"]["API"].ToString() == "D3D"){
        descOut.api = t800::GRAPHICS_API::D3D11;
    }
    if (obj["settings"]["fullscreen"].ToInt() == 1) {
        descOut.videoMode = t800::T8_VIDEO_MODE::FULLSCREEN;
    }

    descOut.width               = obj["settings"]["width"].ToInt();
    descOut.height              = obj["settings"]["height"].ToInt();    
    descOut.qualitylevel        = obj["settings"]["qualitylevel"].ToInt();
    
    descOut.enableguclogging    = obj["settings"]["GuCEnableLogging"].ToInt();
    descOut.gucenableparam      = obj["settings"]["GuCEnableParam"].ToInt();
    descOut.gucverbosity        = obj["settings"]["GuCVerbosity"].ToInt();

    descOut.timerunning         = obj["settings"]["TimeRunning"].ToInt();

    return true;
}
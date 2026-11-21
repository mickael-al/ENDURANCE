#include "ProjectData.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc,char** argv)
{
    assert(argc==2);
    ModelData legacy;
    JS::ParseContext oldScene("{\"name\":\"old cube\",\"idBuffer\":0}");
    assert(oldScene.parseTo(legacy)==JS::Error::NoError);
    assert(!legacy.immutableShadowContent);

    std::ifstream file(argv[1],std::ios::binary);
    assert(file);
    const std::string json((std::istreambuf_iterator<char>(file)),{});
    SceneData scene;
    JS::ParseContext current(json);
    assert(current.parseTo(scene)==JS::Error::NoError);
    assert(scene.modelData.size()==1);
    auto& cube=scene.modelData.front();
    assert(cube.name.find("HumainSize")==0 && cube.immutableShadowContent);
    assert(cube.idMaterial==-1 && cube.idBuffer==0);
    assert(cube.scale==glm::vec3(1,1.8f,1));
    const auto saved=JS::serializeStruct(cube);
    ModelData restored;
    JS::ParseContext roundtrip(saved);
    assert(roundtrip.parseTo(restored)==JS::Error::NoError && restored.immutableShadowContent);
    assert(restored.position==cube.position && restored.scale==cube.scale);
    cube.immutableShadowContent=false;
    const auto disabled=JS::serializeStruct(cube);
    JS::ParseContext off(disabled);
    assert(off.parseTo(restored)==JS::Error::NoError && !restored.immutableShadowContent);
    std::cout<<"PASS production ModelData: legacy default false, actual HumainSize opt-in, enabled/disabled roundtrip preserves transforms\n";
}

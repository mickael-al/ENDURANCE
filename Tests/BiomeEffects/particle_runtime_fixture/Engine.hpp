#pragma once
#include "PointeurClass.hpp"
namespace Ge {
class Engine {
public:
    inline static const ptrClass* pointers=nullptr;
    static const ptrClass* getPtrClassAddr() { return pointers; }
};
}

#pragma once
#include <memory>
#include "Mesh.h"

namespace NovaEngine {

class Primitives {
public:
    static std::shared_ptr<Mesh> cube();               // 1x1x1
    static std::shared_ptr<Mesh> sphere(int segments = 32);
    static std::shared_ptr<Mesh> plane(float size = 10.0f);
};

}  // namespace NovaEngine

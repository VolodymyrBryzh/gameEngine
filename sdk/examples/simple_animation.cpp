#include "../../sdk/survival_sdk.h"
#include <cmath>
#include "../../raymath.h"

namespace SDK {

// Приклад простої анімації гойдання
class SwayAnimation : public IAnimation {
    float time = 0.0f;
    float speed = 2.0f;
    float amplitude = 0.1f;

public:
    void Update(float deltaTime) override {
        time += deltaTime;
    }

    Matrix GetTransform() const override {
        float angle = sinf(time * speed) * amplitude;
        return MatrixRotateZ(angle);
    }
};

// Приклад сутності, яка використовує анімацію
class AnimatedTree : public IEntity {
    Vector3 position;
    IModelProvider* modelProvider;
    IAnimation* animation;

public:
    AnimatedTree(Vector3 pos, IModelProvider* mp) 
        : position(pos), modelProvider(mp) {
        animation = new SwayAnimation();
    }

    ~AnimatedTree() {
        delete animation;
    }

    void Init() override {}
    
    void Update(float deltaTime) override {
        animation->Update(deltaTime);
    }

    void Draw() override {
        if (modelProvider) {
            Model m = modelProvider->GetModel();
            m.transform = animation->GetTransform();
            DrawModel(m, position, 1.0f, WHITE);
        }
    }

    Vector3 GetPosition() const override { return position; }
    void SetPosition(Vector3 pos) override { position = pos; }
};

} // namespace SDK

#pragma once
#include "../raylib.h"
#include <vector>
#include <string>

namespace SDK {

// Інтерфейс для моделей (процедурних або завантажених)
class IModelProvider {
public:
    virtual ~IModelProvider() = default;
    virtual Model GetModel() = 0;
    virtual void Unload() = 0;
};

// Інтерфейс для анімацій
class IAnimation {
public:
    virtual ~IAnimation() = default;
    virtual void Update(float deltaTime) = 0;
    virtual Matrix GetTransform() const = 0;
};

// Базовий клас для ігрових об'єктів (сутністей)
class IEntity {
public:
    virtual ~IEntity() = default;
    virtual void Init() = 0;
    virtual void Update(float deltaTime) = 0;
    virtual void Draw() = 0;
    
    virtual Vector3 GetPosition() const = 0;
    virtual void SetPosition(Vector3 pos) = 0;
};

// Структура для реєстрації нових типів контенту
struct AssetRegistry {
    std::string name;
    IModelProvider* modelProvider;
    // Можна додати фабрику для створення сутностей
};

} // namespace SDK

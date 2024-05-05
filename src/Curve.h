#pragma once

#include "Component.h"

class Curve : public Component
{
public:
    static std::shared_ptr<Curve> create();

    explicit Curve(AK::Badge<Curve>);

    virtual void draw_editor() override;
    
    std::vector<glm::vec2> points = {};
    glm::vec2 get_point_at(float x) const;

protected:
    explicit Curve();

private:
    float length() const;
};

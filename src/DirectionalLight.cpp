#include "DirectionalLight.h"

#include "Renderer.h"

std::shared_ptr<DirectionalLight> DirectionalLight::create()
{
    auto directional_light = std::make_shared<DirectionalLight>();
    Renderer::get_instance()->register_light(directional_light);
    return directional_light;
}

std::string DirectionalLight::get_name() const
{
    std::string const name = typeid(decltype(*this)).name();
    return name.substr(6);
}

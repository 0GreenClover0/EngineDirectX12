#include <GLFW/glfw3.h>
#include <glm/gtc/random.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/vec2.hpp>
#include <imgui.h>

#include "AK/AK.h"
#include "Collider2D.h"
#include "Entity.h"
#include "Globals.h"
#include "IceBound.h"
#include "Input.h"
#include "Model.h"
#include "ResourceManager.h"

std::shared_ptr<IceBound> IceBound::create()
{
    return std::make_shared<IceBound>(AK::Badge<IceBound> {});
}

IceBound::IceBound(AK::Badge<IceBound>)
{
}

void IceBound::awake()
{
    set_can_tick(true);
}

void IceBound::update()
{
}

void IceBound::draw_editor()
{
    Component::draw_editor();

    if (entity->get_component<Collider2D>() != nullptr && entity->get_component<Model>() != nullptr)
    {
        std::array const ice_types = {"Rectangle", "Circle"};
        i32 current_item_index = static_cast<i32>(m_type);
        if (ImGui::Combo("Collider Type", &current_item_index, ice_types.data(), ice_types.size()))
        {
            m_type = static_cast<ColliderType2D>(current_item_index);
            if (m_type == ColliderType2D::Rectangle)
            {
                if (m_size > 4)
                {
                    m_size = 4;
                }
            }
        }

        u32 constexpr min_size = 1;

        if (m_type == ColliderType2D::Circle)
        {
            u32 constexpr max_size = 7;
            if (ImGui::SliderScalar("Size: ", ImGuiDataType_U32, &m_size, &min_size, &max_size))
            {
                if (m_size < 1 || m_size > max_size)
                {
                    Debug::log("Error: Wrong ice bound size " + std::to_string(m_size), DebugType::Error);
                    return;
                }

                entity->get_component<Model>()->model_path = "./res/models/iceIslands/c_" + std::to_string(m_size) + ".gltf";
                entity->get_component<Model>()->reprepare();
            }
        }
        else if (m_type == ColliderType2D::Rectangle)
        {
            u32 constexpr max_size = 4;
            if (ImGui::SliderScalar("Size: ", ImGuiDataType_U32, &m_size, &min_size, &max_size))
            {
                if (m_size < 1 || m_size > max_size)
                {
                    Debug::log("Error: Wrong ice bound size " + std::to_string(m_size), DebugType::Error);
                    return;
                }

                entity->get_component<Model>()->model_path = "./res/models/iceIslands/s_" + std::to_string(m_size) + ".gltf";
                entity->get_component<Model>()->reprepare();
            }
        }
    }
    else
    {
        if (ImGui::Button("Add components"))
        {
            auto const standard_shader = ResourceManager::get_instance().load_shader("./res/shaders/lit.hlsl", "./res/shaders/lit.hlsl");
            auto const standard_material = Material::create(standard_shader);

            entity->add_component(Collider2D::create(glm::vec2(1.0f, 1.0f), false));
            entity->add_component(Model::create("./res/models/iceIslands/c_1.gltf", standard_material));
        }
    }
}

#include "Renderer.h"

#include <array>
#include <format>
#include <iostream>
#include <glad/glad.h>
#include <glm/gtx/rotate_vector.hpp>

#include "AK.h"
#include "Camera.h"
#include "Entity.h"

std::shared_ptr<Renderer> Renderer::create()
{
    // We can't use make_shared here because the constructor is private.
    // https://stackoverflow.com/questions/56735974/access-private-constructor-from-public-static-member-function-using-shared-ptr-i
    auto renderer = std::shared_ptr<Renderer>(new Renderer());

    assert(instance == nullptr);

    set_instance(renderer);

    return renderer;
}

void Renderer::initialize()
{
    frustum_culling_shader = Shader::create("./res/shaders/frustum_culling.glsl");

    // TODO: GPU instancing on one material currently supports only the first mesh that was bound to the material.
    size_t max_size = 0;
    for (auto const& material : instanced_materials)
    {
        material->model_matrices.reserve(material->drawables.size());
        material->bounding_boxes = std::vector<BoundingBoxShader>(material->drawables.size());

        if (max_size < material->drawables.size())
            max_size = material->drawables.size();
    }

    glGenBuffers(1, &gpu_instancing_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpu_instancing_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, max_size * sizeof(glm::mat4), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gpu_instancing_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glGenBuffers(1, &bounding_boxes_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bounding_boxes_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, max_size * sizeof(BoundingBoxShader), nullptr, GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bounding_boxes_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glGenBuffers(1, &visible_instances_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, visible_instances_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, max_size * sizeof(GLuint), nullptr, GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, visible_instances_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void Renderer::register_shader(std::shared_ptr<Shader> const& shader)
{
    if (!shaders_map.contains(shader))
    {
        shaders_map.insert(std::make_pair(shader, std::vector<std::shared_ptr<Drawable>> {}));
    }
}

void Renderer::unregister_shader(std::shared_ptr<Shader> const& shader)
{
    shaders_map.erase(shader);
}

void Renderer::register_drawable(std::shared_ptr<Drawable> const& drawable)
{
    if (drawable->material->is_gpu_instanced)
    {
        drawable->material->drawables.emplace_back(drawable);
    }

    if (drawable->render_order == 0 && !drawable->material->is_gpu_instanced)
    {
        assert(shaders_map.contains(drawable->material->shader));

        shaders_map[drawable->material->shader].emplace_back(drawable);
    }
    else if (drawable->render_order != 0)
    {
        custom_render_order_drawables.insert(std::make_pair(drawable->render_order, drawable));
    }
    else if (drawable->material->is_gpu_instanced)
    {
        // TODO: Add create() function for materials and register them there.
        if (auto const iterator = std::ranges::find(instanced_materials, drawable->material); iterator == instanced_materials.end())
        {
            instanced_materials.emplace_back(drawable->material);
        }
    }
}

void Renderer::register_light(std::shared_ptr<Light> const& light)
{
    if (auto const potential_point_light = std::dynamic_pointer_cast<PointLight>(light))
    {
        point_lights.emplace_back(potential_point_light);
    }
    else if (auto const potential_spot_light = std::dynamic_pointer_cast<SpotLight>(light))
    {
        spot_lights.emplace_back(potential_spot_light);
    }
    else if (auto const potential_directional_light = std::dynamic_pointer_cast<DirectionalLight>(light))
    {
        // Don't assert here
        assert(directional_light == nullptr);

        directional_light = potential_directional_light;
    }

    lights.emplace_back(light);
}

void Renderer::unregister_light(std::shared_ptr<Light> const& light)
{
    AK::swap_and_erase(lights, light);
}

void Renderer::render() const
{
    // Premultiply projection and view matrices
    glm::mat4 const projection_view = Camera::get_main_camera()->projection * Camera::get_main_camera()->get_view_matrix();
    glm::mat4 const projection_view_no_translation = Camera::get_main_camera()->projection * glm::mat4(glm::mat3(Camera::get_main_camera()->get_view_matrix()));

    for (auto const& [shader, drawables] : shaders_map)
    {
        shader->use();

        set_shader_uniforms(shader, projection_view, projection_view_no_translation);
        
        for (auto const& drawable : drawables)
        {
            // Could be beneficial to sort drawables per entities as well
            shader->set_mat4("PVM", projection_view * drawable->entity->transform->get_model_matrix());
            shader->set_mat4("model", drawable->entity->transform->get_model_matrix());

            shader->set_vec3("material.color", glm::vec3(drawable->material->color.x, drawable->material->color.y, drawable->material->color.z));
            shader->set_float("material.specular", drawable->material->specular);
            shader->set_float("material.shininess", drawable->material->shininess);

            shader->set_float("radiusMultiplier", drawable->material->radius_multiplier);
            shader->set_int("sector_count", drawable->material->sector_count);
            shader->set_int("stack_count", drawable->material->stack_count);

            drawable->draw();
        }
    }

    for (auto const& material : instanced_materials)
    {
        if (material->drawables.size() == 0)
            continue;

        auto const first_drawable = material->first_drawable;
        auto const shader = first_drawable->material->shader;

        material->model_matrices.clear();
        material->model_matrices.reserve(material->drawables.size());

        // TODO: Adjust bounding boxes on GPU?
        for (uint32_t i = 0; i < material->drawables.size(); ++i)
        {
            if (material->drawables[i]->entity->transform->needs_bounding_box_adjusting)
            {
                material->drawables[i]->bounds = material->first_drawable->get_adjusted_bounding_box(material->drawables[i]->entity->transform->get_model_matrix());
                material->bounding_boxes[i] = BoundingBoxShader(material->drawables[i]->bounds);
                material->drawables[i]->entity->transform->needs_bounding_box_adjusting = false;
            }
        }

        frustum_culling_shader->use();

        // Set frustum planes
        auto frustum_planes = Camera::get_main_camera()->get_frustum_planes();

        for (uint32_t i = 0; i < 6; ++i)
        {
            frustum_culling_shader->set_vec4(std::format("frustumPlanes[{}]", i), frustum_planes[i]);
        }

        // Send bounding boxes
        // TODO: Batch them with all other existing objects and send only once
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, bounding_boxes_ssbo);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, material->bounding_boxes.size() * sizeof(BoundingBoxShader), material->bounding_boxes.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        // Run frustum culling
        glDispatchCompute((material->drawables.size() / 1024) + 1, 1, 1);

        // Wait for SSBO write to complete
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // Read visible_instances SSBO which has value of 1 when a corresponding object is visible and 0 if it is not visible
        auto* visible_instances = new GLuint[material->drawables.size()];
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, visible_instances_ssbo);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, material->drawables.size() * sizeof(GLuint), visible_instances);

        // TODO: Pass visible instances directly to the shader by a shared SSBO. Might not actually be beneficial?
        for (uint32_t i = 0; i < material->drawables.size(); ++i)
        {
            if (visible_instances[i] == 1)
            {
                material->model_matrices.emplace_back(material->drawables[i]->entity->transform->get_model_matrix());
            }
        }

        shader->use();

        set_shader_uniforms(shader, projection_view, projection_view_no_translation);

        shader->set_vec3("material.color", glm::vec3(first_drawable->material->color.x, first_drawable->material->color.y, first_drawable->material->color.z));
        shader->set_float("material.specular", first_drawable->material->specular);
        shader->set_float("material.shininess", first_drawable->material->shininess);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpu_instancing_ssbo);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, material->model_matrices.size() * sizeof(glm::mat4), material->model_matrices.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        first_drawable->draw_instanced(material->model_matrices.size());
    }

    for (auto const& [render_order, drawable] : custom_render_order_drawables)
    {
        auto const drawable_locked = drawable.lock();

        if (drawable_locked == nullptr)
            continue;

        auto const shader = drawable_locked->material->shader;
        shader->use();

        set_shader_uniforms(drawable_locked->material->shader, projection_view, projection_view_no_translation);

        shader->set_mat4("PVM", projection_view * drawable_locked->entity->transform->get_model_matrix());
        shader->set_mat4("model", drawable_locked->entity->transform->get_model_matrix());

        shader->set_vec3("material.color", glm::vec3(drawable_locked->material->color.x, drawable_locked->material->color.y, drawable_locked->material->color.z));
        shader->set_float("material.specular", drawable_locked->material->specular);
        shader->set_float("material.shininess", drawable_locked->material->shininess);

        shader->set_float("radiusMultiplier", drawable_locked->material->radius_multiplier);
        shader->set_int("sector_count", drawable_locked->material->sector_count);
        shader->set_int("stack_count", drawable_locked->material->stack_count);

        drawable_locked->draw();
    }
}

void Renderer::set_shader_uniforms(std::shared_ptr<Shader> const& shader, glm::mat4 const& projection_view, glm::mat4 const& projection_view_no_translation) const
{
    // TODO: Check if shader was already processed and don't perform any operations?
    // TODO: Ultimately we would probably want to cache the uniform location instead of retrieving them by name

    shader->set_vec3("cameraPosition", Camera::get_main_camera()->position);
    shader->set_mat4("PV", projection_view);
    shader->set_mat4("PVnoTranslation", projection_view_no_translation);

    // TODO: Choose only the closest lights

    int32_t enabled_light_count = 0;
    for (uint32_t i = 0; i < point_lights.size(); ++i)
    {
        if (!point_lights[i]->enabled)
            continue;

        std::string light_element = std::format("pointLights[{}].", enabled_light_count);
        shader->set_vec3(light_element + "position", point_lights[i]->entity->transform->get_local_position());

        shader->set_vec3(light_element + "ambient", point_lights[i]->ambient);
        shader->set_vec3(light_element + "diffuse", point_lights[i]->diffuse);
        shader->set_vec3(light_element + "specular", point_lights[i]->specular);

        shader->set_float(light_element + "constant", point_lights[i]->constant);
        shader->set_float(light_element + "linear", point_lights[i]->linear);
        shader->set_float(light_element + "quadratic", point_lights[i]->quadratic);

        enabled_light_count++;
    }

    shader->set_int("pointLightCount", enabled_light_count > max_point_lights ? max_point_lights : enabled_light_count);

    enabled_light_count = 0;
    for (uint32_t i = 0; i < spot_lights.size(); ++i)
    {
        if (!spot_lights[i]->enabled)
            continue;

        std::string light_element = std::format("spotLights[{}].", enabled_light_count);
        shader->set_vec3(light_element + "position", spot_lights[i]->entity->transform->get_local_position());
        shader->set_vec3(light_element + "direction", spot_lights[i]->entity->transform->get_forward());

        shader->set_vec3(light_element + "ambient", spot_lights[i]->ambient);
        shader->set_vec3(light_element + "diffuse", spot_lights[i]->diffuse);
        shader->set_vec3(light_element + "specular", spot_lights[i]->specular);

        shader->set_float(light_element + "cutOff", spot_lights[i]->cut_off);
        shader->set_float(light_element + "outerCutOff", spot_lights[i]->outer_cut_off);

        shader->set_float(light_element + "constant", spot_lights[i]->constant);
        shader->set_float(light_element + "linear", spot_lights[i]->linear);
        shader->set_float(light_element + "quadratic", spot_lights[i]->quadratic);

        enabled_light_count++;
    }

    shader->set_int("spotLightCount", enabled_light_count > max_spot_lights ? max_spot_lights : enabled_light_count);

    bool const directional_light_on = directional_light != nullptr && directional_light->enabled;
    if (directional_light_on)
    {
        shader->set_vec3("directionalLight.direction", directional_light->entity->transform->get_forward());

        shader->set_vec3("directionalLight.ambient", directional_light->ambient);
        shader->set_vec3("directionalLight.diffuse", directional_light->diffuse);
        shader->set_vec3("directionalLight.specular", directional_light->specular);
    }

    shader->set_bool("directionalLightOn", directional_light_on);
}

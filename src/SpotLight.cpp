#include "SpotLight.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include "Renderer.h"
#include "RendererDX11.h"
#include "Entity.h"

std::shared_ptr<SpotLight> SpotLight::create()
{
    auto spot_light = std::make_shared<SpotLight>(AK::Badge<SpotLight> {});
    spot_light->set_up_shadow_mapping();
    return spot_light;
}

void SpotLight::draw_editor()
{
    Light::draw_editor();
}

void SpotLight::set_render_target_for_shadow_mapping() const
{
    auto const renderer = RendererDX11::get_instance_dx11();
    renderer->get_device_context()->OMSetRenderTargets(1, &renderer->g_emptyRenderTargetView, m_shadow_depth_stencil_view);
    renderer->get_device_context()->ClearDepthStencilView(m_shadow_depth_stencil_view, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

glm::mat4 SpotLight::get_projection_view_matrix()
{
    if (m_last_model_matrix != entity->transform->get_model_matrix() && entity != nullptr)
    {
        auto const renderer = RendererDX11::get_instance_dx11();

        m_last_model_matrix = entity->transform->get_model_matrix();

        float const aspect = renderer->SHADOW_MAP_SIZE / renderer->SHADOW_MAP_SIZE;

        glm::mat4 const projection_matrix = glm::perspective(glm::radians(90.0f), aspect, m_near_plane, m_far_plane);
        glm::mat4 const view_matrix = glm::lookAt(entity->transform->get_position(), entity->transform->get_position() + entity->transform->get_forward(), entity->transform->get_up());
        m_projection_view_matrix = projection_matrix * view_matrix;
    }

    return m_projection_view_matrix;
}

void SpotLight::set_up_shadow_mapping()
{
    auto renderer = RendererDX11::get_instance_dx11();

    D3D11_TEXTURE2D_DESC shadow_texture_desc = {};
    shadow_texture_desc.Width = static_cast<u32>(renderer->SHADOW_MAP_SIZE);
    shadow_texture_desc.Height = static_cast<u32>(renderer->SHADOW_MAP_SIZE);
    shadow_texture_desc.MipLevels = 1;
    shadow_texture_desc.ArraySize = 1;
    shadow_texture_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    shadow_texture_desc.SampleDesc.Count = 1;
    shadow_texture_desc.SampleDesc.Quality = 0;
    shadow_texture_desc.Usage = D3D11_USAGE_DEFAULT;
    shadow_texture_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    shadow_texture_desc.CPUAccessFlags = 0;
    shadow_texture_desc.MiscFlags = 0;

    HRESULT hr = renderer->get_device()->CreateTexture2D(&shadow_texture_desc, nullptr, &m_shadow_texture);
    assert(SUCCEEDED(hr));

    D3D11_DEPTH_STENCIL_VIEW_DESC shadow_depth_stencil_view_desc = {};
    shadow_depth_stencil_view_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    shadow_depth_stencil_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    shadow_depth_stencil_view_desc.Texture2D.MipSlice = 0;
    shadow_depth_stencil_view_desc.Flags = 0;

    hr = renderer->get_device()->CreateDepthStencilView(m_shadow_texture, &shadow_depth_stencil_view_desc, &m_shadow_depth_stencil_view);
    assert(SUCCEEDED(hr));

    D3D11_SHADER_RESOURCE_VIEW_DESC shadow_shader_resource_view_desc = {};
    shadow_shader_resource_view_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    shadow_shader_resource_view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    shadow_shader_resource_view_desc.Texture2D.MostDetailedMip = 0;
    shadow_shader_resource_view_desc.Texture2D.MipLevels = shadow_texture_desc.MipLevels;

    hr = renderer->get_device()->CreateShaderResourceView(m_shadow_texture, &shadow_shader_resource_view_desc, &m_shadow_shader_resource_view);
    assert(SUCCEEDED(hr));
}

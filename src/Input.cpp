#include "Input.h"

void Input::set_input(std::shared_ptr<Input> const& input_system)
{
    input = input_system;

    glfwSetCursorPosCallback(input->window->get_glfw_window(), &input->mouse_callback);
    glfwSetWindowFocusCallback(input->window->get_glfw_window(), &input->focus_callback);
}

Input::Input(std::shared_ptr<Window> const& window) : window(window)
{
}

bool Input::is_key_down(int const key) const
{
    return glfwGetKey(window->get_glfw_window(), key) == GLFW_PRESS;
}

void Input::mouse_callback(GLFWwindow* window, double const x, double const y)
{
    if (input->mouse_callback_impl != nullptr)
        input->mouse_callback_impl(x, y);
}

void Input::focus_callback(GLFWwindow* window, int const focused)
{
    if (input->focus_callback_impl != nullptr)
        input->focus_callback_impl(focused);
}

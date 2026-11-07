#include "EditorExtension.hpp"

#include "imgui-cmake/Header/imgui.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace Ge
{

struct EditorExtensionRegistry::Impl
{
    struct Window
    {
        std::string id;
        std::string menu;
        std::string label;
        bool requiresProject = true;
        bool open = false;
        WindowFactory factory;
        std::unique_ptr<EditorWindowExtension> instance;
    };

    struct Inspector
    {
        std::string id;
        float height = 200.f;
        InspectorFactory factory;
        std::unique_ptr<EditorScriptInspectorExtension> instance;
    };

    std::set<std::string> ids;
    std::map<std::string, Window> windows;
    std::map<std::string, Inspector> inspectors;

    std::vector<Window*> orderedWindows()
    {
        std::vector<Window*> ordered;
        ordered.reserve(windows.size());
        for (auto& entry : windows)
            ordered.push_back(&entry.second);
        std::sort(ordered.begin(), ordered.end(), [](const Window* a, const Window* b)
        {
            if (a->menu != b->menu)
                return a->menu < b->menu;
            return a->id < b->id;
        });
        return ordered;
    }
};

EditorExtensionRegistry::EditorExtensionRegistry()
    : m_impl(std::make_unique<Impl>())
{
}

EditorExtensionRegistry::~EditorExtensionRegistry() = default;

EditorExtensionRegistry& EditorExtensionRegistry::instance()
{
    static EditorExtensionRegistry registry;
    return registry;
}

bool EditorExtensionRegistry::registerWindowFactory(std::string id, std::string menu,
                                                   std::string label, bool requiresProject,
                                                   WindowFactory factory)
{
    if (id.empty() || label.empty() || !factory || m_impl->ids.count(id) != 0)
        return false;

    Impl::Window window;
    window.id = id;
    window.menu = std::move(menu);
    window.label = std::move(label);
    window.requiresProject = requiresProject;
    window.factory = std::move(factory);
    m_impl->windows.emplace(id, std::move(window));
    m_impl->ids.insert(std::move(id));
    return true;
}

bool EditorExtensionRegistry::registerInspectorFactory(std::string id, std::string scriptType,
                                                      float preferredHeight,
                                                      InspectorFactory factory)
{
    if (id.empty() || scriptType.empty() || !factory || !std::isfinite(preferredHeight)
        || preferredHeight <= 0.f || m_impl->ids.count(id) != 0
        || m_impl->inspectors.count(scriptType) != 0)
        return false;

    Impl::Inspector inspector;
    inspector.id = id;
    inspector.height = preferredHeight;
    inspector.factory = std::move(factory);
    m_impl->inspectors.emplace(std::move(scriptType), std::move(inspector));
    m_impl->ids.insert(std::move(id));
    return true;
}

void EditorExtensionRegistry::renderMenu(const EditorExtensionContext& context)
{
    if (!ImGui::GetCurrentContext())
        return;

    const auto windows = m_impl->orderedWindows();
    auto renderItem = [&](Impl::Window& window)
    {
        ImGui::PushID(window.id.c_str());
        if (ImGui::MenuItem(window.label.c_str(), nullptr, window.open,
                            !window.requiresProject || context.hasProject))
            window.open = !window.open;
        ImGui::PopID();
    };

    for (std::size_t first = 0; first < windows.size();)
    {
        std::size_t end = first + 1;
        while (end < windows.size() && windows[end]->menu == windows[first]->menu)
            ++end;

        if (windows[first]->menu.empty())
        {
            for (std::size_t i = first; i < end; ++i)
                renderItem(*windows[i]);
        }
        else
        {
            bool enabled = false;
            for (std::size_t i = first; i < end; ++i)
                enabled = enabled || !windows[i]->requiresProject || context.hasProject;
            if (ImGui::BeginMenu(windows[first]->menu.c_str(), enabled))
            {
                for (std::size_t i = first; i < end; ++i)
                    renderItem(*windows[i]);
                ImGui::EndMenu();
            }
        }
        first = end;
    }
}

void EditorExtensionRegistry::renderWindows(const EditorExtensionContext& context)
{
    if (!ImGui::GetCurrentContext())
        return;

    // std::map keeps entry addresses stable; the snapshot also makes extension
    // registration during a callback apply only on the following frame.
    const auto windows = m_impl->orderedWindows();
    for (auto* window : windows)
    {
        if (!window->instance && window->open
            && (!window->requiresProject || context.hasProject))
            window->instance = window->factory();
        if (window->instance)
            window->instance->render(context, &window->open);
    }
}

bool EditorExtensionRegistry::hasInspector(const std::string& scriptType) const
{
    return m_impl->inspectors.count(scriptType) != 0;
}

float EditorExtensionRegistry::inspectorHeight(const std::string& scriptType) const
{
    const auto found = m_impl->inspectors.find(scriptType);
    return found == m_impl->inspectors.end() ? 200.f : found->second.height;
}

bool EditorExtensionRegistry::renderInspector(const EditorExtensionContext& context,
                                            const std::string& scriptType, std::string& json)
{
    const auto found = m_impl->inspectors.find(scriptType);
    if (found == m_impl->inspectors.end() || !ImGui::GetCurrentContext())
        return false;

    auto& inspector = found->second;
    if (!inspector.instance)
        inspector.instance = inspector.factory();
    if (!inspector.instance)
        return false;

    ImGui::PushID(inspector.id.c_str());
    inspector.instance->render(context, json);
    ImGui::PopID();
    return true;
}

}

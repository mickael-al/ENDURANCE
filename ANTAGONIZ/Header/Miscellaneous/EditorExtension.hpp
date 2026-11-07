#ifndef GE_EDITOR_EXTENSION_HPP
#define GE_EDITOR_EXTENSION_HPP

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace Ge
{

struct EditorExtensionContext
{
    std::filesystem::path projectRoot;
    bool hasProject = false;
};

class EditorWindowExtension
{
public:
    virtual ~EditorWindowExtension() = default;
    virtual void render(const EditorExtensionContext& context, bool* open) = 0;
};

class EditorScriptInspectorExtension
{
public:
    virtual ~EditorScriptInspectorExtension() = default;
    virtual void render(const EditorExtensionContext& context, std::string& json) = 0;
};

// Register extensions from their implementation files. Registration stores a
// factory only; extension objects are constructed on their first UI invocation.
// UI rendering and any later registration take place on the editor thread.
class EditorExtensionRegistry
{
public:
    static EditorExtensionRegistry& instance();

    template<class T>
    bool registerWindow(std::string id, std::string menu, std::string label,
                        bool requiresProject = true)
    {
        static_assert(std::is_base_of<EditorWindowExtension, T>::value,
                      "Editor windows must implement EditorWindowExtension");
        static_assert(std::is_default_constructible<T>::value,
                      "Editor windows must have a public default constructor");
        return registerWindowFactory(std::move(id), std::move(menu), std::move(label),
            requiresProject, []() -> std::unique_ptr<EditorWindowExtension>
            {
                return std::make_unique<T>();
            });
    }

    template<class T>
    bool registerInspector(std::string id, std::string scriptType,
                           float preferredHeight = 200.f)
    {
        static_assert(std::is_base_of<EditorScriptInspectorExtension, T>::value,
                      "Editor inspectors must implement EditorScriptInspectorExtension");
        static_assert(std::is_default_constructible<T>::value,
                      "Editor inspectors must have a public default constructor");
        return registerInspectorFactory(std::move(id), std::move(scriptType),
            preferredHeight, []() -> std::unique_ptr<EditorScriptInspectorExtension>
            {
                return std::make_unique<T>();
            });
    }

    // Call renderMenu within the editor's main menu bar. Each menu is drawn
    // once, and entries are ordered by menu and registration ID.
    void renderMenu(const EditorExtensionContext& context);
    // Existing instances are called even while closed or without a project so
    // that an extension can finish a close/save confirmation or change projects.
    void renderWindows(const EditorExtensionContext& context);

    bool hasInspector(const std::string& scriptType) const;
    float inspectorHeight(const std::string& scriptType) const;
    // True means the registered inspector handled this invocation, regardless
    // of whether it changed JSON. False allows the ordinary JSON editor fallback.
    bool renderInspector(const EditorExtensionContext& context,
                         const std::string& scriptType, std::string& json);

    EditorExtensionRegistry(const EditorExtensionRegistry&) = delete;
    EditorExtensionRegistry& operator=(const EditorExtensionRegistry&) = delete;

private:
    using WindowFactory = std::function<std::unique_ptr<EditorWindowExtension>()>;
    using InspectorFactory = std::function<std::unique_ptr<EditorScriptInspectorExtension>()>;
    struct Impl;

    EditorExtensionRegistry();
    ~EditorExtensionRegistry();

    bool registerWindowFactory(std::string id, std::string menu, std::string label,
                               bool requiresProject, WindowFactory factory);
    bool registerInspectorFactory(std::string id, std::string scriptType,
                                  float preferredHeight, InspectorFactory factory);

    std::unique_ptr<Impl> m_impl;
};

}

// Internal linkage permits the same header to be seen by several translation
// units without Factory/Behaviour symbols or CLEAR_REGISTRY dependencies.
#define GE_EDITOR_EXTENSION_JOIN_IMPL(A, B) A##B
#define GE_EDITOR_EXTENSION_JOIN(A, B) GE_EDITOR_EXTENSION_JOIN_IMPL(A, B)
#define REGISTER_EDITOR_WINDOW(TYPE, ...) \
    namespace { [[maybe_unused]] const bool \
        GE_EDITOR_EXTENSION_JOIN(geEditorWindowRegistration_, __COUNTER__) = \
        ::Ge::EditorExtensionRegistry::instance().registerWindow<TYPE>(__VA_ARGS__); }
#define REGISTER_EDITOR_INSPECTOR(TYPE, ...) \
    namespace { [[maybe_unused]] const bool \
        GE_EDITOR_EXTENSION_JOIN(geEditorInspectorRegistration_, __COUNTER__) = \
        ::Ge::EditorExtensionRegistry::instance().registerInspector<TYPE>(__VA_ARGS__); }

#endif

#include "EditorExtension.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// This declaration and registration are also compiled in a second translation
// unit, as they would be if an extension were registered from a shared header.
extern int extensionHeaderConstructors;
extern int extensionHeaderRenders;
extern int extensionHeaderInspectorConstructors;
extern int extensionHeaderInspectorRenders;
struct HeaderExtension final : Ge::EditorWindowExtension {
    HeaderExtension() { ++extensionHeaderConstructors; }
    void render(const Ge::EditorExtensionContext&, bool*) override {
        ++extensionHeaderRenders;
    }
};
REGISTER_EDITOR_WINDOW(HeaderExtension, "tests.header", "Shared", "Header", false)

struct HeaderInspector final : Ge::EditorScriptInspectorExtension {
    HeaderInspector() { ++extensionHeaderInspectorConstructors; }
    void render(const Ge::EditorExtensionContext&, std::string&) override {
        ++extensionHeaderInspectorRenders;
    }
};
REGISTER_EDITOR_INSPECTOR(HeaderInspector, "tests.header.inspector", "HeaderScript")

#ifndef EDITOR_EXTENSIONS_SECOND_TU
int extensionHeaderConstructors = 0;
int extensionHeaderRenders = 0;
int extensionHeaderInspectorConstructors = 0;
int extensionHeaderInspectorRenders = 0;

namespace {
int alwaysConstructors = 0;
int alwaysRenders = 0;
int requiredConstructors = 0;
int requiredRenders = 0;
int guardConstructors = 0;
int guardRenders = 0;
int guardCloseRequests = 0;
int inspectorConstructors = 0;
int inspectorRenders = 0;
bool guardDirty = true;
bool alwaysLastOpen = false;
bool guardLastOpen = false;
Ge::EditorExtensionContext alwaysLastContext;
Ge::EditorExtensionContext guardLastContext;
std::string* inspectorLastJson = nullptr;

struct AlwaysExtension final : Ge::EditorWindowExtension {
    AlwaysExtension() { ++alwaysConstructors; }
    void render(const Ge::EditorExtensionContext& context, bool* open) override {
        ++alwaysRenders;
        alwaysLastOpen = *open;
        alwaysLastContext = context;
    }
};
struct RequiredExtension final : Ge::EditorWindowExtension {
    RequiredExtension() { ++requiredConstructors; }
    void render(const Ge::EditorExtensionContext&, bool*) override {
        ++requiredRenders;
    }
};
struct GuardExtension final : Ge::EditorWindowExtension {
    GuardExtension() { ++guardConstructors; }
    void render(const Ge::EditorExtensionContext& context, bool* open) override {
        ++guardRenders;
        guardLastContext = context;
        if (!*open && guardDirty) {
            ++guardCloseRequests;
            *open = true; // A dirty document may reject a requested close.
        }
        guardLastOpen = *open;
    }
};
struct InspectorExtension final : Ge::EditorScriptInspectorExtension {
    InspectorExtension() { ++inspectorConstructors; }
    void render(const Ge::EditorExtensionContext& context, std::string& json) override {
        ++inspectorRenders;
        inspectorLastJson = &json;
        json = context.projectRoot.generic_u8string();
    }
};

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Item {
    std::string label;
    ImRect bounds;
    bool disabled;
};
std::vector<Item> items;
Ge::EditorExtensionContext context;

void frame() {
    items.clear();
    ImGui::NewFrame();
    if (ImGui::BeginMainMenuBar()) {
        Ge::EditorExtensionRegistry::instance().renderMenu(context);
        ImGui::EndMainMenuBar();
    }
    Ge::EditorExtensionRegistry::instance().renderWindows(context);
    ImGui::Render();
}

Item item(const char* label) {
    const auto found = std::find_if(items.begin(), items.end(), [&](const Item& value) {
        return value.label == label;
    });
    if (found == items.end()) throw std::runtime_error(std::string("Missing ImGui item: ") + label);
    return *found;
}

void click(const char* label) {
    const ImVec2 point = item(label).bounds.GetCenter();
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(point.x, point.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
}

void openAlpha() {
    // A previous click can leave Alpha open. Only click when its items are absent.
    if (std::none_of(items.begin(), items.end(), [](const Item& value) {
            return value.label == "Always tools";
        })) click("Alpha");
}
} // namespace

// Observe Dear ImGui's actual item output through its supported test hooks.
// No renderer, graphics context, or replica of the registry is needed.
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID, const ImRect&, const ImGuiLastItemData*) {}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext* current, ImGuiID, const char* label,
                                  ImGuiItemStatusFlags) {
    if (label && *label) {
        items.push_back({label, current->LastItemData.Rect,
                         (current->LastItemData.InFlags & ImGuiItemFlags_Disabled) != 0});
    }
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

int main() {
    try {
        auto& registry = Ge::EditorExtensionRegistry::instance();
        expect(extensionHeaderConstructors == 0, "Header registration eagerly constructed a window");
        expect(extensionHeaderInspectorConstructors == 0,
               "Header registration eagerly constructed an inspector");
        expect(!registry.registerWindow<HeaderExtension>("tests.header", "Shared", "Header"),
               "Header macro did not register its window identifier");
        expect(registry.hasInspector("HeaderScript") &&
                   std::fabs(registry.inspectorHeight("HeaderScript") - 200.f) < 0.01f,
               "Header macro lost its default inspector height");
        expect(registry.registerWindow<AlwaysExtension>("alpha.z", "Alpha", "Always tools", false),
               "Window registration failed");
        expect(registry.registerWindow<GuardExtension>("alpha.a", "Alpha", "Guarded"),
               "Guard registration failed");
        expect(registry.registerWindow<RequiredExtension>("alpha.r", "Alpha", "Project tools"),
               "Required window registration failed");
        expect(registry.registerWindow<RequiredExtension>("zulu.unopened", "Zulu", "Unopened"),
               "Unopened window registration failed");
        expect(registry.registerInspector<InspectorExtension>("tests.inspector", "ExampleScript", 340.f),
               "Inspector registration failed");
        expect(!registry.registerWindow<AlwaysExtension>("tests.inspector", "Alpha", "Collision"),
               "Window reused an inspector's global identifier");
        expect(!registry.registerInspector<InspectorExtension>("alpha.z", "OtherScript"),
               "Inspector reused a window's global identifier");
        expect(!registry.registerInspector<InspectorExtension>("other.inspector", "ExampleScript"),
               "Duplicate script inspector was accepted");
        expect(!registry.registerInspector<InspectorExtension>("invalid.height", "InvalidHeight", 0.f),
               "Zero inspector height was accepted");
        expect(!registry.registerInspector<InspectorExtension>("invalid.infinity", "InvalidInfinity",
                   std::numeric_limits<float>::infinity()), "Nonfinite inspector height was accepted");
        expect(alwaysConstructors == 0 && guardConstructors == 0 && inspectorConstructors == 0,
               "Registration created extension instances");
        expect(registry.hasInspector("ExampleScript") && !registry.hasInspector("UnknownScript"),
               "Inspector lookup confused registered and missing types");
        expect(std::fabs(registry.inspectorHeight("ExampleScript") - 340.f) < 0.01f,
               "Registered inspector height was lost");
        std::string json = "original";
        expect(!registry.renderInspector(context, "ExampleScript", json) && json == "original",
               "Inspector ran without an ImGui context");
        registry.renderMenu(context);
        registry.renderWindows(context);
        std::cout << "PASS: multi-translation-unit registration, lazy factories and collision rejection\n";

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1024.f, 768.f);
        io.DeltaTime = 1.f / 60.f;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ImGui::GetCurrentContext()->TestEngineHookItems = true;
        context = {std::filesystem::path("project-one"), false};
        frame();
        frame();
        expect(alwaysConstructors == 0 && guardConstructors == 0 && requiredConstructors == 0 &&
                   extensionHeaderConstructors == 0,
               "Rendering unopened windows created extension instances");
        expect(item("Alpha").bounds.Min.x < item("Shared").bounds.Min.x &&
                   item("Shared").bounds.Min.x < item("Zulu").bounds.Min.x,
               "Menu groups were not sorted consistently");
        expect(std::count_if(items.begin(), items.end(), [](const Item& value) {
                   return value.label == "Alpha";
               }) == 1, "A menu group was rendered more than once");
        openAlpha();
        expect(item("Guarded").bounds.Min.y < item("Project tools").bounds.Min.y &&
                   item("Project tools").bounds.Min.y < item("Always tools").bounds.Min.y,
               "Items were not sorted by stable extension identifier");
        expect(item("Guarded").disabled && item("Project tools").disabled &&
                   !item("Always tools").disabled,
               "Project availability did not control menu items");
        click("Project tools");
        expect(requiredConstructors == 0, "Disabled project tool was instantiated");
        openAlpha();
        click("Always tools");
        expect(alwaysConstructors == 1 && alwaysRenders > 0 && alwaysLastOpen &&
                   !alwaysLastContext.hasProject,
               "An available tool could not open before a project was loaded");
        openAlpha();
        click("Always tools");
        expect(alwaysConstructors == 1 && !alwaysLastOpen,
               "A window was recreated or skipped on close");
        const int closedRenders = alwaysRenders;
        frame();
        expect(alwaysRenders == closedRenders + 1, "Closed window callbacks stopped running");
        std::cout << "PASS: grouped menus, deterministic order, availability and open/close dispatch\n";

        context = {std::filesystem::path("project-two"), true};
        frame();
        openAlpha();
        expect(!item("Guarded").disabled, "Project tools stayed disabled after loading a project");
        click("Guarded");
        expect(guardConstructors == 1 && guardLastOpen, "Guarded tool could not open");
        openAlpha();
        click("Guarded");
        expect(guardCloseRequests > 0 && guardLastOpen,
               "A dirty window did not receive or reject its close request");
        guardDirty = false;
        openAlpha();
        click("Guarded");
        expect(!guardLastOpen, "Confirmed close did not close the window");
        context = {std::filesystem::path("project-three"), false};
        const int previousGuardRenders = guardRenders;
        frame();
        expect(guardRenders == previousGuardRenders + 1 &&
                   guardLastContext.projectRoot == context.projectRoot && !guardLastContext.hasProject,
               "An existing window missed a closed-project context change");
        std::cout << "PASS: dirty-close guard and context changes reach existing windows\n";

        ImGui::NewFrame();
        ImGui::Begin("Inspector host");
        expect(!registry.renderInspector(context, "UnknownScript", json) && json == "original",
               "An unknown script consumed the fallback JSON inspector");
        expect(registry.renderInspector(context, "ExampleScript", json) &&
                   json == "project-three" && inspectorLastJson == &json,
               "Inspector did not receive the original JSON by reference");
        const int previousInspectorRenders = inspectorRenders;
        expect(registry.renderInspector(context, "ExampleScript", json) &&
                   inspectorRenders == previousInspectorRenders + 1 && inspectorConstructors == 1,
               "Inspector lookup depended on whether JSON changed or recreated its instance");
        expect(registry.renderInspector(context, "HeaderScript", json) &&
                   extensionHeaderInspectorConstructors == 1 && extensionHeaderInspectorRenders == 1,
               "An inspector registered from several translation units could not render");
        ImGui::End();
        ImGui::Render();
        std::cout << "PASS: inspector fallback, JSON reference, height and persistent instance\n";
        ImGui::DestroyContext();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        return 1;
    }
}
#endif

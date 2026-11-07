#include "EditorInput.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
enum Widget { Button, Image, Slider, Drag, Text, Multiline, Numeric, WidgetCount };
std::array<ImRect, WidgetCount> bounds;
char text[128] = "name";
char multiline[512] = "{\"value\":1}";
float slider = 0.5f;
float drag = 1.f;
float numeric = 2.f;
bool editorVisible = true;
Ge::EditorInputCapture capture;
Ge::EditorInputCapture imageCapture;
bool imageActivated = false;
bool imageWithL = false;
bool insertPressed = false;

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void remember(Widget widget) {
    bounds[widget] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

void frame() {
    imageActivated = false;
    imageWithL = false;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20.f, 20.f));
    ImGui::SetNextWindowSize(ImVec2(600.f, 650.f));
    ImGui::Begin("Editor input", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Button("Action", ImVec2(140.f, 32.f));
    remember(Button);
    imageActivated = ImGui::ImageButton("Texture", (ImTextureID)1, ImVec2(72.f, 48.f));
    remember(Image);
    // Match shortcuts handled inside a texture button before later text fields
    // are submitted: capture must not depend on those fields being redrawn.
    imageCapture = Ge::editorInputCapture(editorVisible);
    imageWithL = imageActivated && ImGui::IsKeyDown(ImGuiKey_L);
    ImGui::SetNextItemWidth(280.f);
    ImGui::SliderFloat("Slider", &slider, 0.f, 1.f);
    remember(Slider);
    ImGui::SetNextItemWidth(280.f);
    ImGui::DragFloat("Drag", &drag);
    remember(Drag);
    ImGui::SetNextItemWidth(280.f);
    ImGui::InputText("Text", text, sizeof(text));
    remember(Text);
    ImGui::InputTextMultiline("JSON", multiline, sizeof(multiline), ImVec2(280.f, 100.f));
    remember(Multiline);
    ImGui::SetNextItemWidth(280.f);
    ImGui::InputFloat("Numeric", &numeric);
    remember(Numeric);
    capture = Ge::editorInputCapture(editorVisible);
    insertPressed = ImGui::IsKeyPressed(ImGuiKey_Insert, false);
    ImGui::End();
    ImGui::Render();
}

void moveTo(Widget widget) {
    const ImVec2 point = bounds[widget].GetCenter();
    ImGui::GetIO().AddMousePosEvent(point.x, point.y);
    frame();
}

void press(Widget widget) {
    moveTo(widget);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame();
}

void release() {
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame();
}

void click(Widget widget) {
    press(widget);
    release();
}

void expectTyping(Widget widget, const char* message) {
    click(widget);
    expect(capture.textInput && capture.lockCamera, message);
    // Insert remains a real input event, but production capture identifies its
    // text owner so editor shortcuts can defer to the field.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Insert, true);
    frame();
    expect(insertPressed && capture.textInput,
           "Insert event lost its text-input capture");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Insert, false);
    frame();
}

void expectMouseWidget(Widget widget, const char* message) {
    press(widget);
    expect(!capture.textInput && capture.lockCamera, message);
    release();
    expect(!capture.textInput && !capture.lockCamera,
           "Mouse release required an extra frame or click to unlock the camera");
}
} // namespace

int main() {
    try {
        expect(!Ge::editorInputCapture(true).textInput &&
                   !Ge::editorInputCapture(true).lockCamera,
               "Capture without an ImGui context was not empty");
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1024.f, 768.f);
        io.DeltaTime = 1.f / 60.f;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        frame();
        frame();
        expect(!capture.textInput && !capture.lockCamera,
               "Idle visible editor captured inputs");
        std::cout << "PASS: idle and missing-context input capture\n";

        expectMouseWidget(Button, "Button blocked editor keyboard shortcuts");
        expectMouseWidget(Image, "Texture button blocked editor keyboard shortcuts");
        expectMouseWidget(Slider, "Slider drag blocked editor keyboard shortcuts");
        expectMouseWidget(Drag, "Numeric drag blocked editor keyboard shortcuts");
        io.AddKeyEvent(ImGuiKey_Insert, true);
        frame();
        expect(insertPressed && !capture.textInput,
               "Insert remained captured after releasing a widget");
        io.AddKeyEvent(ImGuiKey_Insert, false);
        frame();
        std::cout << "PASS: buttons, texture clicks and drags release capture immediately\n";

        expectTyping(Text, "Single-line text did not capture shortcuts");
        io.AddInputCharacter('l');
        frame();
        expect(std::string(text).find('l') != std::string::npos && capture.textInput,
               "Text field did not receive typing while editor shortcuts were captured");
        expectTyping(Multiline, "Multiline text did not capture shortcuts");
        expectTyping(Numeric, "Numeric text input did not capture shortcuts");
        click(Button);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        press(Drag);
        expect(capture.textInput && capture.lockCamera,
               "Ctrl-click numeric drag did not capture its temporary text editor");
        release();
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame();
        expect(capture.textInput, "Temporary numeric input lost capture on mouse release");
        std::cout << "PASS: text, JSON and numeric text editing capture shortcuts\n";

        click(Text);
        frame();
        expect(io.WantTextInput && capture.textInput,
               "Typing fixture did not establish ImGui's delayed text-input request");
        io.AddKeyEvent(ImGuiKey_L, true);
        press(Image);
        expect(io.WantTextInput && !imageCapture.textInput && !capture.textInput,
               "Leaving a field waited for stale WantTextInput to clear");
        release();
        expect(imageWithL && !imageCapture.textInput && !capture.lockCamera,
               "L plus texture click failed on the first click after text editing");
        io.AddKeyEvent(ImGuiKey_L, false);
        frame();
        std::cout << "PASS: first texture click with L works despite delayed WantTextInput\n";

        click(Text);
        expect(capture.textInput && capture.lockCamera, "Hidden-editor fixture was not typing");
        editorVisible = false;
        frame();
        expect(!capture.textInput && !capture.lockCamera,
               "Hidden editor retained text or camera capture");
        editorVisible = true;
        frame();
        expect(capture.textInput && capture.lockCamera,
               "Visible text input no longer captured shortcuts");
        press(Slider);
        editorVisible = false;
        frame();
        expect(!capture.textInput && !capture.lockCamera,
               "Hidden editor retained an active drag's camera lock");
        release();
        ImGui::DestroyContext();
        expect(!Ge::editorInputCapture(true).lockCamera,
               "Destroyed context retained camera capture");
        std::cout << "PASS: hiding the editor releases text and drag capture\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        return 1;
    }
}

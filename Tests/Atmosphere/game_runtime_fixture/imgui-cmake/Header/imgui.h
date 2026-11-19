#pragma once
// ImGui ownership is outside this headless renderer test. These stubs do not
// modify settings; production serialization and every render path still run.
using ImGuiColorEditFlags=int;
constexpr int ImGuiColorEditFlags_HDR=1,ImGuiColorEditFlags_Float=2;
constexpr int ImGuiDataType_U32=0;
namespace ImGui
{
    template<class... T> inline void TextUnformatted(T...) {}
    template<class... T> inline void Text(T...) {}
    template<class... T> inline void TextWrapped(T...) {}
    template<class... T> inline void SetTooltip(T...) {}
    template<class... T> inline void SameLine(T...) {}
    template<class... T> inline void PushID(T...) {}
    inline void PopID() {}
    inline void EndCombo() {}
    inline void TreePop() {}
    inline bool IsItemHovered() { return false; }
    template<class... T> inline bool Button(T...) { return false; }
    template<class... T> inline bool BeginCombo(T...) { return false; }
    template<class... T> inline bool Selectable(T...) { return false; }
    template<class... T> inline bool TreeNode(T...) { return false; }
    template<class... T> inline bool SliderInt(T...) { return false; }
    template<class... T> inline bool SliderFloat(T...) { return false; }
    template<class... T> inline bool DragFloat(T...) { return false; }
    template<class... T> inline bool DragFloat2(T...) { return false; }
    template<class... T> inline bool DragFloat3(T...) { return false; }
    template<class... T> inline bool DragInt(T...) { return false; }
    template<class... T> inline bool ColorEdit3(T...) { return false; }
    template<class... T> inline bool Checkbox(T...) { return false; }
    template<class... T> inline bool Combo(T...) { return false; }
    template<class... T> inline bool InputScalar(T...) { return false; }
}

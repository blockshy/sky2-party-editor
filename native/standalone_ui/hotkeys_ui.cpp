// 快捷键编辑器只编辑候选值；真实绑定由共同配置事务在“保存”成功后发布。
// 控件全部位于 Main 并通过现有导航登记，选择键位不录制真实按键，因此
// 手柄的 A/B、LB/RB、LT/RT 始终保留统一窗口含义，也不会误触另一个 Mod。
#include "hotkeys.h"
#include "ui.h"
#include "ui_layout.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace sky2solo {
namespace {
const char* Text(int language, const char* zh, const char* tw, const char* ja, const char* en,
    const char* de, const char* fr, const char* es, const char* ko) {
    const char* values[]{zh, tw, ja, en, de, fr, es, ko};
    return values[std::clamp(language, 0, 7)];
}
const char* ActionName(const HotkeyDefinition& info, int language) {
    const auto id = info.id ? info.id : "";
    if (!std::strcmp(id, "chest.open")) return Text(language, "宝箱窗口", "寶箱視窗", "宝箱ウィンドウ", "Chest window", "Truhenfenster", "Fenêtre des coffres", "Ventana de cofres", "보물 상자 창");
    if (!std::strcmp(id, "chest.cycle_mode")) return Text(language, "切换统计口径", "切換統計口徑", "集計方式の切り替え", "Collection mode", "Zählweise wechseln", "Mode de comptage", "Modo de recuento", "집계 방식 전환");
    if (!std::strcmp(id, "chest.toggle_markers")) return Text(language, "暂停／恢复宝箱标记", "暫停／恢復寶箱標記", "宝箱マーカーの切り替え", "Toggle chest markers", "Truhenmarkierungen", "Activer les repères", "Alternar marcadores", "상자 표시 전환");
    if (!std::strcmp(id, "chest.toggle_map_reveal")) return Text(language, "地图全显", "地圖全顯", "マップ全表示", "Reveal map", "Karte aufdecken", "Révéler la carte", "Revelar mapa", "지도 전체 표시");
    if (!std::strcmp(id, "chest.toggle_unvisited")) return Text(language, "未到访传送点", "未到訪傳送點", "未訪問の移動先", "Unvisited travel points", "Unbesuchte Reiseziele", "Destinations non visitées", "Destinos no visitados", "미방문 이동 지점");
    if (!std::strcmp(id, "chest.open_travel")) return Text(language, "打开传送页", "開啟傳送頁", "移動ページを開く", "Open travel page", "Reiseseite öffnen", "Ouvrir les voyages", "Abrir viajes", "이동 페이지 열기");
    if (!std::strcmp(id, "party.open")) return Text(language, "队伍窗口", "隊伍視窗", "パーティーウィンドウ", "Party window", "Gruppenfenster", "Fenêtre d’équipe", "Ventana de grupo", "파티 창");
    if (!std::strcmp(id, "highlight.open")) return Text(language, "高亮窗口", "高亮視窗", "表示設定ウィンドウ", "Highlight window", "Markierungsfenster", "Fenêtre des repères", "Ventana de resaltado", "강조 설정 창");
    if (!std::strcmp(id, "highlight.toggle")) return Text(language, "高亮总开关", "高亮總開關", "強調表示の切り替え", "Toggle highlights", "Markierungen umschalten", "Activer le surlignage", "Alternar resaltado", "강조 표시 전환");
    return info.label ? info.label : id;
}
const char* Unbound(int language) {
    return Text(language, "未设置", "未設定", "未設定", "Unbound", "Nicht belegt", "Non défini", "Sin asignar", "미설정");
}
std::string Message(const std::string& message, int language) {
    // 引擎保留稳定的诊断文本供测试和日志使用；页面在此翻译校验原因。
    // 冲突详情保留注册的模块/动作名称，不能仅用“保存失败”掩盖实际占用者。
    if (message.rfind("Conflict: ", 0) == 0)
        return std::string(Text(language, "已被占用：", "已被占用：", "使用中：", "Already used: ", "Bereits belegt: ", "Déjà utilisé : ", "Ya está en uso: ", "이미 사용 중: ")) + message.substr(10);
    if (message == "The window must keep a keyboard shortcut.")
        return Text(language, "窗口入口必须保留一个键盘快捷键。", "視窗入口必須保留一個鍵盤快捷鍵。", "ウィンドウを開くキーボード設定は必須です。", message.c_str(), "Eine Tastaturbelegung zum Öffnen muss erhalten bleiben.", "Gardez un raccourci clavier pour ouvrir cette fenêtre.", "Conserva un atajo de teclado para abrir esta ventana.", "창을 여는 키보드 단축키는 반드시 유지해야 합니다.");
    if (message == "This keyboard key is reserved." || message == "Unsupported modifier.")
        return Text(language, "该键位被保留，请从列表选择其他按键。", "此按鍵已保留，請從清單選擇其他按鍵。", "このキーは予約済みです。別のキーを選択してください。", "This key is reserved. Choose another key from the list.", "Diese Taste ist reserviert. Eine andere Taste wählen.", "Cette touche est réservée. Choisissez-en une autre.", "Esta tecla está reservada. Elige otra.", "예약된 키입니다. 목록에서 다른 키를 선택하세요.");
    if (message == "Alt+F4 is reserved by Windows." || message == "Ctrl+Alt+Delete is reserved by Windows." || message == "Alt+Backspace is reserved.")
        return Text(language, "该组合属于系统快捷键，无法分配给 Mod。", "此組合屬於系統快捷鍵，無法分配給 Mod。", "この組み合わせはシステム用のため設定できません。", "This combination is reserved for the system.", "Diese Kombination ist für das System reserviert.", "Cette combinaison est réservée au système.", "Esta combinación está reservada para el sistema.", "시스템에서 사용하는 조합은 지정할 수 없습니다.");
    if (message == "Use View plus one supported controller button.")
        return Text(language, "请选择 View 加一个手柄按钮。", "請選擇 View 加一個手柄按鈕。", "View＋ボタン1つを選択してください。", message.c_str(), "View plus eine Controller-Taste wählen.", "Choisissez View et un bouton de manette.", "Elige View y un botón del mando.", "View와 버튼 하나를 선택하세요.");
    if (message == "Shortcut settings changed externally; retry.")
        return Text(language, "配置文件已被其他程序修改，本次未保存，请重试。", "設定檔已被其他程式修改，本次未儲存，請重試。", "設定ファイルが外部で変更されました。保存を再試行してください。", message.c_str(), "Die Datei wurde extern geändert. Erneut versuchen.", "Le fichier a été modifié ailleurs. Réessayez.", "El archivo cambió externamente. Inténtalo de nuevo.", "다른 프로그램이 설정 파일을 변경했습니다. 다시 시도하세요.");
    if (message == "Shortcut registry is unavailable." || message == "Shortcut module is not registered." || message == "Unknown action.")
        return Text(language, "快捷键服务不可用，本次未保存，请重新启动游戏。", "快捷鍵服務無法使用，本次未儲存，請重新啟動遊戲。", "キー設定サービスを利用できません。ゲームを再起動してください。", "Shortcut service unavailable. Restart the game.", "Tastendienst nicht verfügbar. Spiel neu starten.", "Service de raccourcis indisponible. Relancez le jeu.", "Servicio de atajos no disponible. Reinicia el juego.", "단축키 서비스를 사용할 수 없습니다. 게임을 다시 시작하세요.");
    if (message == "Cannot read shortcuts.ini." || message == "Cannot read the complete shortcuts.ini." ||
        message == "Cannot write shortcut settings." || message == "Cannot replace shortcut settings." || message == "Cannot save shortcut settings.")
        return Text(language, "无法读写 shortcuts.ini，快捷键未更新。请检查文件权限或占用后重试。", "無法讀寫 shortcuts.ini，快捷鍵未更新。請檢查檔案權限或占用後重試。", "shortcuts.ini を読み書きできません。権限や使用状況を確認してください。", "Cannot access shortcuts.ini. Shortcuts are unchanged; check permissions and retry.", "Kein Zugriff auf shortcuts.ini. Rechte prüfen und erneut versuchen.", "Accès à shortcuts.ini impossible. Vérifiez les droits et réessayez.", "No se puede acceder a shortcuts.ini. Revisa los permisos e inténtalo de nuevo.", "shortcuts.ini에 접근할 수 없습니다. 권한을 확인하고 다시 시도하세요.");
    // 启动修复信息可能包含多条动作与最终备用入口。逐段翻译结构，保留键位
    // 和原始动作名，使玩家能够按提示重新打开窗口并显式保存修复后的配置。
    std::string result = message;
    struct Translation { const char* source; const char* zh; const char* tw; };
    constexpr Translation translations[]{
        {"Shortcut directory is unavailable or unsafe.", "快捷键目录不可用或不安全。", "快捷鍵目錄無法使用或不安全。"},
        {"shortcuts.ini is not a supported plain file.", "shortcuts.ini 必须为普通文件。", "shortcuts.ini 必須為一般檔案。"},
        {"shortcuts.ini must use UTF-8 text.", "shortcuts.ini 必须为 UTF-8 文本。", "shortcuts.ini 必須為 UTF-8 文字。"},
        {"shortcuts.ini is too large.", "shortcuts.ini 文件过大。", "shortcuts.ini 檔案過大。"},
        {"Unsupported shortcut schema; defaults are active.", "快捷键配置版本不受支持，正在使用默认设置。", "快捷鍵設定版本不支援，正在使用預設值。"},
        {"Invalid shortcut: ", "无效快捷键：", "無效快捷鍵："},
        {". Defaults used. ", "。已采用默认值。", "。已採用預設值。"},
        {"Adjusted ", "已调整 ", "已調整 "},
        {"Conflict: ", "已被占用：", "已被占用："},
        {"Window shortcut: ", "当前窗口入口：", "目前視窗入口："},
    };
    if (language == 0 || language == 1) for (const auto& entry : translations) {
        const auto target = language == 0 ? entry.zh : entry.tw;
        size_t position = 0;
        while ((position = result.find(entry.source, position)) != std::string::npos) {
            result.replace(position, std::strlen(entry.source), target); position += std::strlen(target);
        }
    }
    return result;
}
std::string KeyboardLabel(const HotkeyBinding& binding, int language) {
    return binding.key ? HotkeyKeyboardText(binding) : Unbound(language);
}
std::string PadLabel(const HotkeyBinding& binding, int language) {
    const char* direction = nullptr;
    switch (binding.pad) {
    case XINPUT_GAMEPAD_DPAD_UP: direction = Text(language, "十字键上", "十字鍵上", "十字キー上", "D-pad Up", "Steuerkreuz oben", "Croix haut", "Cruceta arriba", "십자키 위"); break;
    case XINPUT_GAMEPAD_DPAD_DOWN: direction = Text(language, "十字键下", "十字鍵下", "十字キー下", "D-pad Down", "Steuerkreuz unten", "Croix bas", "Cruceta abajo", "십자키 아래"); break;
    case XINPUT_GAMEPAD_DPAD_LEFT: direction = Text(language, "十字键左", "十字鍵左", "十字キー左", "D-pad Left", "Steuerkreuz links", "Croix gauche", "Cruceta izquierda", "십자키 왼쪽"); break;
    case XINPUT_GAMEPAD_DPAD_RIGHT: direction = Text(language, "十字键右", "十字鍵右", "十字キー右", "D-pad Right", "Steuerkreuz rechts", "Croix droite", "Cruceta derecha", "십자키 오른쪽"); break;
    default: break;
    }
    if (direction) return std::string("View + ") + direction;
    return binding.pad ? HotkeyPadText(binding) : Unbound(language);
}
void LoadSelected(HotkeyEditorState& state, const HotkeySnapshot& snapshot) {
    state.selected = snapshot.count ? std::min(state.selected, snapshot.count - 1) : 0;
    state.draft = snapshot.count ? snapshot.bindings[state.selected] : HotkeyBinding{};
    state.revision = snapshot.revision;
    state.keyPicker = state.padPicker = false;
}
std::vector<uint16_t> KeyboardChoices() {
    // 常用功能键放在前面，其余合法键再按虚拟键序列补入。合法范围由引擎
    // 单点定义，UI 不另造一套可保存但不能触发的键位白名单。
    std::vector<uint16_t> result;
    for (uint16_t key = VK_F1; key <= VK_F24; ++key)
        if (ValidHotkeyKey(key)) result.push_back(key);
    for (uint16_t key = 1; key < 256; ++key)
        if ((key < VK_F1 || key > VK_F24) && ValidHotkeyKey(key)) result.push_back(key);
    return result;
}
void DrawKeyboardPicker(HotkeyEditorState& state, int language, float height) {
    const auto& ui = *UiApi();
    if (!state.keyPicker) return;
    ui.begin_child("keyboard_choices", height);
    if (ui.selectable("none", Unbound(language), state.draft.key == 0)) {
        state.draft.key = 0; state.draft.modifiers = 0; state.keyPicker = false;
    }
    static const auto choices = KeyboardChoices();
    for (const auto key : choices) {
        const auto id = std::to_string(key);
        const auto label = HotkeyKeyName(key);
        if (ui.selectable(id.c_str(), label.c_str(), state.draft.key == key)) {
            state.draft.key = key; state.keyPicker = false;
        }
    }
    ui.end_child();
}
void DrawPadPicker(HotkeyEditorState& state, int language, float height) {
    const auto& ui = *UiApi();
    if (!state.padPicker) return;
    ui.begin_child("pad_choices", height);
    if (ui.selectable("none", Unbound(language), state.draft.pad == 0)) {
        state.draft.pad = 0; state.padPicker = false;
    }
    constexpr WORD buttons[]{XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_LEFT,
        XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B,
        XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB};
    for (const auto button : buttons) {
        if (!ValidHotkeyPad(button)) continue;
        const auto id = std::to_string(button);
        const auto label = PadLabel(HotkeyBinding{0, 0, button}, language);
        if (ui.selectable(id.c_str(), label.c_str(), state.draft.pad == button)) {
            state.draft.pad = button; state.padPicker = false;
        }
    }
    ui.end_child();
}
}

void ResetHotkeyEditor(HotkeyEditorState& state) noexcept { state = HotkeyEditorState{}; }

void DrawHotkeySettings(HotkeyEditorState& state, int language) {
    const auto& ui = *UiApi();
    const auto snapshot = ReadHotkeys();
    if (!snapshot.count) {
        layout::Status(Text(language, "快捷键尚未初始化。", "快捷鍵尚未初始化。", "キー設定はまだ初期化されていません。", "Shortcuts are not initialized.", "Tastenbelegung noch nicht bereit.", "Les raccourcis ne sont pas initialisés.", "Los atajos no están inicializados.", "단축키가 아직 초기화되지 않았습니다."), 2);
        return;
    }
    if (state.revision != snapshot.revision || state.selected >= snapshot.count) LoadSelected(state, snapshot);
    const auto draftAtFrameStart = state.draft;
    const auto* info = HotkeyInfo(state.selected);
    if (!info) return;
    const auto notice = HotkeyNotice();
    if (!notice.empty()) layout::Status(Message(notice, language).c_str(), 2);
    layout::Muted(Text(language,
        "先选动作，再选择按键。保存时检查所有已加载合作 Mod 的冲突；不会替换其他动作。",
        "先選動作，再選擇按鍵。儲存時檢查所有已載入合作 Mod 的衝突；不會取代其他動作。",
        "動作とキーを選びます。保存時に対応 Mod との重複を確認し、他の動作を上書きしません。",
        "Select an action and keys. Saving checks loaded cooperating Mods and never replaces another action.",
        "Aktion und Tasten wählen. Beim Speichern werden geladene Mods auf Konflikte geprüft; andere Aktionen bleiben erhalten.",
        "Choisissez une action et ses touches. L’enregistrement vérifie les conflits entre Mods compatibles chargés.",
        "Elige una acción y sus teclas. Al guardar se comprueban conflictos con otros Mods compatibles cargados.",
        "동작과 키를 선택하세요. 저장 시 실행 중인 호환 Mod와 충돌을 확인하며 다른 동작을 덮어쓰지 않습니다."));
    float viewportWidth = 0, viewportHeight = 0;
    ui.content_size(&viewportWidth, &viewportHeight);
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const float maximumListHeight = std::clamp(viewportHeight - 70 * scale, 110 * scale, 340 * scale);
    const float pickerHeight = std::clamp(viewportHeight * .5f, 100 * scale, 210 * scale);
    const bool dirtyBefore = state.draft != snapshot.bindings[state.selected];

    // 列数由实际内容宽度决定；动作列表使用 Main 剩余高度，编辑卡可推动 Main
    // 外层滚动。不会再次固定一个与实际窗口无关的低矮“选择动作”区域。
    const int columns = layout::Columns("hotkey_columns", 285);
    layout::BeginCard("hotkey_actions");
    layout::Section(Text(language, "选择动作", "選擇動作", "動作を選択", "Choose action", "Aktion wählen", "Choisir l’action", "Elegir acción", "동작 선택"), nullptr);
    // 用真实换行高度估算列表所需空间。队伍只有一个动作、高亮只有两个动作，
    // 无需占满整个 Main；宝箱动作较多时仍以剩余视口为上限并保留内层滚动。
    std::vector<std::string> actionLabels(snapshot.count);
    const auto& style = ImGui::GetStyle();
    const float rowWrap = std::max(1.0f, ImGui::GetContentRegionAvail().x - style.WindowPadding.x * 2 - 18 * scale - 2);
    float naturalHeight = style.WindowPadding.y * 2 + 2;
    for (size_t index = 0; index < snapshot.count; ++index) if (const auto* action = HotkeyInfo(index)) {
        actionLabels[index] = std::string(ActionName(*action, language)) + "\n" +
            KeyboardLabel(snapshot.bindings[index], language) + "  /  " + PadLabel(snapshot.bindings[index], language);
        naturalHeight += std::max(34 * scale, ImGui::CalcTextSize(actionLabels[index].c_str(), nullptr, false, rowWrap).y + 14 * scale) + style.ItemSpacing.y;
    }
    const float listHeight = std::min(maximumListHeight, std::max(64 * scale, naturalHeight));
    ui.begin_disabled(dirtyBefore);
    ui.begin_child("action_list", listHeight);
    for (size_t index = 0; index < snapshot.count; ++index) {
        const auto* action = HotkeyInfo(index);
        if (!action) continue;
        const auto id = std::to_string(index);
        const auto& label = actionLabels[index];
        if (ui.selectable(id.c_str(), label.c_str(), state.selected == index)) {
            state.selected = index; LoadSelected(state, snapshot); state.message.clear(); info = action;
        }
    }
    ui.end_child(); ui.end_disabled();
    if (dirtyBefore) layout::Muted(Text(language, "保存或撤销后可切换动作。", "儲存或撤銷後可切換動作。", "保存または取消後に動作を切り替えられます。", "Save or discard before changing actions.", "Vor dem Wechsel speichern oder verwerfen.", "Enregistrez ou annulez avant de changer d’action.", "Guarda o descarta antes de cambiar de acción.", "저장하거나 취소한 뒤 동작을 바꿀 수 있습니다."));
    layout::EndCard();
    if (columns == 2) layout::NextColumn();
    layout::BeginCard("hotkey_editor");
    layout::Section(ActionName(*info, language), nullptr);
    layout::Muted(info->opensWindow ? Text(language, "窗口入口必须保留一个键盘快捷键。", "視窗入口必須保留一個鍵盤快捷鍵。", "ウィンドウを開くキーボード設定は必須です。", "Keep a keyboard shortcut for opening this window.", "Eine Tastaturbelegung zum Öffnen muss erhalten bleiben.", "Gardez un raccourci clavier pour ouvrir cette fenêtre.", "Conserva un atajo de teclado para abrir esta ventana.", "창을 여는 키보드 단축키는 반드시 유지해야 합니다.") :
        Text(language, "仅在所有 Mod 窗口关闭时执行。", "僅在所有 Mod 視窗關閉時執行。", "すべての Mod ウィンドウが閉じている時のみ実行します。", "Runs only while all Mod windows are closed.", "Nur aktiv, wenn alle Mod-Fenster geschlossen sind.", "Active uniquement si toutes les fenêtres de Mod sont fermées.", "Solo actúa con todas las ventanas de Mod cerradas.", "모든 Mod 창이 닫혀 있을 때만 실행됩니다."));
    ui.text_wrapped(Text(language, "键盘主键", "鍵盤主鍵", "キーボードのキー", "Keyboard key", "Tastaturtaste", "Touche du clavier", "Tecla principal", "키보드 키"));
    const auto keyboard = state.draft.key ? HotkeyKeyName(state.draft.key) : Unbound(language);
    const auto chooseKey = keyboard + "  ▾";
    if (ui.button("choose_key", chooseKey.c_str())) { state.keyPicker = !state.keyPicker; state.padPicker = false; }
    DrawKeyboardPicker(state, language, pickerHeight);
    ui.begin_disabled(state.draft.key == 0);
    if (ImGui::BeginTable("modifiers", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
        const struct { const char* label; uint8_t bit; } modifiers[]{{"Ctrl", HotkeyCtrl}, {"Alt", HotkeyAlt}, {"Shift", HotkeyShift}};
        for (const auto& modifier : modifiers) {
            ImGui::TableNextColumn(); int32_t enabled = (state.draft.modifiers & modifier.bit) != 0;
            if (ui.checkbox(modifier.label, modifier.label, &enabled)) {
                if (enabled) state.draft.modifiers |= modifier.bit;
                else state.draft.modifiers &= static_cast<uint8_t>(~modifier.bit);
            }
        }
        ImGui::EndTable();
    }
    ui.end_disabled();
    ui.text_wrapped(Text(language, "手柄组合", "手柄組合", "コントローラー", "Controller chord", "Controller-Kombination", "Combinaison manette", "Combinación del mando", "컨트롤러 조합"));
    const auto choosePad = PadLabel(state.draft, language) + "  ▾";
    if (ui.button("choose_pad", choosePad.c_str())) { state.padPicker = !state.padPicker; state.keyPicker = false; }
    DrawPadPicker(state, language, pickerHeight);
    layout::Muted(Text(language, "手柄固定为 View＋一个按钮；方向、确认和滚动操作保持不变。", "手柄固定為 View＋一個按鈕；方向、確認與捲動操作保持不變。", "View＋ボタン1つを使用します。移動・決定・スクロールは変わりません。", "Use View + one button. Navigation, confirm and scrolling stay the same.", "View + eine Taste. Navigation, Bestätigung und Scrollen bleiben gleich.", "View + un bouton. Navigation, confirmation et défilement restent inchangés.", "View + un botón. La navegación, confirmación y desplazamiento no cambian.", "View + 버튼 하나를 사용합니다. 탐색, 확인, 스크롤 조작은 유지됩니다."));

    const bool dirty = state.draft != snapshot.bindings[state.selected];
    // 上次“已保存”的回执不能沿用到下一份草稿，否则玩家可能误认为新选择
    // 也已经写入配置。失败回执同样在修改候选后撤销，新的校验提示会随即更新。
    if (state.draft != draftAtFrameStart) { state.message.clear(); state.messageError = false; }
    const auto validation = ValidateHotkey(state.selected, state.draft);
    if (!validation.empty()) layout::Status(Message(validation, language).c_str(), 2);
    if (!state.message.empty()) layout::Status(Message(state.message, language).c_str(), state.messageError ? 2 : 0);
    ui.begin_disabled(!dirty || !validation.empty());
    if (ui.button("save_hotkey", Text(language, "保存快捷键", "儲存快捷鍵", "キー設定を保存", "Save shortcut", "Belegung speichern", "Enregistrer", "Guardar atajo", "단축키 저장"))) {
        std::string error;
        state.messageError = !CommitHotkey(state.selected, state.draft, error);
        if (!state.messageError) {
            LoadSelected(state, ReadHotkeys());
            state.message = Text(language, "已保存。松开按键后，新快捷键生效。", "已儲存。放開按鍵後，新快捷鍵生效。", "保存しました。キーを離すと新しい設定が有効になります。", "Saved. Release all inputs to use the new shortcut.", "Gespeichert. Alle Eingaben loslassen, um die neue Belegung zu verwenden.", "Enregistré. Relâchez les commandes pour utiliser le nouveau raccourci.", "Guardado. Suelta los controles para usar el nuevo atajo.", "저장했습니다. 모든 입력을 놓으면 새 단축키가 적용됩니다.");
        } else state.message = error;
    }
    ui.end_disabled(); ui.same_line(); ui.begin_disabled(!dirty);
    if (ui.button("discard_hotkey", Text(language, "撤销修改", "撤銷修改", "変更を取消", "Discard", "Verwerfen", "Annuler", "Descartar", "변경 취소"))) {
        LoadSelected(state, ReadHotkeys()); state.message.clear();
    }
    ui.end_disabled();
    if (ui.button("default_hotkeys", Text(language, "恢复本 Mod 默认键位", "恢復本 Mod 預設按鍵", "この Mod の初期設定に戻す", "Restore this Mod’s defaults", "Standard dieses Mods wiederherstellen", "Réinitialiser ce Mod", "Restaurar valores de este Mod", "이 Mod의 기본 키 복원"))) {
        std::string error;
        state.messageError = !RestoreDefaultHotkeys(error);
        if (!state.messageError) {
            LoadSelected(state, ReadHotkeys());
            state.message = Text(language, "已恢复默认键位。", "已恢復預設按鍵。", "初期設定に戻しました。", "Default shortcuts restored.", "Standardbelegung wiederhergestellt.", "Raccourcis par défaut restaurés.", "Atajos predeterminados restaurados.", "기본 단축키를 복원했습니다.");
        } else state.message = error;
    }
    layout::EndCard(); layout::EndColumns();
    layout::Muted(Text(language, "冲突检查覆盖当前已加载的合作 Mod；游戏本体和其他 Mod 的自定义键位无法自动读取。", "衝突檢查涵蓋目前已載入的合作 Mod；無法自動讀取遊戲本體及其他 Mod 的自訂按鍵。", "重複確認は対応 Mod が対象です。ゲーム本体や他の Mod の設定は自動取得できません。", "Checks cover loaded cooperating Mods. Game bindings and other Mods cannot be read automatically.", "Geprüft werden geladene kompatible Mods. Spielbelegungen und andere Mods können nicht automatisch gelesen werden.", "La vérification couvre les Mods compatibles chargés, pas les touches du jeu ou des autres Mods.", "Se comprueban los Mods compatibles cargados, no las teclas del juego ni de otros Mods.", "실행 중인 호환 Mod만 충돌을 확인합니다. 게임과 다른 Mod의 키 설정은 자동으로 읽을 수 없습니다."));
}
}

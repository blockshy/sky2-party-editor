// 统一中心的队伍页。宿主负责设备导航与模态输入，本文件只消费控件激活结果。
// 与旧独立面板一样，所有游戏变化只提交到 control_state / roster_service 安全队列。
#include "hub_panel.h"
#include "control_state.h"
#include "game_names.h"
#include "panel_input_policy.h"
#include "runtime.h"
#include "ui_text.h"
#include <sky2_ui.hpp>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <string>

namespace sky2party {
namespace {
constexpr Text featureLabels[]{Text::FeatureFixedMembers, Text::FeatureAnywhere,
    Text::FeatureUnavailable, Text::FeatureUnjoined};
panelinput::Confirmation confirmation;
size_t selected = 0;
uint64_t previousGeneration = 0;
bool active = false, submissionRejected = false;
enum class Page { Settings, Roster };
Page currentPage = Page::Settings;

// 帧结构只能按原 v1 前缀判定有效，不能以扩展后的 sizeof 拒绝旧宿主。
bool ValidFrame(const Sky2Frame* frame) noexcept {
    return frame && frame->size >= offsetof(Sky2Frame, controller) + sizeof(frame->controller);
}

// Hub 专用排版文案不加入独立版的 UI 目录，避免原 ASI/Standalone 页面被本次
// 视觉调整改变。八列顺序沿用 UiTextEntry，稳定控件 ID 与译文始终分离。
enum class HubText : size_t {
    Settings, SettingsHint, FixedHint, AnywhereHint, UnavailableHint, UnjoinedHint,
    RosterHint, Selected, PersistentChange, Details, ReadyToAdd, Preparation, Applied, Count
};
constexpr UiTextEntry hubTexts[]{
    {"功能设置", "機能設定", "Feature settings", "功能設定", "Funktionseinstellungen", "Paramètres des fonctions", "Ajustes de funciones", "기능 설정"},
    {"请求状态与实际生效状态分别显示。", "要求した設定と反映済みの状態を表示します。", "Requested settings and applied states are shown separately.", "分別顯示請求狀態與實際生效狀態。", "Gewünschte und bereits angewendete Einstellungen werden getrennt angezeigt.", "Les réglages demandés et les états appliqués sont affichés séparément.", "Se muestran por separado los ajustes solicitados y los estados aplicados.", "요청한 설정과 실제 적용 상태를 따로 표시합니다."},
    {"允许在游戏编成中调整固定队员。", "ゲームの編成画面で固定メンバーを変更できます。", "Allow fixed members to be changed in the game's party menu.", "允許在遊戲編成中調整固定隊員。", "Feste Mitglieder im Gruppenmenü des Spiels austauschen.", "Permet de changer les membres imposés dans le menu d’équipe du jeu.", "Permite cambiar miembros fijos en el menú de grupo del juego.", "게임 편성 화면에서 고정 멤버를 변경할 수 있습니다."},
    {"在可自由行动的探索场景使用编成。", "自由に行動できる探索中に編成を利用できます。", "Use party changes during free exploration.", "在可自由行動的探索場景使用編成。", "Gruppenänderungen während der freien Erkundung nutzen.", "Permet de modifier l’équipe pendant l’exploration libre.", "Permite cambiar el grupo durante la exploración libre.", "자유롭게 이동할 수 있는 탐색 중 편성을 사용할 수 있습니다."},
    {"允许选择当前暂不可选的后备成员。", "一時的に選べないリザーブメンバーを選択できます。", "Allow selection of temporarily unavailable standby members.", "允許選擇目前暫不可選的後備成員。", "Vorübergehend gesperrte Reservemitglieder auswählen.", "Permet de sélectionner les réservistes temporairement indisponibles.", "Permite seleccionar miembros en espera no disponibles temporalmente.", "일시적으로 선택할 수 없는 리저브 멤버를 선택할 수 있습니다."},
    {"允许逐人加入角色；每次操作均需两次确认。", "キャラを1人ずつ追加できます。毎回2回の確認が必要です。", "Add characters individually. Each action requires two confirmations.", "允許逐人加入角色；每次操作皆需兩次確認。", "Figuren einzeln hinzufügen. Jede Aktion erfordert zwei Bestätigungen.", "Ajoutez les personnages un par un. Chaque action demande deux confirmations.", "Añade personajes individualmente. Cada acción requiere dos confirmaciones.", "캐릭터를 한 명씩 추가할 수 있습니다. 매번 두 번 확인해야 합니다."},
    {"选择角色查看详情，再单独确认加入操作。", "キャラを選んで詳細を確認し、追加操作を別途確定してください。", "Select a character to view details, then confirm the addition separately.", "選擇角色查看詳情，再另外確認加入操作。", "Figur für Details auswählen, anschließend das Hinzufügen gesondert bestätigen.", "Sélectionnez un personnage pour voir les détails, puis confirmez son ajout séparément.", "Selecciona un personaje para ver los detalles y confirma su incorporación por separado.", "캐릭터를 선택해 정보를 확인한 뒤 추가 작업을 별도로 확인하세요."},
    {"所选角色", "選択中のキャラ", "Selected character", "所選角色", "Ausgewählte Figur", "Personnage sélectionné", "Personaje seleccionado", "선택한 캐릭터"},
    {"加入和补足会随存档保留；操作前请备份存档。", "追加や補完の結果はセーブに残ります。操作前にセーブをバックアップしてください。", "Adding and preparing characters persists in saved games. Back up your save first.", "加入與補足會隨存檔保留；操作前請備份存檔。", "Hinzufügen und Vorbereiten bleibt im Spielstand erhalten. Vorher den Spielstand sichern.", "L’ajout et la préparation sont conservés dans la sauvegarde. Faites-en une copie avant d’agir.", "Las incorporaciones y los datos añadidos se conservan al guardar. Haz antes una copia de la partida.", "추가 및 보충 결과는 저장 파일에 남습니다. 작업 전에 저장 파일을 백업하세요."},
    {"操作与补足说明", "操作と補完の詳細", "Action and preparation details", "操作與補足說明", "Details zu Aktion und Vorbereitung", "Détails de l’action et de la préparation", "Detalles de la acción y la preparación", "작업 및 보충 안내"},
    {"可以加入后备", "リザーブに追加できます", "Ready to add to standby", "可以加入後備", "Kann zur Reserve hinzugefügt werden", "Prêt à être ajouté en réserve", "Listo para añadir a miembros en espera", "리저브 목록에 추가 가능"},
    {"此角色需要补足缺失的基础装备或战技。", "このキャラには不足している基本装備やクラフトの補完が必要です。", "This character needs missing basic gear or Crafts to be supplied.", "此角色需要補足缺少的基本裝備或戰技。", "Bei dieser Figur müssen fehlende Grundausrüstung oder Techniken ergänzt werden.", "Ce personnage a besoin de l’équipement de base ou des techs manquants.", "Este personaje necesita el equipo básico o las técnicas que le falten.", "이 캐릭터는 부족한 기본 장비나 크래프트를 보충해야 합니다."},
    {"设置已应用。", "設定を反映しました。", "Settings applied.", "設定已套用。", "Einstellungen angewendet.", "Paramètres appliqués.", "Ajustes aplicados.", "설정이 적용되었습니다."},
};
static_assert(sizeof(hubTexts) / sizeof(hubTexts[0]) == static_cast<size_t>(HubText::Count));
const char* H(HubText id) noexcept {
    const auto& value = hubTexts[static_cast<size_t>(id)];
    return Localize(value.chinese, value.japanese, value.english, value.traditionalChinese,
        value.german, value.french, value.spanish, value.korean);
}

void UpdateLanguage() noexcept {
    if (!Sky2Hub_Host || !Sky2Hub_Host->language) return;
    // 公共 ABI 与旧模块枚举的中/繁/日/英顺序不同，必须按语义转换。
    constexpr Language languages[]{Language::Chinese, Language::TraditionalChinese, Language::Japanese,
        Language::English, Language::German, Language::French, Language::Spanish, Language::Korean};
    const auto index = Sky2Hub_Host->language();
    if (index >= 0 && index < 8) SetDisplayLanguage(languages[index]);
}

std::string Character(uint32_t id) {
    if (const auto* name = CharacterNameFor(CurrentLanguage(), id)) return name;
    char text[96]{};
    std::snprintf(text, sizeof(text), Tr(Text::CharacterIdFormat), id);
    return text;
}
const char* ControlText(ControlMessage message) noexcept {
    switch (message) {
    case ControlMessage::FixedMembersNeedRestoring: return Tr(Text::ControlFixedBlocked);
    case ControlMessage::CannotVerifyParties: return Tr(Text::ControlFixedUnknown);
    case ControlMessage::Applied: return H(HubText::Applied);
    case ControlMessage::AppliedNotSaved: return Tr(Text::ControlSaveFailed);
    case ControlMessage::ApplyFailed: return Tr(Text::ControlApplyFailed);
    case ControlMessage::ServiceError: return Tr(Text::ControlException);
    case ControlMessage::WaitingForExploration: return Tr(Text::ControlWaiting);
    case ControlMessage::WaitingForSave: return Tr(Text::ControlLoadWait);
    default: return Tr(Text::ControlDefault);
    }
}
const char* RoleAction(const RosterEntry& member) noexcept {
    return Tr(member.needsPreparation ? Text::PrepareAndAdd : member.hidden ? Text::RevealReserve : Text::AddReserve);
}
const char* RoleState(const RosterEntry& member) noexcept {
    if (member.hidden) return Tr(Text::HiddenReserve);
    if (member.inParty) return Tr(member.unavailable ? Text::Unavailable : Text::InRoster);
    return Tr(!member.initialized && !member.needsPreparation ? Text::DataNotReady : Text::Unjoined);
}
bool CanAdd(const ControlSnapshot& snapshot, const RosterEntry& member) noexcept {
    return snapshot.appliedStateKnown && (snapshot.appliedFeatures & FeatureUnjoined) &&
        snapshot.roster.ready && snapshot.roster.canEdit && member.canAdd &&
        snapshot.roster.pendingId == kNoRosterId;
}
void CancelConfirmation() noexcept {
    confirmation.Cancel();
    submissionRejected = false;
}
void DrawTabs(const Sky2Frame& frame) {
    const auto& ui = *Sky2Hub_Host->ui;
    const char* labels[]{H(HubText::Settings), Tr(Text::RoleSection)};
    constexpr const char* legacyIds[]{"party.tab.settings", "party.tab.roster"};
    constexpr int pageCount = static_cast<int>(std::size(legacyIds));
    const int previousPage = static_cast<int>(currentPage);
    int nextPage = previousPage;
    if (sky2ui::HasTabBar(ui)) {
        nextPage = sky2ui::TabBar(ui, "party.tab", labels, pageCount, previousPage);
    } else {
        // 旧宿主使用原按钮 ID；整栏返回单一目标，再统一取消危险操作确认。
        for (int index = 0; index < pageCount; ++index) {
            if (index && ui.same_line) ui.same_line();
            if (sky2ui::Tab(ui, legacyIds[index], labels[index], previousPage == index)) nextPage = index;
        }
    }
    if (frame.foreground && nextPage >= 0 && nextPage < pageCount && nextPage != previousPage) {
        currentPage = static_cast<Page>(nextPage);
        CancelConfirmation();
    }
}

float RosterListHeight(const Sky2UiApi& ui, const Sky2Frame& frame, int columns) {
    float width = 0, height = 0;
    sky2ui::ContentSize(ui, &width, &height);
    const float scale = std::isfinite(frame.scale) && frame.scale > 0 ? frame.scale : 1.0f;
    if (!std::isfinite(height) || height <= 0) return 260.0f * scale;
    // 尺寸查询返回固定 Main 视口的物理像素，不能再次整体乘缩放，也不能用
    // 自动高度卡片的剩余尺寸反推自身高度。宽屏给标题/提示/边距留出空间；
    // 窄屏把列表控制在视口约四成，为下方所选角色和二次确认入口保留位置。
    const float desired = columns >= 2 ? height - 160.0f * scale : height * .4f;
    return std::max(110.0f * scale, desired);
}
void SKY2_CALL OpenPage(void*) noexcept {
    CancelConfirmation();
    currentPage = Page::Settings;
    if (Sky2Hub_Host) Sky2Hub_Host->open_page(Sky2Hub_Host->owner);
}
void SKY2_CALL OpenRoster(void*) noexcept {
    CancelConfirmation();
    currentPage = Page::Roster;
    if (Sky2Hub_Host) Sky2Hub_Host->open_page(Sky2Hub_Host->owner);
}
void SKY2_CALL ToggleFeature(void* user) noexcept {
    const auto index = reinterpret_cast<uintptr_t>(user);
    if (index >= 4) return;
    CancelConfirmation();
    const auto snapshot = ReadControlSnapshot();
    // 包括关闭固定队员限制在内的所有动作，都必须走已有协调器的最终复核。
    RequestFeatureMask(snapshot.requestedFeatures ^ (1u << index));
}
const char* SKY2_CALL ActionTitle(void* user) noexcept {
    UpdateLanguage();
    const auto index = reinterpret_cast<uintptr_t>(user);
    return index < 4 ? Tr(featureLabels[index]) : Tr(Text::RoleSection);
}
int32_t SKY2_CALL ActionState(void* user) noexcept {
    const auto index=reinterpret_cast<uintptr_t>(user);if(index>=4)return -1;
    const auto snapshot=ReadControlSnapshot();const uint32_t bit=1u<<index;
    // 首页显示实际已应用状态；排队、事务失败或未知状态不能伪装成已开启。
    if(!snapshot.appliedStateKnown)return -1;
    if(snapshot.waitingForSafeState&&((snapshot.requestedFeatures^snapshot.appliedFeatures)&bit))return 2;
    return (snapshot.appliedFeatures&bit)?1:0;
}
const char* SKY2_CALL PageTitle(void*) noexcept {
    UpdateLanguage();
    return Localize("打开队伍编辑", "パーティー編集を開く", "Open Party Editor", "開啟隊伍編輯",
        "Gruppeneditor öffnen", "Ouvrir l’éditeur d’équipe", "Abrir editor de grupo", "파티 편집 열기");
}
void FixedWarning(const ControlSnapshot& snapshot) {
    const auto& guard = snapshot.fixedGuard;
    if (!snapshot.fixedGuardFresh || !guard.valid || !guard.riskCount) return;
    const auto* ui = Sky2Hub_Host->ui;
    sky2ui::BeginCard(*ui, "fixed.warning");
    sky2ui::Status(*ui, Tr(Text::FixedWarningPrefix), 2);
    for (uint32_t index = 0; index < std::min<uint32_t>(guard.memberCount, 4); ++index) {
        char text[256]{};
        const auto& member = guard.members[index];
        const auto name = Character(member.id);
        std::snprintf(text, sizeof(text), Tr(Text::FixedMemberFormat), member.partyIndex + 1, name.c_str());
        ui->text_wrapped(text);
    }
    if (guard.riskCount > 4) {
        char text[96]{};
        std::snprintf(text, sizeof(text), Tr(Text::FixedMoreFormat), guard.riskCount);
        ui->text(text);
    }
    ui->text_wrapped(Tr(Text::FixedRestoreNote));
    sky2ui::EndCard(*ui);
}

int ControlTone(ControlMessage message) noexcept {
    switch (message) {
    case ControlMessage::Applied: return 1;
    case ControlMessage::ApplyFailed: case ControlMessage::ServiceError: return 3;
    case ControlMessage::FixedMembersNeedRestoring: case ControlMessage::CannotVerifyParties:
    case ControlMessage::AppliedNotSaved: return 2;
    default: return 0;
    }
}
int ResultTone(RosterResult result) noexcept {
    switch (result) {
    case RosterResult::AddedReserve: case RosterResult::RevealedReserve:
    case RosterResult::PreparedAndAdded: case RosterResult::PreparedAndRevealed: return 1;
    case RosterResult::Queued: case RosterResult::None: return 0;
    case RosterResult::PreparationFailed: case RosterResult::PartiallyPrepared: return 3;
    default: return 2;
    }
}
}

bool RegisterHubActions() noexcept {
    if (!Sky2Hub_Host) return false;
    UpdateLanguage();
    const auto add = [](const char* id, const char* title, uint32_t flags, void* user,
                        void (SKY2_CALL *invoke)(void*), const char* (SKY2_CALL *titleNow)(void*),
                        int32_t (SKY2_CALL *stateNow)(void*)=nullptr) {
        const Sky2Action action{sizeof(Sky2Action), id, title, flags, user, invoke, nullptr, titleNow,stateNow};
        return Sky2Hub_Host->register_action(Sky2Hub_Host->owner, &action) != 0;
    };
    if (!add("party.open", PageTitle(nullptr), SKY2_ACTION_GLOBAL | SKY2_ACTION_FAVORITE,
        nullptr, &OpenPage, &PageTitle)) return false;
    constexpr const char* ids[]{"party.toggle_fixed", "party.toggle_anywhere", "party.toggle_unavailable", "party.toggle_unjoined"};
    for (uintptr_t index = 0; index < 4; ++index)
        if (!add(ids[index], Tr(featureLabels[index]), SKY2_ACTION_GLOBAL, reinterpret_cast<void*>(index),
            &ToggleFeature, &ActionTitle,&ActionState)) return false;
    // 角色快捷动作只导航，不预先武装确认，更不接受角色 ID 参数来绕过页面确认。
    return add("party.roster", Tr(Text::RoleSection), SKY2_ACTION_GLOBAL | SKY2_ACTION_CONFIRM,
        reinterpret_cast<void*>(4), &OpenRoster, &ActionTitle);
}

void SKY2_CALL HubVisibilityChanged(int32_t visible) noexcept {
    active = visible != 0;
    // 宿主切页、关闭或失焦时取消确认；再次打开必须由玩家重新确认两次。
    CancelConfirmation();
}

void SKY2_CALL TickHubPanel(const Sky2Frame* frame) noexcept {
    UpdateLanguage();
    const bool visible = ValidFrame(frame) && frame->foreground && frame->panel_open && frame->page_active;
    if (!visible || visible != active) CancelConfirmation();
    active = visible;
    const auto snapshot = ReadControlSnapshot();
    if (snapshot.roster.generation != previousGeneration || !snapshot.roster.canEdit) CancelConfirmation();
    previousGeneration = snapshot.roster.generation;
}

void SKY2_CALL DrawHubHeader(const Sky2Frame* frame) noexcept {
    if (!Sky2Hub_Host || !ValidFrame(frame) || !frame->panel_open || !frame->page_active) return;
    try {
        // Header 仅更新本地化和页签选择，不重复执行 Tick 或采样业务状态。
        UpdateLanguage();
        DrawTabs(*frame);
    } catch (...) {
        CancelConfirmation();
        Log("Hub Party header exception contained; pending confirmation canceled.");
    }
}

void SKY2_CALL DrawHubPanel(const Sky2Frame* frame) noexcept {
    if (!Sky2Hub_Host || !ValidFrame(frame) || !frame->panel_open || !frame->page_active) return;
    try {
        TickHubPanel(frame);
        const auto snapshot = ReadControlSnapshot();
        const auto* ui = Sky2Hub_Host->ui;
        char buffer[640]{};
        // 新宿主先画固定 Header，再显式标记当前帧。缺少尾部标记的旧宿主
        // 在 Main 内继续画原页签，不依赖时间戳或跨帧布尔状态猜测调用顺序。
        if (!sky2ui::HeaderDrawn(*frame)) {
            DrawTabs(*frame);
            if (ui->spacing) ui->spacing();
        }
        if (currentPage == Page::Settings) {
        // 宿主已显示模块名称和版本，此处从具体功能开始，避免堆叠重复标题。
        sky2ui::Section(*ui, H(HubText::Settings), H(HubText::SettingsHint));
        sky2ui::Columns(*ui, "feature.grid", 290.0f);
        constexpr HubText featureHints[]{HubText::FixedHint, HubText::AnywhereHint,
            HubText::UnavailableHint, HubText::UnjoinedHint};
        for (uint32_t index = 0; index < 4; ++index) {
            const auto bit = 1u << index;
            int32_t wanted = (snapshot.requestedFeatures & bit) != 0;
            const bool applied = (snapshot.appliedFeatures & bit) != 0;
            const auto id = "feature." + std::to_string(index);
            const auto card = id + ".card";
            sky2ui::BeginCard(*ui, card.c_str());
            if (ui->checkbox(id.c_str(), Tr(featureLabels[index]), &wanted) && frame->foreground) {
                CancelConfirmation();
                // 读取最新请求掩码，避免同一帧多个控件被激活时覆盖先前请求。
                const auto latest = ReadControlSnapshot().requestedFeatures;
                RequestFeatureMask(wanted ? latest | bit : latest & ~bit);
            }
            ui->text_wrapped(H(featureHints[index]));
            if (!snapshot.appliedStateKnown) {
                std::snprintf(buffer, sizeof(buffer), Tr(Text::UnknownStateFormat), Tr(wanted ? Text::Enable : Text::Disable));
                sky2ui::Status(*ui, buffer, 2);
            } else if ((wanted != 0) != applied) {
                std::snprintf(buffer, sizeof(buffer), Tr(Text::PendingStateFormat), Tr(applied ? Text::On : Text::Off),
                    Tr(wanted ? Text::Enable : Text::Disable));
                sky2ui::Status(*ui, buffer, 2);
            } else sky2ui::Status(*ui, Tr(applied ? Text::On : Text::Off), applied ? 1 : 0);
            sky2ui::EndCard(*ui);
            if (index != 3) sky2ui::NextColumn(*ui);
        }
        sky2ui::EndColumns(*ui);
        sky2ui::Status(*ui, ControlText(snapshot.message), ControlTone(snapshot.message));
        FixedWarning(snapshot);
        return;
        }
        sky2ui::Section(*ui, Tr(Text::RoleSection), H(HubText::RosterHint));
        if (!GameNamesReady(CurrentLanguage())) sky2ui::Status(*ui, Tr(Text::GameNamesUnavailable), 2);
        const int rosterColumns = sky2ui::Columns(*ui, "roster.columns", 300.0f);
        sky2ui::BeginCard(*ui, "roster.selection");
        ui->text(Tr(Text::CharacterColumn));
        // 列表只选择角色，右侧（窄屏为下方）操作卡独立确认；方向导航和切换
        // 角色不会提交写入。列表随 Main 视口改变高度，旧宿主保持原固定尺寸。
        if (ui->begin_child("roster.list", RosterListHeight(*ui, *frame, rosterColumns))) {
            for (size_t index = 0; index < snapshot.roster.members.size(); ++index) {
                const auto& member = snapshot.roster.members[index];
                const auto name = Character(kRosterDefinitions[index].id);
                const auto label = name + "  ·  " + (snapshot.roster.ready ? RoleState(member) : Tr(Text::WaitingData));
                const auto id = "role." + std::to_string(kRosterDefinitions[index].id);
                if (ui->selectable(id.c_str(), label.c_str(), selected == index) && selected != index) {
                    selected = index;
                    CancelConfirmation();
                }
            }
        }
        ui->end_child();
        sky2ui::EndCard(*ui);
        sky2ui::NextColumn(*ui);
        sky2ui::BeginCard(*ui, "roster.action");
        sky2ui::Section(*ui, H(HubText::Selected));
        const auto& member = snapshot.roster.members[selected];
        const auto name = Character(kRosterDefinitions[selected].id);
        const auto* level = GameTermFor(CurrentLanguage(), GameTerm::Level);
        std::snprintf(buffer, sizeof(buffer), "%s  ·  %s %u", name.c_str(), level ? level : Tr(Text::LevelColumn), member.level);
        ui->text(buffer);
        // 失焦时继续显示只读快照，TickHubPanel 已取消武装。即使旧宿主或测试
        // 替身没有禁用后台控件，也不能由后台绘制路径发出角色写入请求。
        const bool canAdd = frame->foreground && CanAdd(snapshot, member);
        const bool armed = confirmation.Armed(member.id, snapshot.roster.generation, frame->time_ms);
        sky2ui::Status(*ui, snapshot.roster.ready ? RoleState(member) : Tr(Text::WaitingData), 0);
        if (member.needsPreparation) sky2ui::Status(*ui, H(HubText::Preparation), 2);
        // 持久化风险紧邻唯一执行入口，无论说明折叠与否都保持可见。
        sky2ui::Status(*ui, H(HubText::PersistentChange), 2);
        ui->begin_disabled(!canAdd);
        if (ui->button("role.confirm", Tr(armed ? Text::ConfirmAgain :
            member.needsPreparation ? Text::PrepareAndAdd : member.hidden ? Text::RevealReserve : Text::AddReserve)) && canAdd) {
            if (confirmation.Press(member.id, snapshot.roster.generation, frame->time_ms))
                submissionRejected = !QueueAddMember(member.id);
        }
        ui->end_disabled();
        if (confirmation.Armed(member.id, snapshot.roster.generation, frame->time_ms)) {
            std::snprintf(buffer, sizeof(buffer), Tr(Text::ConfirmRoleFormat), name.c_str(), RoleAction(member));
            sky2ui::Status(*ui, buffer, 2);
        } else if (submissionRejected) sky2ui::Status(*ui, Tr(Text::StateChanged), 3);
        else if (snapshot.roster.pendingId != kNoRosterId) sky2ui::Status(*ui, Tr(Text::WaitingExecution), 0);
        else if (snapshot.roster.lastResult != RosterResult::None) {
            // 执行结果属于最后一次请求，不一定属于刚选中的角色；显式标明
            // 目标姓名，避免切换选择后把其他角色的成功结果当成当前操作结果。
            const auto result = snapshot.roster.lastId != kNoRosterId ?
                Character(snapshot.roster.lastId) + " · " + RosterResultText(snapshot.roster.lastResult) :
                std::string(RosterResultText(snapshot.roster.lastResult));
            sky2ui::Status(*ui, result.c_str(), ResultTone(snapshot.roster.lastResult));
        }
        else if (!(snapshot.appliedFeatures & FeatureUnjoined)) sky2ui::Status(*ui, Tr(Text::EnableUnjoinedNote), 0);
        else if (!snapshot.roster.canEdit) sky2ui::Status(*ui, RosterBlockReasonText(snapshot.roster.blockReason), 2);
        else if (canAdd) sky2ui::Status(*ui, H(HubText::ReadyToAdd), 1);
        if (sky2ui::Disclosure(*ui, "roster.details", H(HubText::Details))) {
            ui->text_wrapped(Tr(Text::RoleUsageNote));
            if (member.needsPreparation) {
                ui->text_wrapped(Tr(Text::PrepareWeaponNote));
                ui->text_wrapped(Tr(Text::PrepareCraftNote));
            }
            ui->text_wrapped(Tr(Text::SaveNote));
        }
        sky2ui::EndCard(*ui);
        sky2ui::EndColumns(*ui);
    } catch (...) {
        CancelConfirmation();
        Log("Hub Party page exception contained; pending confirmation canceled.");
    }
}
}

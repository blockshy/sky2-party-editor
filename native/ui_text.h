// Mod 自有界面的八语文案目录。游戏角色名由原生资源名称层提供，不在这里手工翻译。
// 文本编号用于跨线程状态快照；调用方保存编号，在显示时调用 Tr，避免缓存旧语言译文。
#pragma once
#include "localization.h"
#include <cstddef>
#include <cstdint>

namespace sky2party {
// 编号只描述语义，不用于 ImGui 的控件身份。控件须继续使用稳定的独立 ID，
// 这样语言切换不会重置选择、滚动位置或角色操作的二次确认状态。
enum class Text : uint16_t {
    None,
    TitleFormat,
    FeatureFixedMembers,
    FeatureAnywhere,
    FeatureUnavailable,
    FeatureUnjoined,
    Close,
    FeatureColumn,
    StatusColumn,
    Enable,
    Disable,
    On,
    Off,
    UnknownStateFormat,
    PendingStateFormat,
    ControlDefault,
    FixedWarningPrefix,
    FixedMemberFormat,
    FixedUnknownFormat,
    FixedMoreFormat,
    FixedRestoreNote,
    RoleSection,
    RoleSectionNote,
    CharacterColumn,
    LevelColumn,
    ActionColumn,
    PrepareAndAdd,
    RevealReserve,
    AddReserve,
    HiddenReserve,
    Unavailable,
    InRoster,
    DataNotReady,
    Unjoined,
    WaitingData,
    WaitingExecution,
    ConfirmAgain,
    AdjustInGame,
    ConfirmRoleFormat,
    StateChanged,
    EnableUnjoinedNote,
    RoleUsageNote,
    PrepareWeaponNote,
    PrepareCraftNote,
    SaveNote,
    ShowHide,
    Select,
    ToggleConfirm,
    DpadUpDown,
    InputUnavailable,
    ControlFixedBlocked,
    ControlFixedUnknown,
    ControlApplied,
    ControlSaveFailed,
    ControlApplyFailed,
    ControlException,
    ControlWaiting,
    ControlLoadWait,
    ResultQueued,
    ResultAddedReserve,
    ResultRevealedReserve,
    ResultAlreadyPresent,
    ResultUnsupportedCharacter,
    ResultUninitialized,
    ResultInvalidData,
    ResultFull,
    ResultNotReady,
    ResultUnsafeState,
    ResultStaleRequest,
    ResultBusy,
    ResultNativeRejected,
    ResultPreparationFailed,
    ResultPartiallyPrepared,
    ResultPreparedAndAdded,
    ResultPreparedAndRevealed,
    BlockNotExploring,
    BlockFormationStoryLock,
    BlockNativeEntryLock,
    ListSeparator,
    CharacterIdFormat,
    GameNamesUnavailable,
    Count
};

// 字段顺序严格对应 Language；八列全部显式填写，不靠英文回退冒充完整翻译。
// 带 Format 后缀的条目必须在全部语言保留相同 printf 参数类型与顺序：
// TitleFormat(%s 版本)、UnknownStateFormat(%s 期望状态)、PendingStateFormat(%s 当前,%s 期望)、
// FixedMemberFormat(%u 队伍,%s 原生角色名)、FixedUnknownFormat(%u 队伍,%u 角色 ID)、
// FixedMoreFormat(%u 人数)、ConfirmRoleFormat(%s 原生角色名,%s 操作)、CharacterIdFormat(%u 角色 ID)。
// 纯逻辑测试核对这些约定；句中的游戏术语沿用已核对的原文词形，但游戏完整名称由资源层读取。
struct UiTextEntry {
    const char* chinese;
    const char* japanese;
    const char* english;
    const char* traditionalChinese;
    const char* german;
    const char* french;
    const char* spanish;
    const char* korean;
};

inline constexpr UiTextEntry kUiTexts[] = {
    {"", "", "", "", "", "", "", ""}, // None：明确表示没有待显示消息，不触发回退。
    {"队伍编辑 · %s", "パーティー編集 · %s", "Party Editor · %s", "隊伍編輯 · %s", "Gruppeneditor · %s", "Éditeur d’équipe · %s", "Editor de grupo · %s", "파티 편집 · %s"}, // TitleFormat
    {"解除固定队员", "固定メンバーを変更可能に", "Unlock fixed members", "解除固定隊員", "Feste Mitglieder freigeben", "Déverrouiller les membres imposés", "Desbloquear miembros fijos", "고정 멤버 제한 해제"}, // FeatureFixedMembers
    {"随处编成", "どこでも編成", "Party changes anywhere", "隨處編成", "Gruppe überall ändern", "Modifier l’équipe partout", "Cambiar grupo en cualquier lugar", "어디서나 편성"}, // FeatureAnywhere
    {"暂不可选后备", "一時選択不可のリザーブを解放", "Unlock unavailable standby members", "暫不可選後備", "Gesperrte Reserve freigeben", "Déverrouiller les réservistes indisponibles", "Desbloquear miembros en espera no disponibles", "일시 선택 불가 리저브 멤버 해제"}, // FeatureUnavailable
    {"未入队角色（实验）", "未加入キャラ（実験的）", "Unjoined characters (experimental)", "未入隊角色（實驗）", "Noch nicht beigetretene Figuren (experimentell)", "Personnages non recrutés (expérimental)", "Personajes no reclutados (experimental)", "미가입 캐릭터 (실험적)"}, // FeatureUnjoined
    {"关闭", "閉じる", "Close", "關閉", "Schließen", "Fermer", "Cerrar", "닫기"}, // Close
    {"功能", "機能", "Feature", "功能", "Funktion", "Fonction", "Función", "기능"}, // FeatureColumn
    {"状态", "状態", "Status", "狀態", "Status", "État", "Estado", "상태"}, // StatusColumn
    {"开启", "有効", "On", "開啟", "Ein", "Activé", "Activado", "켜짐"}, // Enable
    {"关闭", "無効", "Off", "關閉", "Aus", "Désactivé", "Desactivado", "꺼짐"}, // Disable
    {"已开启", "有効", "On", "已開啟", "Ein", "Activé", "Activado", "켜짐"}, // On
    {"已关闭", "無効", "Off", "已關閉", "Aus", "Désactivé", "Desactivado", "꺼짐"}, // Off
    {"待核对（期望%s）", "要確認（要求：%s）", "Unverified (requested: %s)", "待核對（期望%s）", "Ungeprüft (gewünscht: %s)", "Non vérifié (demandé : %s)", "Sin verificar (solicitado: %s)", "확인 필요 (요청: %s)"}, // UnknownStateFormat
    {"%s → 等待%s", "%s → %s 待ち", "%s → %s pending", "%s → 等待%s", "%s → %s ausstehend", "%s → %s en attente", "%s → %s pendiente", "%s → %s 대기"}, // PendingStateFormat
    {"开关将在正常探索、游戏菜单关闭后生效。", "通常の探索画面でゲームメニューを閉じると、設定が反映されます。", "Changes apply during normal exploration with game menus closed.", "開關將在一般探索、遊戲選單關閉後生效。", "Änderungen gelten beim normalen Erkunden, wenn Spielmenüs geschlossen sind.", "Les changements s’appliquent en exploration normale, menus du jeu fermés.", "Los cambios se aplican al explorar normalmente con los menús del juego cerrados.", "일반 탐색 중 게임 메뉴를 닫으면 설정이 적용됩니다."}, // ControlDefault
    {"固定队员仍在后备：", "固定メンバーがリザーブにいます：", "Fixed members still in standby: ", "固定隊員仍在後備：", "Feste Mitglieder noch in Reserve: ", "Membres imposés encore en réserve : ", "Miembros fijos aún en espera: ", "고정 멤버가 리저브에 남아 있습니다: "}, // FixedWarningPrefix
    {"队伍 %u · %s", "パーティー %u · %s", "Party %u · %s", "隊伍 %u · %s", "Gruppe %u · %s", "Équipe %u · %s", "Grupo %u · %s", "파티 %u · %s"}, // FixedMemberFormat
    {"队伍 %u · 角色 %u", "パーティー %u · キャラ %u", "Party %u · Character %u", "隊伍 %u · 角色 %u", "Gruppe %u · Figur %u", "Équipe %u · Personnage %u", "Grupo %u · Personaje %u", "파티 %u · 캐릭터 %u"}, // FixedUnknownFormat
    {"（共 %u 人）", "（計 %u 人）", " (%u in total)", "（共 %u 人）", " (insgesamt %u)", " (%u au total)", " (%u en total)", " (총 %u명)"}, // FixedMoreFormat
    {"关闭解除固定或卸载前，请在游戏队伍菜单中将其换回主力并保存。", "固定解除を無効にする前やアンインストール前に、ゲームの編成画面で戦闘メンバーに戻してセーブしてください。", "Before disabling fixed-member unlock or uninstalling, move them back into the active party using the game’s party menu and save.", "關閉解除固定或解除安裝前，請在遊戲隊伍選單中將其換回主力並儲存。", "Vor dem Deaktivieren der Freigabe oder der Deinstallation diese Mitglieder im Gruppenmenü des Spiels wieder in die aktive Gruppe nehmen und speichern.", "Avant de désactiver le déverrouillage ou de désinstaller le mod, replacez-les dans l’équipe active via le menu d’équipe du jeu, puis sauvegardez.", "Antes de desactivar el desbloqueo o desinstalar el mod, devuélvelos al grupo activo desde el menú de grupo del juego y guarda.", "고정 해제를 끄거나 모드를 삭제하기 전에 게임의 편성 화면에서 전투 멤버로 돌려놓고 저장하세요."}, // FixedRestoreNote
    {"角色后备", "リザーブメンバー", "Standby roster", "角色後備", "Reservemitglieder", "Membres de réserve", "Miembros en espera", "리저브 멤버"}, // RoleSection
    {"逐人确认，最多四名主力", "1人ずつ確認・戦闘メンバーは最大4人", "Confirm each; up to four active members", "逐人確認，最多四名主力", "Einzeln bestätigen; bis zu vier aktive Mitglieder", "Confirmation individuelle ; quatre membres actifs max.", "Confirmación individual; hasta cuatro miembros activos", "한 명씩 확인 · 전투 멤버 최대 4명"}, // RoleSectionNote
    {"角色", "キャラクター", "Character", "角色", "Figur", "Personnage", "Personaje", "캐릭터"}, // CharacterColumn
    {"等级", "レベル", "Level", "等級", "Stufe", "Niveau", "Nivel", "레벨"}, // LevelColumn
    {"操作", "操作", "Action", "操作", "Aktion", "Action", "Acción", "동작"}, // ActionColumn
    {"补基础装备并加入", "基本装備を補って追加", "Prepare and add", "補基本裝備並加入", "Vorbereiten und hinzufügen", "Préparer et ajouter", "Preparar y añadir", "기본 장비 보충 후 추가"}, // PrepareAndAdd
    {"开放后备", "リザーブに表示", "Show in standby", "開放後備", "Reserve einblenden", "Afficher en réserve", "Mostrar en espera", "리저브 목록에 표시"}, // RevealReserve
    {"加入后备", "リザーブに追加", "Add to standby", "加入後備", "Zur Reserve hinzufügen", "Ajouter en réserve", "Añadir en espera", "리저브 목록에 추가"}, // AddReserve
    {"隐藏后备", "非表示のリザーブ", "Hidden standby", "隱藏後備", "Versteckte Reserve", "Réserviste masqué", "Miembro en espera oculto", "숨겨진 리저브 멤버"}, // HiddenReserve
    {"暂不可选", "一時選択不可", "Unavailable", "暫不可選", "Nicht verfügbar", "Indisponible", "No disponible", "일시 선택 불가"}, // Unavailable
    {"已在名单", "加入済み", "In roster", "已在名單", "Bereits in der Liste", "Déjà dans la liste", "Ya en la lista", "목록에 있음"}, // InRoster
    {"数据未就绪", "データ未準備", "Data not ready", "資料未就緒", "Daten nicht bereit", "Données non prêtes", "Datos no listos", "데이터 준비 안 됨"}, // DataNotReady
    {"未入队", "未加入", "Not joined", "未入隊", "Nicht beigetreten", "Non recruté", "No reclutado", "미가입"}, // Unjoined
    {"等待游戏数据", "ゲームデータ待ち", "Waiting for game data", "等待遊戲資料", "Warte auf Spieldaten", "Attente des données du jeu", "Esperando datos del juego", "게임 데이터 대기"}, // WaitingData
    {"等待执行", "処理待ち", "Pending", "等待執行", "Ausstehend", "En attente", "Pendiente", "실행 대기"}, // WaitingExecution
    {"再次确认", "もう一度確認", "Confirm again", "再次確認", "Erneut bestätigen", "Confirmer à nouveau", "Confirmar de nuevo", "다시 확인"}, // ConfirmAgain
    {"游戏队伍菜单中调整", "ゲームの編成画面で変更", "Use the game’s party menu", "遊戲隊伍選單中調整", "Im Gruppenmenü ändern", "Modifier via le menu d’équipe", "Cambiar en el menú de grupo", "게임 편성 화면에서 변경"}, // AdjustInGame
    {"再次确认：%s · %s", "再確認：%s · %s", "Confirm again: %s · %s", "再次確認：%s · %s", "Erneut bestätigen: %s · %s", "Confirmer à nouveau : %s · %s", "Confirmar de nuevo: %s · %s", "다시 확인: %s · %s"}, // ConfirmRoleFormat
    {"当前状态已变化，请返回探索后重试。", "状態が変わりました。探索画面に戻って再試行してください。", "The current state changed. Return to exploration and try again.", "目前狀態已變更，請返回探索後重試。", "Der Zustand hat sich geändert. Zur Erkundung zurückkehren und erneut versuchen.", "L’état a changé. Revenez à l’exploration et réessayez.", "El estado ha cambiado. Vuelve a la exploración e inténtalo de nuevo.", "현재 상태가 변경되었습니다. 탐색 화면으로 돌아가 다시 시도하세요."}, // StateChanged
    {"开启“未入队角色（实验）”后，可选择角色加入后备。", "「未加入キャラ（実験的）」を有効にすると、キャラを選んでリザーブに追加できます。", "Enable “Unjoined characters (experimental)” to add characters to standby.", "開啟「未入隊角色（實驗）」後，可選擇角色加入後備。", "„Noch nicht beigetretene Figuren (experimentell)“ aktivieren, um Figuren zur Reserve hinzuzufügen.", "Activez « Personnages non recrutés (expérimental) » pour ajouter des personnages en réserve.", "Activa «Personajes no reclutados (experimental)» para añadir personajes en espera.", "'미가입 캐릭터 (실험적)'를 켜면 캐릭터를 골라 리저브 목록에 추가할 수 있습니다."}, // EnableUnjoinedNote
    {"选择角色后确认两次；只补缺失基础装备/战技，保留培养。", "キャラを選んで2回確認。基本装備とクラフトの不足分のみ補い、育成状況は維持します。", "Select a character and confirm twice. Only missing basic gear and Crafts are supplied; existing progress is kept.", "選擇角色後確認兩次；只補缺少的基本裝備／戰技，保留培養。", "Figur wählen und zweimal bestätigen. Nur fehlende Grundausrüstung und Techniken werden ergänzt; bisherige Entwicklung bleibt erhalten.", "Choisissez un personnage et confirmez deux fois. Seuls l’équipement de base et les techs manquants sont ajoutés ; la progression est conservée.", "Elige un personaje y confirma dos veces. Solo se añaden equipo básico y técnicas que falten; se conserva su desarrollo.", "캐릭터를 선택하고 두 번 확인하세요. 부족한 기본 장비와 크래프트만 보충하며 육성 상태는 유지됩니다."}, // RoleUsageNote
    {"空武器槽生成基础武器，不消耗背包；", "武器欄が空なら基本武器を生成します。所持品は消費しません。", "An empty weapon slot receives a basic weapon; inventory is not consumed.", "空武器欄位產生基本武器，不消耗背包；", "Leere Waffenplätze erhalten eine Grundwaffe; das Inventar wird nicht verbraucht.", "Une arme de base est créée si l’emplacement est vide, sans consommer l’inventaire.", "Si la ranura está vacía, se crea un arma básica sin consumir el inventario.", "무기 슬롯이 비어 있으면 기본 무기를 생성하며 소지품은 소모하지 않습니다."}, // PrepareWeaponNote
    {"空战技按等级补齐，保留已有培养。", "クラフトが未登録ならレベルに応じて補います。既存の育成状況は維持します。", "Empty Craft lists are filled by level; existing character progress is kept.", "空戰技依等級補齊，保留既有培養。", "Leere Technikenlisten werden der Stufe entsprechend ergänzt; bisherige Entwicklung bleibt erhalten.", "Les listes de techs vides sont complétées selon le niveau ; la progression existante est conservée.", "Las listas vacías de técnicas se completan según el nivel; se conserva el desarrollo existente.", "크래프트 목록이 비어 있으면 레벨에 맞춰 보충하며 기존 육성 상태는 유지됩니다."}, // PrepareCraftNote
    {"加入后用游戏队伍菜单换人；保存游戏会保留加入结果。", "追加後はゲームの編成画面で入れ替えてください。セーブすると追加結果も保存されます。", "Use the game’s party menu to swap members after adding them. Saving the game keeps these changes.", "加入後用遊戲隊伍選單換人；儲存遊戲會保留加入結果。", "Nach dem Hinzufügen im Gruppenmenü wechseln. Beim Speichern des Spiels werden diese Änderungen übernommen.", "Après l’ajout, changez les membres via le menu d’équipe du jeu. Sauvegarder conserve les ajouts.", "Tras añadir miembros, cámbialos desde el menú de grupo del juego. Guardar conserva las incorporaciones.", "추가한 뒤 게임 편성 화면에서 멤버를 교체하세요. 게임을 저장하면 추가 결과도 유지됩니다."}, // SaveNote
    {"显示/隐藏", "表示/非表示", "Show/hide", "顯示／隱藏", "Ein-/ausblenden", "Afficher/masquer", "Mostrar/ocultar", "표시/숨기기"}, // ShowHide
    {"选择", "選択", "Select", "選擇", "Auswählen", "Sélectionner", "Seleccionar", "선택"}, // Select
    {"切换 / 确认", "切替 / 決定", "Toggle / confirm", "切換／確認", "Umschalten / bestätigen", "Basculer / confirmer", "Alternar / confirmar", "전환 / 확인"}, // ToggleConfirm
    {"十字键 ↑ / ↓", "方向キー ↑ / ↓", "D-pad ↑ / ↓", "方向鍵 ↑ / ↓", "Steuerkreuz ↑ / ↓", "Croix dir. ↑ / ↓", "Cruceta ↑ / ↓", "방향 패드 ↑ / ↓"}, // DpadUpDown
    {"手柄输入链尚未确认，请使用已设置的键盘快捷键打开；方向键 / Enter 操作。", "コントローラー入力を確認できません。設定したキーボードショートカットで開き、方向キーとEnterで操作してください。", "Controller input is not verified. Open with your configured keyboard shortcut; use arrow keys and Enter.", "手把輸入尚未確認，請使用已設定的鍵盤快捷鍵開啟；方向鍵／Enter 操作。", "Controller-Eingabe nicht bestätigt. Mit dem eingestellten Tastenkürzel öffnen; mit Pfeiltasten und Enter bedienen.", "Entrée manette non vérifiée. Ouvrez avec votre raccourci clavier ; utilisez les flèches et Entrée.", "La entrada del mando no está verificada. Abre con tu atajo de teclado configurado; usa las flechas y Enter.", "컨트롤러 입력이 확인되지 않았습니다. 설정한 키보드 단축키로 열고 방향키와 Enter로 조작하세요."}, // InputUnavailable
    {"未关闭：请先在游戏队伍菜单中将提示的固定队员换回主力。", "無効化を中止しました。表示された固定メンバーをゲームの編成画面で戦闘メンバーに戻してください。", "Kept enabled: return the listed fixed members to the active party using the game’s party menu first.", "未關閉：請先在遊戲隊伍選單中將提示的固定隊員換回主力。", "Bleibt aktiv: Die genannten festen Mitglieder zuerst im Gruppenmenü in die aktive Gruppe zurücknehmen.", "Reste activé : replacez d’abord les membres imposés indiqués dans l’équipe active via le menu du jeu.", "Sigue activado: devuelve primero los miembros fijos indicados al grupo activo desde el menú del juego.", "해제를 유지합니다. 표시된 고정 멤버를 먼저 게임 편성 화면에서 전투 멤버로 돌려놓으세요."}, // ControlFixedBlocked
    {"未关闭：无法核实全部队伍，请返回稳定探索后重试。", "無効化を中止しました。全パーティーを確認できません。安定した探索画面で再試行してください。", "Kept enabled: all parties could not be checked. Return to normal exploration and retry.", "未關閉：無法核實全部隊伍，請返回穩定探索後重試。", "Bleibt aktiv: Nicht alle Gruppen konnten geprüft werden. Beim normalen Erkunden erneut versuchen.", "Reste activé : impossible de vérifier toutes les équipes. Réessayez en exploration normale.", "Sigue activado: no se pudieron verificar todos los grupos. Reinténtalo durante la exploración normal.", "해제를 유지합니다. 모든 파티를 확인하지 못했습니다. 안정적인 탐색 화면에서 다시 시도하세요."}, // ControlFixedUnknown
    {"设置已应用；角色加入需在下方单独确认。", "設定を反映しました。キャラの追加は下の一覧で個別に確認してください。", "Settings applied. Confirm character additions individually below.", "設定已套用；角色加入需在下方個別確認。", "Einstellungen übernommen. Figuren unten einzeln zum Hinzufügen bestätigen.", "Paramètres appliqués. Confirmez chaque ajout de personnage ci-dessous.", "Ajustes aplicados. Confirma cada incorporación por separado abajo.", "설정이 적용되었습니다. 캐릭터 추가는 아래에서 한 명씩 확인하세요."}, // ControlApplied
    {"设置已生效，但配置保存失败；重启后可能恢复旧设置。", "設定は反映されましたが、保存に失敗しました。再起動後は以前の設定に戻る可能性があります。", "Settings applied, but could not be saved. Restarting may restore the old settings.", "設定已生效，但設定檔儲存失敗；重新啟動後可能恢復舊設定。", "Einstellungen aktiv, aber nicht gespeichert. Nach einem Neustart können die alten Einstellungen gelten.", "Paramètres appliqués, mais non enregistrés. Un redémarrage peut rétablir les anciens paramètres.", "Ajustes aplicados, pero no guardados. Al reiniciar podrían restaurarse los anteriores.", "설정은 적용됐지만 저장에 실패했습니다. 재시작하면 이전 설정으로 돌아갈 수 있습니다."}, // ControlSaveFailed
    {"设置未完整应用，已停止重试；请查看日志并重启游戏。", "設定を完全に反映できず、再試行を停止しました。ログを確認し、ゲームを再起動してください。", "Settings were not fully applied; retries stopped. Check the log and restart the game.", "設定未完整套用，已停止重試；請查看紀錄並重新啟動遊戲。", "Einstellungen nicht vollständig übernommen; weitere Versuche gestoppt. Log prüfen und Spiel neu starten.", "Paramètres partiellement appliqués ; tentatives arrêtées. Consultez le journal et redémarrez le jeu.", "No se aplicaron todos los ajustes; se detuvieron los reintentos. Revisa el registro y reinicia el juego.", "설정이 완전히 적용되지 않아 재시도를 중단했습니다. 로그를 확인하고 게임을 재시작하세요."}, // ControlApplyFailed
    {"控制服务发生异常；已停用角色加入，请重新启动游戏。", "制御処理でエラーが発生したため、キャラ追加を停止しました。ゲームを再起動してください。", "Control service error. Character additions are disabled; restart the game.", "控制服務發生異常；已停用角色加入，請重新啟動遊戲。", "Fehler im Steuerdienst. Hinzufügen von Figuren deaktiviert; Spiel neu starten.", "Erreur du service de contrôle. Ajout de personnages désactivé ; redémarrez le jeu.", "Error del servicio de control. Las incorporaciones están desactivadas; reinicia el juego.", "제어 서비스 오류로 캐릭터 추가를 중단했습니다. 게임을 재시작하세요."}, // ControlException
    {"设置等待应用，请返回可自由行动的探索画面。", "設定の反映待ちです。自由に動ける探索画面に戻ってください。", "Settings pending. Return to an exploration scene where you can move freely.", "設定等待套用，請返回可自由行動的探索畫面。", "Einstellungen ausstehend. Zu einer Erkundungsszene mit freier Bewegung zurückkehren.", "Paramètres en attente. Revenez à une scène d’exploration où vous pouvez vous déplacer librement.", "Ajustes pendientes. Vuelve a una escena de exploración donde puedas moverte libremente.", "설정 적용 대기 중입니다. 자유롭게 이동할 수 있는 탐색 화면으로 돌아가세요."}, // ControlWaiting
    {"等待读取存档并进入自由探索。", "セーブデータをロードし、自由探索に入ってください。", "Waiting for a loaded save and free exploration.", "等待读取存檔並進入自由探索。", "Warte auf geladenen Spielstand und freie Erkundung.", "En attente du chargement d’une sauvegarde et de l’exploration libre.", "Esperando a que cargues una partida y entres en exploración libre.", "저장 데이터를 불러오고 자유 탐색에 들어가기를 기다리는 중입니다."}, // ControlLoadWait
    {"等待当前探索帧处理", "探索画面での処理待ち", "Waiting for the exploration update", "等待目前探索畫面處理", "Warte auf Erkundungsaktualisierung", "En attente du traitement en exploration", "Esperando la actualización de exploración", "탐색 화면 처리 대기"}, // ResultQueued
    {"已加入后备，原有培养保持不变", "リザーブに追加しました。既存の育成状況は維持されています。", "Added to standby; existing character progress kept", "已加入後備，原有培養保持不變", "Zur Reserve hinzugefügt; bisherige Entwicklung erhalten", "Ajouté en réserve ; progression existante conservée", "Añadido en espera; se conserva su desarrollo", "리저브 목록에 추가했습니다. 기존 육성 상태는 유지됩니다."}, // ResultAddedReserve
    {"已显示在后备，原有培养保持不变", "リザーブに表示しました。既存の育成状況は維持されています。", "Revealed in standby; existing character progress kept", "已顯示在後備，原有培養保持不變", "In Reserve eingeblendet; bisherige Entwicklung erhalten", "Affiché en réserve ; progression existante conservée", "Visible en espera; se conserva su desarrollo", "리저브 목록에 표시했습니다. 기존 육성 상태는 유지됩니다."}, // ResultRevealedReserve
    {"角色已经在当前名单中", "このキャラはすでに現在の一覧にいます。", "This character is already in the current roster", "角色已經在目前名單中", "Diese Figur ist bereits in der aktuellen Liste", "Ce personnage est déjà dans la liste actuelle", "Este personaje ya está en la lista actual", "이미 현재 목록에 있는 캐릭터입니다."}, // ResultAlreadyPresent
    {"不支持此角色", "このキャラには対応していません。", "This character is not supported", "不支援此角色", "Diese Figur wird nicht unterstützt", "Ce personnage n’est pas pris en charge", "Este personaje no es compatible", "지원하지 않는 캐릭터입니다."}, // ResultUnsupportedCharacter
    {"角色数据尚未完整初始化，暂不可加入", "キャラのデータが完全には初期化されていないため、まだ追加できません。", "Character data is not fully initialized; cannot add yet", "角色資料尚未完整初始化，暫時無法加入", "Figurendaten noch nicht vollständig initialisiert; Hinzufügen derzeit nicht möglich", "Données du personnage non entièrement initialisées ; ajout indisponible", "Los datos del personaje no están completamente inicializados; aún no se puede añadir", "캐릭터 데이터가 완전히 초기화되지 않아 아직 추가할 수 없습니다."}, // ResultUninitialized
    {"当前名单或角色数据不符合已验证条件", "現在の一覧またはキャラのデータが検証条件を満たしていません。", "The current roster or character data did not pass validation", "目前名單或角色資料不符合已驗證條件", "Aktuelle Liste oder Figurendaten erfüllen die geprüften Bedingungen nicht", "La liste ou les données du personnage ne satisfont pas les contrôles", "La lista o los datos del personaje no superaron la validación", "현재 목록 또는 캐릭터 데이터가 검증 조건을 충족하지 않습니다."}, // ResultInvalidData
    {"当前队伍名单已满", "現在のパーティー一覧がいっぱいです。", "The current party roster is full", "目前隊伍名單已滿", "Die aktuelle Gruppenliste ist voll", "La liste de l’équipe actuelle est pleine", "La lista del grupo actual está llena", "현재 파티 목록이 가득 찼습니다."}, // ResultFull
    {"等待读取当前角色数据", "現在のキャラデータの読み取り待ちです。", "Waiting to read current character data", "等待读取目前角色資料", "Warte auf aktuelle Figurendaten", "En attente des données actuelles du personnage", "Esperando los datos actuales del personaje", "현재 캐릭터 데이터를 읽기를 기다리는 중입니다."}, // ResultNotReady
    {"请回到普通探索画面后重新操作", "通常の探索画面に戻って再操作してください。", "Return to normal exploration and try again", "請回到一般探索畫面後重新操作", "Zur normalen Erkundung zurückkehren und erneut versuchen", "Revenez à l’exploration normale et réessayez", "Vuelve a la exploración normal e inténtalo de nuevo", "일반 탐색 화면으로 돌아가 다시 시도하세요."}, // ResultUnsafeState
    {"场景或队伍已变化，请重新确认", "場面またはパーティーが変わりました。再度確認してください。", "The scene or party changed; confirm again", "場景或隊伍已變更，請重新確認", "Szene oder Gruppe geändert; erneut bestätigen", "La scène ou l’équipe a changé ; confirmez à nouveau", "La escena o el grupo ha cambiado; confirma de nuevo", "장면 또는 파티가 변경되었습니다. 다시 확인하세요."}, // ResultStaleRequest
    {"上一条加入请求仍在处理", "前の追加要求を処理中です。", "The previous addition is still being processed", "上一筆加入請求仍在處理", "Die vorige Hinzufügung wird noch verarbeitet", "L’ajout précédent est toujours en cours", "La incorporación anterior sigue en proceso", "이전 추가 요청을 아직 처리 중입니다."}, // ResultBusy
    {"未确认加入成功，请先核对游戏队伍", "追加の成功を確認できませんでした。ゲーム内のパーティーを確認してください。", "Addition was not confirmed; check the in-game party first", "未確認加入成功，請先核對遊戲隊伍", "Hinzufügung nicht bestätigt; zuerst die Gruppe im Spiel prüfen", "Ajout non confirmé ; vérifiez d’abord l’équipe dans le jeu", "No se confirmó la incorporación; comprueba primero el grupo en el juego", "추가 성공을 확인하지 못했습니다. 먼저 게임 내 파티를 확인하세요."}, // ResultNativeRejected
    {"缺失数据未能补足，未加入队伍", "不足データを補えなかったため、パーティーに追加しませんでした。", "Missing data could not be supplied; character not added", "缺少的資料未能補足，未加入隊伍", "Fehlende Daten konnten nicht ergänzt werden; Figur nicht hinzugefügt", "Impossible de compléter les données manquantes ; personnage non ajouté", "No se pudieron completar los datos faltantes; personaje no añadido", "부족한 데이터를 보충하지 못해 파티에 추가하지 않았습니다."}, // ResultPreparationFailed
    {"数据已部分补足；未确认加入成功，请核对角色和队伍", "データは一部補完されましたが、追加は未確認です。キャラとパーティーを確認してください。", "Data partly supplied; addition not confirmed. Check the character and party", "資料已部分補足；未確認加入成功，請核對角色和隊伍", "Daten teilweise ergänzt; Hinzufügung nicht bestätigt. Figur und Gruppe prüfen", "Données partiellement complétées ; ajout non confirmé. Vérifiez le personnage et l’équipe", "Datos completados parcialmente; incorporación sin confirmar. Comprueba el personaje y el grupo", "데이터를 일부 보충했으나 추가는 확인되지 않았습니다. 캐릭터와 파티를 확인하세요."}, // ResultPartiallyPrepared
    {"已补足缺失数据并加入后备", "不足データを補い、リザーブに追加しました。", "Missing data supplied; added to standby", "已補足缺少的資料並加入後備", "Fehlende Daten ergänzt; zur Reserve hinzugefügt", "Données manquantes complétées ; ajouté en réserve", "Datos faltantes completados; añadido en espera", "부족한 데이터를 보충하고 리저브 목록에 추가했습니다."}, // ResultPreparedAndAdded
    {"已补足缺失数据并显示在后备", "不足データを補い、リザーブに表示しました。", "Missing data supplied; revealed in standby", "已補足缺少的資料並顯示在後備", "Fehlende Daten ergänzt; in Reserve eingeblendet", "Données manquantes complétées ; affiché en réserve", "Datos faltantes completados; visible en espera", "부족한 데이터를 보충하고 리저브 목록에 표시했습니다."}, // ResultPreparedAndRevealed
    {"请回到可自由行动的探索画面并关闭游戏菜单", "自由に動ける探索画面に戻り、ゲームメニューを閉じてください。", "Return to free exploration and close the game menus", "請回到可自由行動的探索畫面並關閉遊戲選單", "Zur freien Erkundung zurückkehren und Spielmenüs schließen", "Revenez à l’exploration libre et fermez les menus du jeu", "Vuelve a la exploración libre y cierra los menús del juego", "자유롭게 이동할 수 있는 탐색 화면으로 돌아가 게임 메뉴를 닫으세요."}, // BlockNotExploring
    {"当前剧情禁止调整队伍；仍可查看角色和应用 Mod 设置", "現在のイベントでは編成を変更できません。キャラの確認やMod設定の反映は可能です。", "Story progress currently blocks party changes. You can still view characters and apply mod settings", "目前劇情禁止調整隊伍；仍可查看角色並套用 Mod 設定", "Die Story verhindert derzeit Gruppenänderungen. Figurenansicht und Mod-Einstellungen bleiben verfügbar", "L’histoire bloque les changements d’équipe. Vous pouvez toujours consulter les personnages et appliquer les paramètres du mod", "La historia impide cambiar el grupo. Aún puedes ver personajes y aplicar ajustes del mod", "현재 스토리에서는 파티를 변경할 수 없습니다. 캐릭터 확인과 모드 설정 적용은 가능합니다."}, // BlockFormationStoryLock
    {"当前入口受限；开启随处编成后可在自由探索时加入角色", "現在の入口は制限されています。「どこでも編成」を有効にすると自由探索中にキャラを追加できます。", "This entry is restricted. Enable “Party changes anywhere” to add characters during free exploration", "目前入口受限；開啟隨處編成後可在自由探索時加入角色", "Dieser Zugang ist gesperrt. „Gruppe überall ändern“ aktivieren, um beim freien Erkunden Figuren hinzuzufügen", "Cet accès est restreint. Activez « Modifier l’équipe partout » pour ajouter des personnages en exploration libre", "Este acceso está restringido. Activa «Cambiar grupo en cualquier lugar» para añadir personajes durante la exploración libre", "현재 진입점이 제한되어 있습니다. '어디서나 편성'을 켜면 자유 탐색 중 캐릭터를 추가할 수 있습니다."}, // BlockNativeEntryLock
    {"；", "、", "; ", "；", "; ", " ; ", "; ", "; "}, // ListSeparator
    {"角色 %u", "キャラ %u", "Character %u", "角色 %u", "Figur %u", "Personnage %u", "Personaje %u", "캐릭터 %u"}, // CharacterIdFormat
    {"当前语言的游戏名称资源不可用，将显示角色编号；请检查游戏文件后重启。", "現在の言語のゲーム名称データを利用できないため、キャラ番号を表示します。ゲームファイルを確認し、再起動してください。", "Game names for this language are unavailable; character IDs are shown. Check the game files and restart.", "目前語言的遊戲名稱資源無法使用，將顯示角色編號；請檢查遊戲檔案後重新啟動。", "Spielnamen in dieser Sprache sind nicht verfügbar; Figuren-IDs werden angezeigt. Spieldateien prüfen und neu starten.", "Les noms du jeu sont indisponibles dans cette langue ; les identifiants des personnages sont affichés. Vérifiez les fichiers du jeu et redémarrez.", "Los nombres del juego no están disponibles en este idioma; se muestran los ID de personaje. Revisa los archivos del juego y reinicia.", "현재 언어의 게임 이름 데이터를 사용할 수 없어 캐릭터 번호를 표시합니다. 게임 파일을 확인하고 재시작하세요."}, // GameNamesUnavailable
};
static_assert(sizeof(kUiTexts) / sizeof(kUiTexts[0]) == static_cast<size_t>(Text::Count),
              "Every UI text ID must have exactly one eight-language entry.");

// 显式语言入口用于无游戏资源的测试及离屏预览；非法文本编号安全返回空串。
// 非法语言编号采用英语，与共享语言层的防御性回退一致，绝不越界读取。
inline const char* TextFor(Language language, Text id) noexcept {
    const size_t index = static_cast<size_t>(id);
    if (index >= static_cast<size_t>(Text::Count)) return "";
    const auto& text = kUiTexts[index];
    const char* const values[]{text.chinese, text.japanese, text.english, text.traditionalChinese,
        text.german, text.french, text.spanish, text.korean};
    const unsigned languageIndex = static_cast<unsigned>(language);
    return values[languageIndex < kLanguageCount ? languageIndex : static_cast<unsigned>(Language::English)];
}

// 必须每次显示时读取语言；不要在函数静态变量、全局 featureNames 或状态缓冲区缓存本结果。
inline const char* Tr(Text id) noexcept { return TextFor(CurrentLanguage(), id); }
} // namespace sky2party

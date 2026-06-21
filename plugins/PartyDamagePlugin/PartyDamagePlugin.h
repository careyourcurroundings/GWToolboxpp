#pragma once

#include <ToolboxUIPlugin.h>
#include <IconsFontAwesome5.h>

#include <GWCA/Constants/Constants.h>
#include <GWCA/Packets/StoC.h>
#include <GWCA/Utilities/Hook.h>

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <queue>

class PartyDamagePlugin : public ToolboxUIPlugin {
public:
    PartyDamagePlugin() = default;
    ~PartyDamagePlugin() override = default;

    const char* Name() const override { return "Damage"; }

    [[nodiscard]] bool HasSettings() const override { return true; }
    void DrawSettings() override;
    void LoadSettings(const wchar_t* folder) override;
    void SaveSettings(const wchar_t* folder) override;
    void Initialize(ImGuiContext* ctx, ImGuiAllocFns allocator_fns, HMODULE toolbox_dll) override;
    void SignalTerminate() override;
    bool CanTerminate() override;
    void Update(float delta) override;
    void Draw(IDirect3DDevice9* pDevice) override;

    struct Settings {
        uint32_t color_background = IM_COL32(0, 0, 0, 76);
        uint32_t color_damage = IM_COL32(0, 0, 0, 76);
        uint32_t color_recent = IM_COL32(102, 153, 230, 205);
        uint32_t color_healing = IM_COL32(102, 230, 102, 205);
        float width = 225.0f;
        bool bars_left = true;
        int recent_max_time = 7000;
        bool hide_in_outpost = false;
        bool print_by_click = false;
        bool overlay_party_window = true;
        bool show_damage = true;
        bool show_healing = true;
        bool show_dps = true;
        bool show_condition_dps = true;
        int user_offset = 0;
    };

private:
    struct PlayerDamage {
        uint32_t total_dmg = 0;
        uint32_t recent_dmg = 0;
        clock_t last_dmg_time = 0;
        uint32_t total_heal = 0;
        uint32_t recent_heal = 0;
        clock_t last_heal_time = 0;
        uint32_t agent_id = 0;
        GW::Constants::Profession primary = GW::Constants::Profession::None;
        GW::Constants::Profession secondary = GW::Constants::Profession::None;
        std::wstring name;
        void Reset();
    };

    struct PartyFramePos {
        ImVec2 top_left;
        ImVec2 bottom_right;
    };

    Settings settings;

    std::vector<PlayerDamage> player_damage;
    std::vector<PlayerDamage> departed_damage;
    std::vector<uint32_t> prev_party_agent_ids;

    std::unordered_map<uint32_t, uint32_t> party_idx_by_agent_id;
    std::vector<uint32_t> party_agent_ids;
    std::vector<std::wstring> party_names;
    PartyFramePos party_health_bars_pos;
    std::unordered_map<uint32_t, PartyFramePos> agent_health_bar_pos;
    uint32_t henchmen_start = UINT32_MAX;
    uint32_t pets_start = UINT32_MAX;

    uint32_t total_damage = 0;
    uint32_t total_healing = 0;

    clock_t first_packet_time = 0;
    clock_t last_packet_time = 0;
    clock_t accumulated_combat_time_ms = 0;

    clock_t GetEffectiveCombatTime() const;

    static constexpr double DEGEN_CAP_HP_SEC = 20.0;
    static constexpr size_t MAX_DEGEN_TRACKERS = 256;

    static constexpr uint32_t DEEP_WOUND_EFFECT_ID = 128;
    static constexpr uint32_t DEEP_WOUND_DMG_CAP = 100;
    static constexpr double DEEP_WOUND_MAX_HP_FRAC = 0.20;

    static constexpr size_t BASIC_COND_COUNT = 4;

    enum class DegenType : uint8_t {
        Bleeding, Poison, Disease, Burning,
        Hex,
        DeepWound
    };

    struct DegenTracker {
        uint32_t agent_id = 0;
        uint32_t effect_id = 0;
        uint32_t skill_id = 0;
        uint32_t caster_id = 0;
        uint32_t attribute_level = 0;
        double raw_hp_per_sec = 0.0;
        double actual_hp_per_sec = 0.0;
        clock_t apply_time = 0;
        double accumulated_damage = 0.0;
        DegenType type = DegenType::Hex;
        uint32_t deep_wound_target_max_hp = 0;
    };

    std::vector<DegenTracker> degen_trackers;

    struct AgentDegenInfo {
        double total_cond_damage[4] = {};
        double hex_damage_by_skill[64] = {};
        uint32_t hex_skill_ids[64] = {};
        uint32_t hex_count = 0;
        double total_hex_damage = 0.0;
        double deep_wound_damage = 0.0;
    };
    AgentDegenInfo degen_info;

    static uint32_t GetCasterAttrLevel(uint32_t caster_id, GW::Constants::AttributeByte attr);

    void RecalcAgentDegenRates(uint32_t agent_id);
    void FinalizeDegenTracker(size_t ti, uint32_t target_max_hp = 0);
    void ClearAllDegen();

    static void* cached_hb_frame;
    static void* cached_party_frame;
    static void* GetCachedHBFrame();
    static void* GetCachedPartyFrame();

    std::map<uint32_t, uint32_t> hp_map_nm;
    std::map<uint32_t, uint32_t> hp_map_hm;

    clock_t send_timer = 0;
    std::queue<std::wstring> send_queue;

    float GetPartOfTotal(uint32_t dmg) const;
    float GetPercentageOfTotal(uint32_t dmg) const;
    float GetPartOfTotalHealing(uint32_t heal) const;
    float GetPercentageOfTotalHealing(uint32_t heal) const;

    PlayerDamage* GetDamageByAgentId(uint32_t agent_id, uint32_t* party_idx_out = nullptr);
    void ReconcileDamageIndices();
    void WriteDamageOf(size_t index, uint32_t rank = 0);
    void WritePartyDamage();
    void WriteOwnDamage();
    void ResetDamage();

    void FetchPartyInfo();
    bool CalcPartyPositions();
    void* FindPartyHealthBars();
    uint32_t GetAgentMaxHp(uint32_t agent_id);

    static void MapLoadedCB(GW::HookStatus*, const GW::Packet::StoC::MapLoaded*);
    static void DamagePacketCB(GW::HookStatus*, const GW::Packet::StoC::GenericModifier*);
    static void GenericValueTargetCB(GW::HookStatus*, const GW::Packet::StoC::GenericValueTarget*);
    static void GenericValueCB(GW::HookStatus*, const GW::Packet::StoC::GenericValue*);
    static void AgentStateCB(GW::HookStatus*, const GW::Packet::StoC::AgentState*);
    static void AgentRemoveCB(GW::HookStatus*, const GW::Packet::StoC::AgentRemove*);
    static void CmdDamage(GW::HookStatus*, const wchar_t*, int, const LPWSTR*);

    static GW::HookEntry ChatCmd_Entry;
    static GW::HookEntry GenericModifier_Entry;
    static GW::HookEntry GenericValueTarget_Entry;
    static GW::HookEntry GenericValue_Entry;
    static GW::HookEntry MapLoaded_Entry;
    static GW::HookEntry AgentRemove_Entry;
    static GW::HookEntry AgentState_Entry;
};
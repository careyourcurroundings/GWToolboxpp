#include "PartyDamagePlugin.h"

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Party.h>
#include <GWCA/GameEntities/Player.h>
#include <GWCA/GameEntities/Skill.h>
#include <GWCA/GameEntities/Attribute.h>

#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/StoCMgr.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/PartyMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/PlayerMgr.h>
#include <GWCA/Managers/SkillbarMgr.h>
#include <GWCA/Context/WorldContext.h>

#include <algorithm>
#include <cmath>
#include <queue>

#include <imgui.h>
#include <imgui_internal.h>

namespace {
    inline clock_t TIMER_INIT() { return clock(); }
    inline clock_t TIMER_DIFF(clock_t t) { return TIMER_INIT() - t; }

    PartyDamagePlugin* g_instance = nullptr;
    constexpr size_t BUFFER_SIZE = 16;
    char g_buffer[BUFFER_SIZE];

    const uint32_t BASIC_COND_IDS[] = { 23, 27, 26, 25 };
    const double BASIC_COND_RATES[] = { 6.0, 8.0, 8.0, 14.0 };
    const size_t BASIC_COND_CNT = sizeof(BASIC_COND_IDS) / sizeof(BASIC_COND_IDS[0]);

    int BasicCondIdx(uint32_t eid) {
        for (size_t i = 0; i < BASIC_COND_CNT; i++)
            if (BASIC_COND_IDS[i] == eid) return (int)i;
        return -1;
    }

    // Degen condition colors - red, green, purple, orange
    static const uint32_t COND_COLORS[4] = {
        0xFFC83232,
        0xFF50C878,
        0xFFC864C8,
        0xFFFF7800
    };
    const char* COND_ICON[4] = { ICON_FA_TINT, ICON_FA_TINT, ICON_FA_SKULL, ICON_FA_FIRE };

    struct DegenEntry {
        uint32_t skill_id;
        uint32_t attr_byte;
        double values[21];
    };

    static const DegenEntry DEGEN_TABLE[] = {
        { (uint32_t)GW::Constants::SkillID::Corrupt_Enchantment, 5, {2,2,4,4,6,6,8,8,10,10,12,12,14,14,16,16,16,18,18,20,20} },
        { (uint32_t)GW::Constants::SkillID::Crippling_Anguish, 10, {2,2,4,4,6,6,8,8,10,10,12,12,14,14,16,16,16,18,18,20,20} },
        { (uint32_t)GW::Constants::SkillID::Illusion_of_Pain, 10, {6,6,8,8,10,10,12,12,14,14,16,16,18,18,20,20,20,22,22,24,24} },
        { (uint32_t)GW::Constants::SkillID::Migraine, 10, {2,2,4,4,6,6,8,8,10,10,12,12,14,14,16,16,16,18,18,20,20} },
        { (uint32_t)GW::Constants::SkillID::Faintheartedness, 5, {0,0,0,2,2,2,2,2,4,4,4,4,4,6,6,6,6,6,6,8,8} },
        { (uint32_t)GW::Constants::SkillID::Life_Siphon, 3, {2,2,2,2,4,4,4,4,4,4,4,4,6,6,6,6,6,6,8,8,8} },
        { (uint32_t)GW::Constants::SkillID::Life_Transfer, 3, {6,6,8,8,8,10,10,10,12,12,12,14,14,14,16,16,16,18,18,18,20} },
        { (uint32_t)GW::Constants::SkillID::Parasitic_Bond, 5, {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2} },
        { (uint32_t)GW::Constants::SkillID::Putrid_Bile, 4, {2,2,2,2,4,4,4,4,4,4,4,4,6,6,6,6,6,6,6,8,8} },
        { (uint32_t)GW::Constants::SkillID::Reapers_Mark, 6, {2,2,4,4,4,4,6,6,6,6,8,8,8,8,10,10,10,12,12,12,12} },
        { (uint32_t)GW::Constants::SkillID::Suffering, 5, {0,0,0,2,2,2,2,2,4,4,4,4,4,6,6,6,6,6,8,8,8} },
        { (uint32_t)GW::Constants::SkillID::Vile_Miasma, 4, {2,2,4,4,4,4,6,6,6,6,8,8,8,8,10,10,10,12,12,12,12} },
        { (uint32_t)GW::Constants::SkillID::Weaken_Knees, 5, {2,2,2,4,4,4,4,4,6,6,6,6,6,8,8,8,8,8,10,10,10} },
        { (uint32_t)GW::Constants::SkillID::Well_of_Silence, 5, {2,2,2,4,4,4,4,4,6,6,6,6,6,8,8,8,8,8,10,10,10} },
        { (uint32_t)GW::Constants::SkillID::Well_of_Suffering, 4, {2,2,4,4,4,6,6,6,8,8,8,10,10,10,12,12,12,14,14,14,16} },
        { (uint32_t)GW::Constants::SkillID::Wither, 5, {4,4,4,4,6,6,6,6,6,6,6,6,8,8,8,8,8,8,8,10,10} },
        { (uint32_t)GW::Constants::SkillID::Conjure_Nightmare, 10, {16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16} },
        { (uint32_t)GW::Constants::SkillID::Conjure_Phantasm, 10, {10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10} },
        { (uint32_t)GW::Constants::SkillID::Ether_Nightmare_luxon, 10, {16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16} },
        { (uint32_t)GW::Constants::SkillID::Ether_Nightmare_kurzick, 10, {16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16} },
        { (uint32_t)GW::Constants::SkillID::Images_of_Remorse, 10, {2,2,2,2,4,4,4,4,4,4,4,4,6,6,6,6,6,6,6,8,8} },
        { (uint32_t)GW::Constants::SkillID::Overload, 11, {2,2,2,2,4,4,4,4,4,4,4,4,6,6,6,6,6,6,6,8,8} },
        { (uint32_t)GW::Constants::SkillID::Phantom_Pain, 10, {2,2,2,4,4,4,4,4,6,6,6,6,6,8,8,8,8,8,10,10,10} },
        { (uint32_t)GW::Constants::SkillID::Recurring_Insecurity, 10, {2,2,4,4,4,6,6,6,8,8,8,10,10,10,12,12,12,14,14,14,16} },
        { (uint32_t)GW::Constants::SkillID::Shrinking_Armor, 10, {2,2,2,4,4,4,4,4,6,6,6,6,6,8,8,8,8,8,10,10,10} },
        { (uint32_t)GW::Constants::SkillID::Teinais_Heat, 1, {4,4,4,6,6,6,6,6,8,8,8,8,8,10,10,10,10,10,12,12,12} },
        { (uint32_t)GW::Constants::SkillID::Enduring_Toxin, 13, {2,2,4,4,4,4,6,6,6,6,8,8,8,8,10,10,10,12,12,12,12} },
        { (uint32_t)GW::Constants::SkillID::Mark_of_Insecurity, 13, {2,2,4,4,4,4,6,6,6,6,8,8,8,8,10,10,10,12,12,12,12} },
        { (uint32_t)GW::Constants::SkillID::Lamentation, 18, {0,0,0,2,2,2,2,2,4,4,4,4,4,6,6,6,6,6,8,8,8} },
        { (uint32_t)GW::Constants::SkillID::Radiation_Field, 0, {12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12} },
    };
    static const size_t DEGEN_TABLE_SIZE = sizeof(DEGEN_TABLE) / sizeof(DEGEN_TABLE[0]);

    struct GenericValueTargetRaw {
        uint32_t header;
        uint32_t value_id;
        uint32_t agent_id;
        uint32_t target_id;
        uint32_t value;
    };

    bool GetFramePos(const GW::UI::Frame* frame, const GW::UI::Frame* relative_to,
        ImVec2* top_left, ImVec2* bottom_right) {
        if (!(frame && relative_to && frame->IsVisible())) return false;
        if (!GImGui) return false;
        auto vp = ImGui::GetMainViewport();
        if (top_left) {
            *top_left = frame->position.GetTopLeftOnScreen(relative_to);
            top_left->x = std::round(top_left->x); top_left->y = std::round(top_left->y);
            top_left->x += vp->Pos.x; top_left->y += vp->Pos.y;
        }
        if (bottom_right) {
            *bottom_right = frame->position.GetBottomRightOnScreen(relative_to);
            bottom_right->x = std::round(bottom_right->x); bottom_right->y = std::round(bottom_right->y);
            bottom_right->x += vp->Pos.x; bottom_right->y += vp->Pos.y;
        }
        return true;
    }
}

void* PartyDamagePlugin::cached_hb_frame;
void* PartyDamagePlugin::cached_party_frame;
void* PartyDamagePlugin::GetCachedHBFrame() { return cached_hb_frame; }
void* PartyDamagePlugin::GetCachedPartyFrame() { return cached_party_frame; }

GW::HookEntry PartyDamagePlugin::ChatCmd_Entry;
GW::HookEntry PartyDamagePlugin::GenericModifier_Entry;
GW::HookEntry PartyDamagePlugin::GenericValueTarget_Entry;
GW::HookEntry PartyDamagePlugin::GenericValue_Entry;
GW::HookEntry PartyDamagePlugin::MapLoaded_Entry;
GW::HookEntry PartyDamagePlugin::AgentRemove_Entry;
GW::HookEntry PartyDamagePlugin::AgentState_Entry;

DLLAPI ToolboxPlugin* ToolboxPluginInstance() {
    static PartyDamagePlugin instance;
    return &instance;
}

clock_t PartyDamagePlugin::GetEffectiveCombatTime() const {
    if (first_packet_time == 0) return accumulated_combat_time_ms;
    return accumulated_combat_time_ms + (last_packet_time - first_packet_time);
}

float PartyDamagePlugin::GetPartOfTotal(uint32_t dmg) const {
    return total_damage == 0 ? 0 : static_cast<float>(dmg) / total_damage;
}
float PartyDamagePlugin::GetPercentageOfTotal(uint32_t dmg) const {
    return GetPartOfTotal(dmg) * 100.0f;
}
float PartyDamagePlugin::GetPartOfTotalHealing(uint32_t heal) const {
    return total_healing == 0 ? 0 : static_cast<float>(heal) / total_healing;
}
float PartyDamagePlugin::GetPercentageOfTotalHealing(uint32_t heal) const {
    return GetPartOfTotalHealing(heal) * 100.0f;
}

void PartyDamagePlugin::PlayerDamage::Reset() {
    total_dmg = 0; recent_dmg = 0; last_dmg_time = 0;
    total_heal = 0; recent_heal = 0; last_heal_time = 0;
    agent_id = 0;
    primary = GW::Constants::Profession::None;
    secondary = GW::Constants::Profession::None;
    name.clear();
}

uint32_t PartyDamagePlugin::GetCasterAttrLevel(uint32_t caster_id, GW::Constants::AttributeByte attr) {
    auto* attrs = GW::PartyMgr::GetAgentAttributes(caster_id);
    if (!attrs) return 0;
    for (size_t i = 0; i < 54; i++) {
        if ((uint8_t)attrs[i].id == (uint8_t)attr)
            return attrs[i].level;
        if ((uint32_t)attrs[i].id >= 0xFF) break;
    }
    return 0;
}

PartyDamagePlugin::PlayerDamage* PartyDamagePlugin::GetDamageByAgentId(uint32_t aid, uint32_t* pidx) {
    auto it = party_idx_by_agent_id.find(aid);
    if (it == party_idx_by_agent_id.end()) return nullptr;
    uint32_t idx = it->second;
    if (idx >= player_damage.size() || idx >= pets_start) return nullptr;
    if (pidx) *pidx = idx;
    return &player_damage[idx];
}

void PartyDamagePlugin::ReconcileDamageIndices() {
    if (party_agent_ids == prev_party_agent_ids) return;
    prev_party_agent_ids = party_agent_ids;
    std::unordered_map<uint32_t, uint32_t> old;
    for (uint32_t i = 0; i < (uint32_t)player_damage.size(); i++)
        if (player_damage[i].agent_id) old[player_damage[i].agent_id] = i;
    std::unordered_map<std::wstring, size_t> dep;
    for (size_t i = 0; i < departed_damage.size(); i++)
        if (!departed_damage[i].name.empty()) dep[departed_damage[i].name] = i;
    std::vector<PlayerDamage> nd(party_agent_ids.size());
    std::unordered_set<uint32_t> cl;
    for (auto& [aid, nidx] : party_idx_by_agent_id) {
        if (nidx >= nd.size() || nidx >= pets_start) continue;
        auto it = old.find(aid);
        if (it != old.end()) { nd[nidx] = player_damage[it->second]; nd[nidx].agent_id = aid; cl.insert(it->second); continue; }
        if (nidx < party_names.size()) {
            auto& n = party_names[nidx]; if (n.empty()) continue;
            auto dit = dep.find(n);
            if (dit != dep.end()) { nd[nidx] = departed_damage[dit->second]; nd[nidx].agent_id = aid; departed_damage[dit->second].Reset(); continue; }
            for (uint32_t i = 0; i < (uint32_t)player_damage.size(); i++)
                if (!cl.count(i) && player_damage[i].name == n && !player_damage[i].name.empty())
                { nd[nidx] = player_damage[i]; nd[nidx].agent_id = aid; cl.insert(i); break; }
        }
    }
    for (uint32_t i = 0; i < (uint32_t)player_damage.size(); i++)
        if (!cl.count(i) && (player_damage[i].total_dmg || player_damage[i].total_heal))
            departed_damage.push_back(std::move(player_damage[i]));
    player_damage = std::move(nd);
    total_damage = 0; total_healing = 0;
    for (auto& e : player_damage) { total_damage += e.total_dmg; total_healing += e.total_heal; }
    for (auto& e : departed_damage) { total_damage += e.total_dmg; total_healing += e.total_heal; }
}

void PartyDamagePlugin::WriteDamageOf(size_t index, uint32_t rank) {
    if (index >= player_damage.size()) return;
    auto& p = player_damage[index];
    if (!p.total_dmg && !p.total_heal) return;
    if (p.name.empty()) return;
    if (!rank) {
        rank = 1;
        for (size_t i = 0; i < player_damage.size(); i++)
            if (i != index && player_damage[i].agent_id && player_damage[i].total_dmg > p.total_dmg) rank++;
    }
    auto prof = [](GW::Constants::Profession pr) -> const wchar_t* {
        switch (pr) {
        case GW::Constants::Profession::Warrior: return L"W";
        case GW::Constants::Profession::Ranger: return L"R";
        case GW::Constants::Profession::Monk: return L"Mo";
        case GW::Constants::Profession::Necromancer: return L"N";
        case GW::Constants::Profession::Mesmer: return L"Me";
        case GW::Constants::Profession::Elementalist: return L"E";
        case GW::Constants::Profession::Assassin: return L"A";
        case GW::Constants::Profession::Ritualist: return L"Rt";
        case GW::Constants::Profession::Paragon: return L"P";
        case GW::Constants::Profession::Dervish: return L"D";
        default: return L"?";
        }
    };
    const wchar_t* pp = prof(p.primary);
    const wchar_t* ps = p.secondary == GW::Constants::Profession::None ? L"" : prof(p.secondary);
    wchar_t buf[200];
    bool hd = p.total_dmg > 0, hh = settings.show_healing && p.total_heal > 0;
    if (hd && hh) swprintf_s(buf, L"#%2d ~ %ls/%ls %ls ~ Dmg: %3.2f%% (%d) ~ Heal: %3.2f%% (%d)", rank, pp, ps, p.name.c_str(), GetPercentageOfTotal(p.total_dmg), p.total_dmg, GetPercentageOfTotalHealing(p.total_heal), p.total_heal);
    else if (hd) swprintf_s(buf, L"#%2d ~ %ls/%ls %ls ~ Dmg: %3.2f%% (%d)", rank, pp, ps, p.name.c_str(), GetPercentageOfTotal(p.total_dmg), p.total_dmg);
    else if (hh) swprintf_s(buf, L"#%2d ~ %ls/%ls %ls ~ Heal: %3.2f%% (%d)", rank, pp, ps, p.name.c_str(), GetPercentageOfTotalHealing(p.total_heal), p.total_heal);
    else return;
    send_queue.push(buf);
}

void PartyDamagePlugin::WritePartyDamage() {
    size_t base = player_damage.size();
    for (auto& e : departed_damage) if (e.total_dmg || e.total_heal) player_damage.push_back(e);
    std::vector<size_t> idx(player_damage.size());
    for (size_t i = 0; i < player_damage.size(); i++) idx[i] = i;
    auto& dr = player_damage;
    std::sort(idx.begin(), idx.end(), [&dr](size_t a, size_t b) { return dr[a].total_dmg > dr[b].total_dmg; });
    for (size_t i = 0; i < idx.size(); i++) WriteDamageOf(idx[i], i + 1);
    send_queue.push(L"Total ~ Dmg: " + std::to_wstring(total_damage) + L" ~ Heal: " + std::to_wstring(total_healing));
    player_damage.resize(base);
}

void PartyDamagePlugin::WriteOwnDamage() {
    uint32_t idx = 0;
    if (GetDamageByAgentId(GW::Agents::GetControlledCharacterId(), &idx)) WriteDamageOf(idx);
}

void PartyDamagePlugin::ResetDamage() {
    total_damage = 0; total_healing = 0;
    for (auto& e : player_damage) e.Reset();
    departed_damage.clear(); prev_party_agent_ids.clear();
    first_packet_time = 0; last_packet_time = 0; accumulated_combat_time_ms = 0;
    ClearAllDegen();
}

uint32_t PartyDamagePlugin::GetAgentMaxHp(uint32_t agent_id) {
    auto living = (GW::AgentLiving*)GW::Agents::GetAgentByID(agent_id);
    if (living && living->max_hp > 0 && living->max_hp < 100000) return living->max_hp;
    auto& hm = GW::PartyMgr::GetIsPartyInHardMode() ? hp_map_hm : hp_map_nm;
    if (living) { auto it = hm.find(living->player_number); if (it != hm.end()) return it->second; }
    return 0;
}

void PartyDamagePlugin::RecalcAgentDegenRates(uint32_t agent_id) {
    std::vector<size_t> idxs;
    for (size_t i = 0; i < degen_trackers.size(); i++)
        if (degen_trackers[i].agent_id == agent_id && degen_trackers[i].type != DegenType::DeepWound)
            idxs.push_back(i);
    if (idxs.empty()) return;
    double total_raw = 0.0;
    for (auto i : idxs) total_raw += degen_trackers[i].raw_hp_per_sec;
    if (total_raw <= DEGEN_CAP_HP_SEC) {
        for (auto i : idxs) degen_trackers[i].actual_hp_per_sec = degen_trackers[i].raw_hp_per_sec;
        return;
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t a, size_t b) { return degen_trackers[a].apply_time > degen_trackers[b].apply_time; });
    double cap_remaining = DEGEN_CAP_HP_SEC;
    for (size_t ti = 0; ti < idxs.size(); ti++) {
        auto& dt = degen_trackers[idxs[ti]];
        if (ti == idxs.size() - 1) dt.actual_hp_per_sec = (std::max)(0.0, (std::min)(dt.raw_hp_per_sec, cap_remaining));
        else { dt.actual_hp_per_sec = (std::min)(dt.raw_hp_per_sec, cap_remaining); cap_remaining -= dt.actual_hp_per_sec; if (cap_remaining < 0) cap_remaining = 0; }
    }
}

void PartyDamagePlugin::FinalizeDegenTracker(size_t ti, uint32_t target_max_hp) {
    if (ti >= degen_trackers.size()) return;
    auto& dt = degen_trackers[ti];
    double elapsed = (double)(TIMER_INIT() - dt.apply_time) / 1000.0;
    double contrib = dt.actual_hp_per_sec * elapsed;
    dt.accumulated_damage += contrib;

    if (dt.type == DegenType::DeepWound) {
        uint32_t mhp = dt.deep_wound_target_max_hp ? dt.deep_wound_target_max_hp : target_max_hp;
        if (mhp > 0) { double dw_dmg = (std::min)((double)(mhp / 5), (double)DEEP_WOUND_DMG_CAP); dt.accumulated_damage += dw_dmg; }
        degen_info.deep_wound_damage += dt.accumulated_damage;
    } else if (dt.type == DegenType::Hex) {
        int slot = -1;
        for (uint32_t si = 0; si < degen_info.hex_count; si++)
            if (degen_info.hex_skill_ids[si] == dt.skill_id) { slot = (int)si; break; }
        if (slot < 0 && degen_info.hex_count < 64) {
            slot = (int)degen_info.hex_count++;
            degen_info.hex_skill_ids[slot] = dt.skill_id;
            degen_info.hex_damage_by_skill[slot] = 0.0;
        }
        if (slot >= 0) degen_info.hex_damage_by_skill[slot] += dt.accumulated_damage;
        degen_info.total_hex_damage += dt.accumulated_damage;
    } else if (dt.type >= DegenType::Bleeding && dt.type <= DegenType::Burning) {
        int ci = (int)dt.type - (int)DegenType::Bleeding;
        if (ci >= 0 && ci < 4) degen_info.total_cond_damage[ci] += dt.accumulated_damage;
    }
    degen_trackers[ti] = degen_trackers.back();
    degen_trackers.pop_back();
}

void PartyDamagePlugin::ClearAllDegen() {
    degen_trackers.clear();
    for (size_t i = 0; i < 4; i++) degen_info.total_cond_damage[i] = 0.0;
    degen_info.total_hex_damage = 0.0;
    degen_info.hex_count = 0;
    for (uint32_t i = 0; i < 64; i++) { degen_info.hex_skill_ids[i] = 0; degen_info.hex_damage_by_skill[i] = 0.0; }
    degen_info.deep_wound_damage = 0.0;
}

void PartyDamagePlugin::FetchPartyInfo() {
    party_idx_by_agent_id.clear(); party_agent_ids.clear();
    auto info = GW::PartyMgr::GetPartyInfo();
    if (!info) return;
    auto append = [&](uint32_t aid, const wchar_t* enc) {
        if (party_idx_by_agent_id.count(aid)) return;
        party_idx_by_agent_id[aid] = (uint32_t)party_agent_ids.size();
        party_agent_ids.push_back(aid);
        while (party_names.size() < party_agent_ids.size()) party_names.emplace_back();
        auto& s = party_names.back();
        if (enc) s = enc;
        else { auto e = GW::Agents::GetAgentEncName(aid); if (e) s = e; }
    };
    for (auto& pl : info->players) {
        if (auto gp = GW::PlayerMgr::GetPlayerByID(pl.login_number)) append(gp->agent_id, gp->name_enc);
        for (auto& h : info->heroes) if (h.owner_player_id == pl.login_number) append(h.agent_id, nullptr);
    }
    henchmen_start = (uint32_t)party_idx_by_agent_id.size();
    for (auto& h : info->henchmen) append(h.agent_id, nullptr);
    pets_start = (uint32_t)party_idx_by_agent_id.size();
    if (auto w = GW::GetWorldContext()) for (auto& p : w->pets) append(p.agent_id, nullptr);
    for (auto& o : info->others) append(o, nullptr);
}

void* PartyDamagePlugin::FindPartyHealthBars() {
    if (cached_hb_frame && ((GW::UI::Frame*)cached_hb_frame)->IsVisible()) return cached_hb_frame;
    auto party = GW::PartyMgr::GetPartyInfo();
    auto pf = party ? GW::UI::GetFrameByLabel(L"Party") : nullptr;
    if (!pf || !pf->IsVisible()) { cached_party_frame = nullptr; return nullptr; }
    cached_party_frame = pf;
    GW::UI::Frame* r = nullptr;
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost) {
        auto s = GW::UI::GetChildFrame(pf, 1); s = GW::UI::GetChildFrame(s, 8); s = GW::UI::GetChildFrame(s, 0); s = GW::UI::GetChildFrame(s, 0);
        r = GW::UI::GetChildFrame(s, 0);
    } else {
        auto s = GW::UI::GetChildFrame(pf, 0); s = GW::UI::GetChildFrame(s, 0);
        r = GW::UI::GetChildFrame(s, 0);
    }
    cached_hb_frame = r;
    return r;
}

bool PartyDamagePlugin::CalcPartyPositions() {
    if (!agent_health_bar_pos.empty()) return true;
    agent_health_bar_pos.clear();
    auto hb = (GW::UI::Frame*)FindPartyHealthBars();
    if (!hb) return false;
    auto pf = (GW::UI::Frame*)cached_party_frame;
    if (!pf) return false;
    auto rel = pf;
    auto hp = GW::UI::GetParentFrame(hb); if (!hp) return false;
    GetFramePos(hp, rel, &party_health_bars_pos.top_left, &party_health_bars_pos.bottom_right);
    ImVec2 tl, br;
    GetFramePos(pf, rel, &tl, &br);
    float d = (party_health_bars_pos.top_left.x - tl.x) / 2.f;
    party_health_bars_pos.top_left.x -= d; party_health_bars_pos.bottom_right.x += d;
    auto phb = GW::UI::GetChildFrame(hb, 0); if (!phb) return false;
    auto pi = GW::PartyMgr::GetPartyInfo(); if (!pi) return false;
    for (auto& pl : pi->players) {
        auto c = GW::UI::GetChildFrame(phb, pl.login_number); if (!c) continue;
        auto ah = GW::UI::GetChildFrame(c, 0); if (!ah) continue;
        auto aid = GW::PlayerMgr::GetPlayerAgentId(pl.login_number); if (!aid) continue;
        GetFramePos(ah, rel, &tl, &br); agent_health_bar_pos[aid] = { tl, br };
        for (auto& h : pi->heroes) {
            if (h.owner_player_id != pl.login_number) continue;
            ah = GW::UI::GetChildFrame(c, 5 + h.agent_id); if (!ah) continue;
            GetFramePos(ah, rel, &tl, &br); agent_health_bar_pos[h.agent_id] = { tl, br };
        }
    }
    auto hhb = GW::UI::GetChildFrame(hb, 1);
    for (auto& h : pi->henchmen) { if (!hhb) continue; auto ah = GW::UI::GetChildFrame(hhb, h.agent_id); if (!ah) continue; GetFramePos(ah, rel, &tl, &br); agent_health_bar_pos[h.agent_id] = { tl, br }; }
    auto ph = GW::UI::GetChildFrame(hb, 3); if (!(ph && ph->IsVisible())) ph = nullptr;
    auto ah = GW::UI::GetChildFrame(hb, 4); if (!(ah && ah->IsVisible())) ah = nullptr;
    for (auto& oid : pi->others) { auto f = ph ? GW::UI::GetChildFrame(ph, oid) : nullptr; if (!f) f = ah ? GW::UI::GetChildFrame(ah, oid) : nullptr; if (!f) continue; GetFramePos(f, rel, &tl, &br); agent_health_bar_pos[oid] = { tl, br }; }
    return true;
}

void PartyDamagePlugin::MapLoadedCB(GW::HookStatus*, const GW::Packet::StoC::MapLoaded*) {
    if (!g_instance) return;
    // Always recalculate positions on zone change - cached frame pointers become invalid
    g_instance->party_health_bars_pos = {};
    g_instance->agent_health_bar_pos.clear();
    cached_hb_frame = nullptr;
    cached_party_frame = nullptr;
    // Do NOT reset damage data - persist across zones until plugin restart or /dmg reset
}

void PartyDamagePlugin::DamagePacketCB(GW::HookStatus*, const GW::Packet::StoC::GenericModifier* pkt) {
    if (!g_instance) return;
    switch (pkt->type) {
    case GW::Packet::StoC::GenericValueID::damage:
    case GW::Packet::StoC::GenericValueID::critical:
    case GW::Packet::StoC::GenericValueID::armorignoring: break;
    default: return;
    }
    bool heal = pkt->value > 0, dmg = pkt->value < 0;
    if (!heal && !dmg) return;
    auto cause = (GW::AgentLiving*)GW::Agents::GetAgentByID(pkt->cause_id);
    if (!(cause && cause->GetIsLivingType()) || cause->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;
    uint32_t pidx = 0;
    auto e = g_instance->GetDamageByAgentId(cause->agent_id, &pidx);
    if (!e) return;
    auto tgt = (GW::AgentLiving*)GW::Agents::GetAgentByID(pkt->target_id);
    if (!(tgt && tgt->GetIsLivingType())) return;
    if (dmg) {
        if (tgt->login_number) return;
        switch (tgt->allegiance) {
        case GW::Constants::Allegiance::Ally_NonAttackable:
        case GW::Constants::Allegiance::Spirit_Pet:
        case GW::Constants::Allegiance::Minion: return;
        default: break;
        }
    } else {
        switch (tgt->allegiance) {
        case GW::Constants::Allegiance::Ally_NonAttackable:
        case GW::Constants::Allegiance::Spirit_Pet:
        case GW::Constants::Allegiance::Minion: break;
        default: return;
        }
    }
    auto& hp = GW::PartyMgr::GetIsPartyInHardMode() ? g_instance->hp_map_hm : g_instance->hp_map_nm;
    float mag1 = heal ? 1.0f - tgt->hp : tgt->hp, mag2 = std::fabs(pkt->value);
    float mag = mag1 < mag2 ? mag1 : mag2;
    long lv;
    if (tgt->max_hp > 0 && tgt->max_hp < 100000) { lv = std::lround(mag * tgt->max_hp); hp[tgt->player_number] = tgt->max_hp; }
    else { auto it = hp.find(tgt->player_number); lv = std::lround(mag * (it == hp.end() ? (double)(tgt->level * 20 + 100) : (double)it->second)); }
    uint32_t amt = (uint32_t)lv;
    if (!e->total_dmg && !e->total_heal) { e->agent_id = pkt->cause_id; e->primary = (GW::Constants::Profession)cause->primary; e->secondary = (GW::Constants::Profession)cause->secondary; }
    if (e->name.empty() && pidx < g_instance->party_names.size()) { auto& s = g_instance->party_names[pidx]; if (!s.empty()) e->name = s; }
    if (dmg) {
        clock_t t = TIMER_INIT(); e->total_dmg += amt; g_instance->total_damage += amt; e->recent_dmg += amt; e->last_dmg_time = t;
        if (!g_instance->first_packet_time) { g_instance->first_packet_time = t; g_instance->last_packet_time = t; }
        else { clock_t el = t - g_instance->last_packet_time; if (el > 5000 && g_instance->first_packet_time) { g_instance->accumulated_combat_time_ms += g_instance->last_packet_time - g_instance->first_packet_time; g_instance->first_packet_time = t; } g_instance->last_packet_time = t; }
    } else { e->total_heal += amt; g_instance->total_healing += amt; e->recent_heal += amt; e->last_heal_time = TIMER_INIT(); }
}

void PartyDamagePlugin::GenericValueTargetCB(GW::HookStatus*, const GW::Packet::StoC::GenericValueTarget* pkt_raw) {
    if (!g_instance) return;
    auto pkt = (const GenericValueTargetRaw*)pkt_raw;
    if (pkt->value_id != GW::Packet::StoC::GenericValueID::skill_activated) return;

    uint32_t caster_id = pkt->agent_id;
    uint32_t target_id = pkt->target_id;
    uint32_t skill_id = pkt->value;

    auto tgt = (GW::AgentLiving*)GW::Agents::GetAgentByID(target_id);
    if (!tgt || tgt->allegiance != GW::Constants::Allegiance::Enemy) return;
    auto cause = (GW::AgentLiving*)GW::Agents::GetAgentByID(caster_id);
    if (!(cause && cause->GetIsLivingType()) || cause->allegiance != GW::Constants::Allegiance::Ally_NonAttackable) return;

    const auto* entry = (const DegenEntry*)nullptr;
    for (size_t i = 0; i < DEGEN_TABLE_SIZE; i++) {
        if (DEGEN_TABLE[i].skill_id == skill_id) { entry = &DEGEN_TABLE[i]; break; }
    }
    if (!entry) return;

    uint32_t attr_level = GetCasterAttrLevel(caster_id, (GW::Constants::AttributeByte)entry->attr_byte);
    uint32_t ca = attr_level < 20u ? attr_level : 20u;
    double hp_per_sec = entry->values[ca];
    uint32_t effect_id = skill_id;

    for (auto& dt : g_instance->degen_trackers) {
        if (dt.agent_id == target_id && dt.effect_id == effect_id && dt.type == DegenType::Hex) {
            dt.attribute_level = attr_level;
            dt.raw_hp_per_sec = hp_per_sec;
            dt.apply_time = TIMER_INIT();
            g_instance->RecalcAgentDegenRates(target_id);
            return;
        }
    }

    if (g_instance->degen_trackers.size() >= MAX_DEGEN_TRACKERS) return;

    DegenTracker dt{};
    dt.agent_id = target_id; dt.effect_id = effect_id; dt.skill_id = skill_id;
    dt.caster_id = caster_id; dt.attribute_level = attr_level;
    dt.raw_hp_per_sec = hp_per_sec; dt.apply_time = TIMER_INIT();
    dt.type = DegenType::Hex;
    g_instance->degen_trackers.push_back(dt);
    g_instance->RecalcAgentDegenRates(target_id);
}

void PartyDamagePlugin::GenericValueCB(GW::HookStatus*, const GW::Packet::StoC::GenericValue* pkt) {
    if (!g_instance) return;
    if (pkt->value_id == GW::Packet::StoC::GenericValueID::add_effect) {
        int ci = BasicCondIdx(pkt->value); if (ci < 0) return;
        auto tgt = (GW::AgentLiving*)GW::Agents::GetAgentByID(pkt->agent_id);
        if (!tgt || tgt->allegiance != GW::Constants::Allegiance::Enemy) return;
        for (auto& dt : g_instance->degen_trackers)
            if (dt.agent_id == pkt->agent_id && dt.effect_id == pkt->value) { dt.apply_time = TIMER_INIT(); g_instance->RecalcAgentDegenRates(pkt->agent_id); return; }
        if (g_instance->degen_trackers.size() >= MAX_DEGEN_TRACKERS) return;
        DegenTracker dt{}; dt.agent_id = pkt->agent_id; dt.effect_id = pkt->value;
        dt.raw_hp_per_sec = BASIC_COND_RATES[ci]; dt.apply_time = TIMER_INIT(); dt.type = (DegenType)ci;
        g_instance->degen_trackers.push_back(dt);
        g_instance->RecalcAgentDegenRates(pkt->agent_id);
    }
    if (pkt->value_id == GW::Packet::StoC::GenericValueID::remove_effect) {
        for (size_t i = 0; i < g_instance->degen_trackers.size(); i++) {
            auto& dt = g_instance->degen_trackers[i];
            if (dt.agent_id == pkt->agent_id && dt.effect_id == pkt->value && dt.type >= DegenType::Bleeding && dt.type <= DegenType::Burning) {
                g_instance->FinalizeDegenTracker(i);
                g_instance->RecalcAgentDegenRates(pkt->agent_id);
                return;
            }
        }
    }
}

void PartyDamagePlugin::AgentStateCB(GW::HookStatus*, const GW::Packet::StoC::AgentState* pkt) {
    if (!g_instance) return;
    if ((pkt->state & 16) != 0) {
        uint32_t mhp = g_instance->GetAgentMaxHp(pkt->agent_id);
        for (size_t i = 0; i < g_instance->degen_trackers.size(); i++)
            if (g_instance->degen_trackers[i].agent_id == pkt->agent_id)
                g_instance->FinalizeDegenTracker(i, mhp);
    }
}

void PartyDamagePlugin::AgentRemoveCB(GW::HookStatus*, const GW::Packet::StoC::AgentRemove* pkt) {
    if (!g_instance) return;
    uint32_t mhp = g_instance->GetAgentMaxHp(pkt->agent_id);
    for (size_t i = 0; i < g_instance->degen_trackers.size(); i++)
        if (g_instance->degen_trackers[i].agent_id == pkt->agent_id)
            g_instance->FinalizeDegenTracker(i, mhp);
}

void PartyDamagePlugin::CmdDamage(GW::HookStatus*, const wchar_t*, int argc, const LPWSTR* argv) {
    if (!g_instance) return;
    if (argc <= 1) { g_instance->WritePartyDamage(); return; }
    std::wstring a1 = argv[1]; for (auto& c : a1) c = towlower(c);
    if (a1 == L"print" || a1 == L"report") g_instance->WritePartyDamage();
    else if (a1 == L"me") g_instance->WriteOwnDamage();
    else if (a1 == L"reset") g_instance->ResetDamage();
    else { wchar_t* e = nullptr; uint32_t idx = (uint32_t)wcstoul(argv[1], &e, 10); if (e && *e == 0 && idx > 0) g_instance->WriteDamageOf(idx - 1); }
}

void PartyDamagePlugin::Initialize(ImGuiContext* ctx, ImGuiAllocFns af, HMODULE tdll) {
    ToolboxUIPlugin::Initialize(ctx, af, tdll);
    g_instance = this;
    can_close = true; show_closebutton = true; show_title = true; is_resizable = false; is_movable = false;
    total_damage = 0; send_timer = TIMER_INIT();
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericModifier>(&GenericModifier_Entry, DamagePacketCB, 0x8000);
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::MapLoaded>(&MapLoaded_Entry, MapLoadedCB, 0x8000);
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValueTarget>(&GenericValueTarget_Entry, GenericValueTargetCB, 0x8000);
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValue>(&GenericValue_Entry, GenericValueCB, 0x8000);
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::AgentState>(&AgentState_Entry, AgentStateCB, 0x8000);
    GW::Chat::CreateCommand(&ChatCmd_Entry, L"dmg", CmdDamage);
    GW::Chat::CreateCommand(&ChatCmd_Entry, L"damage", CmdDamage);
    ResetDamage();
}

void PartyDamagePlugin::SignalTerminate() {
    ToolboxUIPlugin::SignalTerminate();
    GW::StoC::RemoveCallbacks(&GenericModifier_Entry); GW::StoC::RemoveCallbacks(&GenericValueTarget_Entry);
    GW::StoC::RemoveCallbacks(&GenericValue_Entry); GW::StoC::RemoveCallbacks(&MapLoaded_Entry);
    GW::StoC::RemoveCallbacks(&AgentState_Entry); GW::Chat::DeleteCommand(&ChatCmd_Entry);
    if (g_instance == this) g_instance = nullptr;
}

bool PartyDamagePlugin::CanTerminate() { return true; }

void PartyDamagePlugin::Update(float) {
    if (!send_queue.empty() && TIMER_DIFF(send_timer) > 600) {
        send_timer = TIMER_INIT();
        if (GW::Map::GetInstanceType() != GW::Constants::InstanceType::Loading && GW::Agents::GetControlledCharacter())
            { GW::Chat::SendChat('#', send_queue.front().c_str()); send_queue.pop(); }
    }
    for (auto& e : player_damage) {
        if (TIMER_DIFF(e.last_dmg_time) > settings.recent_max_time) e.recent_dmg = 0;
        if (TIMER_DIFF(e.last_heal_time) > settings.recent_max_time) e.recent_heal = 0;
    }
    FetchPartyInfo(); ReconcileDamageIndices();
    for (auto& [aid, pidx] : party_idx_by_agent_id) {
        if (pidx >= player_damage.size() || pidx >= party_names.size()) continue;
        auto& n = party_names[pidx]; if (n.empty()) continue;
        if (player_damage[pidx].name.empty() && player_damage[pidx].agent_id) player_damage[pidx].name = n;
        if (!player_damage[pidx].total_dmg && !player_damage[pidx].total_heal) {
            for (auto& d : departed_damage) {
                if (d.name == n && (d.total_dmg || d.total_heal)) {
                    player_damage[pidx] = d; player_damage[pidx].agent_id = aid; d.Reset();
                    total_damage = 0; total_healing = 0;
                    for (auto& e : player_damage) { total_damage += e.total_dmg; total_healing += e.total_heal; }
                    for (auto& e : departed_damage) { total_damage += e.total_dmg; total_healing += e.total_heal; }
                    break;
                }
            }
        }
    }
    agent_health_bar_pos.clear();
}

void PartyDamagePlugin::Draw(IDirect3DDevice9*) {
    if (!*GetVisiblePtr()) return;
    if (settings.hide_in_outpost && GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost) return;
    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading) return;
    clock_t ct = GetEffectiveCombatTime();
    if (party_agent_ids.empty() || !CalcPartyPositions()) return;
    if (player_damage.size() < party_agent_ids.size()) player_damage.resize(party_agent_ids.size());

    uint32_t mr = 0, md = 0, mrh = 0, mh = 0;
    for (auto& i : player_damage) {
        if (mr < i.recent_dmg) mr = i.recent_dmg;
        if (md < i.total_dmg) md = i.total_dmg;
        if (mrh < i.recent_heal) mrh = i.recent_heal;
        if (mh < i.total_heal) mh = i.total_heal;
    }

    auto shade = [](uint32_t c, int a) {
        auto cl = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
        return ((uint32_t)cl((int)(c >> 24) + a, 0, 255) << 24) |
               ((uint32_t)cl((int)((c >> 16) & 0xFF) + a, 0, 255) << 16) |
               ((uint32_t)cl((int)((c >> 8) & 0xFF) + a, 0, 255) << 8) |
               ((uint32_t)cl((int)(c & 0xFF) + a, 0, 255));
    };
    uint32_t dl = shade(settings.color_damage,20), dd = shade(settings.color_damage,-20);
    uint32_t rl = shade(settings.color_recent,20), rd = shade(settings.color_recent,-20);
    uint32_t hl = shade(settings.color_healing,20), hd = shade(settings.color_healing,-20);

    float w = settings.width, uox = (float)abs(settings.user_offset);
    float wx;
    // Right edge of damage window aligns with left edge of party window
    if (settings.overlay_party_window) {
        wx = party_health_bars_pos.top_left.x - w;
        if (settings.user_offset >= 0) wx += uox;
        else wx -= uox;
    } else {
        wx = party_health_bars_pos.top_left.x - uox - w;
        if (settings.user_offset < 0) wx = party_health_bars_pos.bottom_right.x + uox;
    }

    float ch = settings.show_condition_dps ? ImGui::GetTextLineHeight() * 3.0f + 9.0f : ImGui::GetTextLineHeight() + 3.0f;

    ImGui::SetNextWindowPos({ wx, party_health_bars_pos.top_left.y - ch });
    ImGui::SetNextWindowSize({ w, party_health_bars_pos.bottom_right.y - party_health_bars_pos.top_left.y + ch });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,ImVec2(10,10)); ImGui::PushStyleColor(ImGuiCol_WindowBg,0);

    if (ImGui::Begin(Name(), GetVisiblePtr(), ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove)) {
        auto dl2 = ImGui::GetWindowDrawList();

        // Party DPS at top
        {
            uint32_t party_total_dps = 0;
            if (ct > 0) { uint32_t tpd = 0; for (auto& pd : player_damage) tpd += pd.total_dmg; party_total_dps = (uint32_t)std::llround((double)tpd * 1000.0 / ct); }
            double total_degen = 0.0;
            for (size_t i = 0; i < 4; i++) total_degen += degen_info.total_cond_damage[i];
            total_degen += degen_info.total_hex_damage + degen_info.deep_wound_damage;
            uint32_t degen_dps = ct == 0 ? 0 : (uint32_t)std::llround(total_degen * 1000.0 / ct);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255,255,200,255));
            ImGui::Text("Party DPS: %d/s", party_total_dps + degen_dps);
            ImGui::PopStyleColor();
        }

        if (settings.show_condition_dps) {
            auto calc_dps = [&](double d) { return ct == 0 ? 0 : (uint32_t)std::llround(d * 1000.0 / ct); };
            // Line 1: Bleeding (red), Poison (green), Disease (purple)
            for (int ci = 0; ci < 3; ci++) {
                ImGui::PushStyleColor(ImGuiCol_Text, COND_COLORS[ci]);
                ImGui::Text("%s", COND_ICON[ci]); ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s: %.0f total", ci==0?"Bleeding":ci==1?"Poison":"Disease", degen_info.total_cond_damage[ci]); }
                ImGui::SameLine(); ImGui::Text("%d/s", calc_dps(degen_info.total_cond_damage[ci])); ImGui::SameLine();
            }
            ImGui::NewLine();
            // Line 2: Burning (orange), Hex (purple), Deep Wound (red droplet-slash)
            ImGui::PushStyleColor(ImGuiCol_Text, COND_COLORS[3]);
            ImGui::Text("%s", COND_ICON[3]); ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Burning: %.0f total", degen_info.total_cond_damage[3]); }
            ImGui::SameLine(); ImGui::Text("%d/s", calc_dps(degen_info.total_cond_damage[3])); ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150,100,255,255));
            ImGui::Text(ICON_FA_BIOHAZARD); ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip(); ImGui::Text("Hex degen damage:"); ImGui::Separator();
                for (uint32_t si = 0; si < degen_info.hex_count; si++) {
                    ImGui::Text("  Skill %u: %.0f total", degen_info.hex_skill_ids[si], degen_info.hex_damage_by_skill[si]);
                }
                if (degen_info.hex_count == 0) ImGui::Text("  (none)");
                ImGui::EndTooltip();
            }
            ImGui::SameLine(); ImGui::Text("%d/s", calc_dps(degen_info.total_hex_damage));
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255,50,50,255));
            ImGui::Text(ICON_FA_TINT_SLASH); ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Deep Wound: %.0f total damage from death blows", degen_info.deep_wound_damage); }
            ImGui::SameLine(); ImGui::Text("%d/s", calc_dps(degen_info.deep_wound_damage));
        }

        for (auto& [aid, _] : party_idx_by_agent_id) {
            (void)_;
            uint32_t tidx = 0; auto e = GetDamageByAgentId(aid, &tidx);
            if (!e) continue;
            auto hp_it = agent_health_bar_pos.find(aid);
            if (hp_it == agent_health_bar_pos.end()) continue;
            auto& hp = hp_it->second;
            ImVec2 tl = { wx, hp.top_left.y }, br = { tl.x + w, hp.bottom_right.y };
            dl2->AddRectFilled(tl, br, settings.color_background);
            float df = (float)e->total_dmg;
            if (df >= 0) {
                float pom = md > 0 ? df / md : 0;
                float bx = settings.bars_left ? tl.x + w * (1 - pom) : tl.x;
                float be = settings.bars_left ? tl.x + w : tl.x + w * pom;
                dl2->AddRectFilledMultiColor({ bx, tl.y }, { be, br.y }, dl, dl, dd, dd);
            }
            if (settings.show_damage && e->recent_dmg) {
                float p = mr > 0 ? (float)e->recent_dmg / mr : 0;
                float lx = settings.bars_left ? tl.x + w * (1 - p) : tl.x;
                float rx = settings.bars_left ? tl.x + w : tl.x + w * p;
                dl2->AddRectFilledMultiColor({ lx, br.y - 6 }, { rx, br.y }, rl, rl, rd, rd);
            }
            if (settings.show_healing && e->recent_heal) {
                float p = mrh > 0 ? (float)e->recent_heal / mrh : 0;
                float lx = settings.bars_left ? tl.x + w * (1 - p) : tl.x;
                float rx = settings.bars_left ? tl.x + w : tl.x + w * p;
                dl2->AddRectFilledMultiColor({ lx, tl.y }, { rx, tl.y + 6 }, hl, hl, hd, hd);
            }
            float rh2 = br.y - tl.y, th = ImGui::GetTextLineHeight(), ty = tl.y + (rh2 - th) / 2;

            // Draw text with no clip rect to allow overflow
            if (settings.show_damage && e->total_dmg) {
                if (df < 1000) snprintf(g_buffer, BUFFER_SIZE, "%.0f", df);
                else if (df < 10000) snprintf(g_buffer, BUFFER_SIZE, "%.2f k", df/1000);
                else if (df < 1000000) snprintf(g_buffer, BUFFER_SIZE, "%.1f k", df/1000);
                else snprintf(g_buffer, BUFFER_SIZE, "%.2f m", df/1000000);
                dl2->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { tl.x + ImGui::GetStyle().ItemSpacing.x, ty }, IM_COL32(255,255,255,255), g_buffer, NULL, w * 2.0f);
                if (!settings.show_healing) {
                    snprintf(g_buffer, BUFFER_SIZE, "%.1f %%", GetPercentageOfTotal(e->total_dmg));
                    dl2->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { tl.x + w/2, ty }, IM_COL32(255,255,255,255), g_buffer, NULL, w * 2.0f);
                }
            }
            if (settings.show_dps && settings.show_damage && e->total_dmg) {
                uint32_t dps = ct == 0 ? 0 : (uint32_t)std::llround((double)e->total_dmg * 1000.0 / ct);
                snprintf(g_buffer, BUFFER_SIZE, "%d/s", dps);
                dl2->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { tl.x + w * 0.75f, ty }, IM_COL32(255,255,255,255), g_buffer, NULL, w * 2.0f);
            }
            if (settings.show_healing && e->total_heal) {
                float hf = (float)e->total_heal;
                if (hf < 1000) snprintf(g_buffer, BUFFER_SIZE, "%.0f", hf);
                else if (hf < 10000) snprintf(g_buffer, BUFFER_SIZE, "%.2f k", hf/1000);
                else if (hf < 1000000) snprintf(g_buffer, BUFFER_SIZE, "%.1f k", hf/1000);
                else snprintf(g_buffer, BUFFER_SIZE, "%.2f m", hf/1000000);
                float hx = settings.show_damage ? tl.x + w/2 : tl.x + ImGui::GetStyle().ItemSpacing.x;
                dl2->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { hx, ty }, settings.color_healing, g_buffer, NULL, w * 2.0f);
                if (!settings.show_damage) {
                    snprintf(g_buffer, BUFFER_SIZE, "%.1f %%", GetPercentageOfTotalHealing(e->total_heal));
                    dl2->AddText(ImGui::GetFont(), ImGui::GetFontSize(), { tl.x + w/2, ty }, settings.color_healing, g_buffer, NULL, w * 2.0f);
                }
            }
            if (settings.print_by_click && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(tl, br) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
                WriteDamageOf(tidx, tidx + 1);
        }
    }
    ImGui::End(); ImGui::PopStyleColor(1); ImGui::PopStyleVar(3);
}

void PartyDamagePlugin::DrawSettings() {
    ToolboxUIPlugin::DrawSettings();
    ImGui::Checkbox("Hide in outpost", &settings.hide_in_outpost);
    ImGui::Checkbox("Print Player Damage by Ctrl + Click", &settings.print_by_click);
    ImGui::Checkbox("Bars towards the left", &settings.bars_left);
    ImGui::Checkbox("Show damage", &settings.show_damage);
    ImGui::Checkbox("Show healing", &settings.show_healing);
    ImGui::Checkbox("Show DPS", &settings.show_dps);
    ImGui::Checkbox("Show Condition DPS", &settings.show_condition_dps);
    ImGui::Checkbox("Show on top of health bars", &settings.overlay_party_window);
    ImGui::SameLine(); ImGui::DragInt("Party window offset", &settings.user_offset);
    ImGui::DragFloat("Width", &settings.width, 1.0f, 50.0f, 1000.0f, "%.0f");
    if (settings.width <= 0) settings.width = 1.0f;
    ImGui::DragInt("Timeout", &settings.recent_max_time, 10.0f, 1000, 10000, "%d milliseconds");
    if (settings.recent_max_time < 0) settings.recent_max_time = 0;
}

void PartyDamagePlugin::LoadSettings(const wchar_t* f) {
    ToolboxUIPlugin::LoadSettings(f);
    LoadSetting("hide_in_outpost", settings.hide_in_outpost); LoadSetting("print_by_click", settings.print_by_click);
    LoadSetting("bars_left", settings.bars_left); LoadSetting("show_damage", settings.show_damage);
    LoadSetting("show_healing", settings.show_healing); LoadSetting("show_dps", settings.show_dps);
    LoadSetting("show_condition_dps", settings.show_condition_dps);
    LoadSetting("overlay_party_window", settings.overlay_party_window); LoadSetting("user_offset", settings.user_offset);
    LoadSetting("width", settings.width); LoadSetting("recent_max_time", settings.recent_max_time);
    LoadSetting("color_background", settings.color_background); LoadSetting("color_damage", settings.color_damage);
    LoadSetting("color_recent", settings.color_recent); LoadSetting("color_healing", settings.color_healing);
}

void PartyDamagePlugin::SaveSettings(const wchar_t* f) {
    SaveSetting("hide_in_outpost", settings.hide_in_outpost); SaveSetting("print_by_click", settings.print_by_click);
    SaveSetting("bars_left", settings.bars_left); SaveSetting("show_damage", settings.show_damage);
    SaveSetting("show_healing", settings.show_healing); SaveSetting("show_dps", settings.show_dps);
    SaveSetting("show_condition_dps", settings.show_condition_dps);
    SaveSetting("overlay_party_window", settings.overlay_party_window); SaveSetting("user_offset", settings.user_offset);
    SaveSetting("width", settings.width); SaveSetting("recent_max_time", settings.recent_max_time);
    SaveSetting("color_background", settings.color_background); SaveSetting("color_damage", settings.color_damage);
    SaveSetting("color_recent", settings.color_recent); SaveSetting("color_healing", settings.color_healing);
    ToolboxUIPlugin::SaveSettings(f);
}
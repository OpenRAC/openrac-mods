// SPDX-License-Identifier: MIT
// Simulates vendor memory, pad and keyboard inputs, and the HUD text API.
#include "openrac/mod.hpp"
#include "rac1_vendor.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
void load_rac1(openrac::Mod&);
namespace {
std::vector<std::uint8_t> ram(0x02000000u);
openrac_frame_fn frame_fn;
openrac_pad_fn pad_fn;
void* frame_user;
void* pad_user;
int balance=100, plays=0, last_sound=-1, hud_calls=0, hud_clears=0;
bool visible=false;
std::vector<openrac_hud_text> hud;
openrac_input keyboard{};
const char* controller_setting="xbox";
const vendor_buy_all::rac1::Disc* disc;
void check(bool ok, const char* why) {
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(EXIT_FAILURE); }
}
openrac::Memory memory() { return openrac::Memory(ram.data()); }
int sound(int id, int mode, openrac::gaddr owner) {
    check(mode==0 && owner==0x6000u, "RAC1 native sound mode and owner");
    ++plays; last_sound=id; return 1;
}
void* function(const char* name) {
    if (std::string(name)==disc->sound) return reinterpret_cast<void*>(&sound);
    return nullptr;
}
void pad(bool down) {
    std::uint8_t report[2]={0xFF,static_cast<std::uint8_t>(down?0x7F:0xFF)};
    pad_fn(pad_user,0,report,2);
    check(report[0]==0xFF && report[1]==(down?0x7F:0xFF), "normal vendor input is preserved");
}
void tick() { frame_fn(frame_user); }
void check_hud(const std::string& expected) {
    check(hud.size()==5, "four outline passes and a single foreground line");
    for (size_t i=0;i<hud.size();++i) {
        const auto& line=hud[i];
        check(std::string(line.text)==expected, "all copies display the expected input and status");
        check(line.scale==0.85f && (line.flags & OPENRAC_HUD_SMALL),
              "small native font on all copies");
        if (i+1==hud.size()) {
            check(line.x==0.50f && line.y==0.716f,
                  "single visible line centred above the carousel");
            check(line.rgba[3]==0xFF && (line.flags & OPENRAC_HUD_SHADOW),
                  "foreground opaque with native shadow");
        } else {
            check(line.rgba[0]==0 && line.rgba[1]==0 && line.rgba[2]==0 &&
                  line.rgba[3]==0x98,"subtle dark outline");
        }
    }
}
}
int main(int argc, char** argv) {
    const std::string version=argc>1?argv[1]:"pal";
    for (const auto& d : vendor_buy_all::rac1::discs) if (version==d.version) disc=&d;
    check(disc!=nullptr,"known disc fixture");
    controller_setting=(argc>2 && std::string(argv[2])=="playstation") ? "playstation" : "xbox";
    const std::string pad_label=std::string(controller_setting)=="playstation" ? "Square" : "X";
    keyboard.size=sizeof keyboard;
    keyboard.focused=1;
    openrac_api api{};
    api.size=sizeof api; api.api_major=1; api.api_minor=20;
    api.game="rac1"; api.version=disc->version; api.memory=ram.data();
    api.log=[](openrac_mod*,int,const char*){};
    api.setting_bool=[](openrac_mod*,const char*){return 1;};
    api.setting_string=[](openrac_mod*,const char*){return controller_setting;};
    api.input=[]() -> const openrac_input* {return &keyboard;};
    api.function=&function;
    api.hook=[](openrac_mod*,const char*,const char*,void*,void**) -> int {
        check(false,"native capture hook should be untouched");
        return -1;
    };
    api.hud_text_set=[](openrac_mod*,const openrac_hud_text* lines,int count) {
        ++hud_calls;
        hud.clear();
        if (!lines || count==0) { ++hud_clears; return 0; }
        check(count==5, "five layered HUD text passes");
        hud.assign(lines,lines+count);
        return 0;
    };
    api.on_frame=[](openrac_mod*,openrac_frame_fn fn,void* user){frame_fn=fn;frame_user=user;return 0;};
    api.on_pad=[](openrac_mod*,openrac_pad_fn fn,void* user){pad_fn=fn;pad_user=user;return 0;};
    api.loaded_level=[](){return 4;};
    api.relocate_data=[](openrac_gaddr a){return a;};
    api.relocate_level_data=[](int level,openrac_gaddr a){check(level==0,"level-0 relocation");return a;};
    api.bolts=[](int add){balance+=add;return balance;};
    api.touch_hint=[](openrac_mod*,const char*,int on){visible=on!=0;};
    auto m=memory();
    m.write<std::int32_t>(disc->mode,5);
    m.write<std::int32_t>(disc->vendor,1);
    m.write<openrac::gaddr>(disc->vendor+0x1C,0x6000u);
    m.write<std::int32_t>(disc->vendor+0x210,1);
    m.write<std::int32_t>(disc->vendor+0xD0,2);
    m.write<std::int32_t>(disc->vendor+0xD4,1);
    m.write<std::uint8_t>(disc->owned+2,1);
    m.write<std::int32_t>(disc->ammo+8,3);
    m.write<std::uint16_t>(disc->table+2*0x18+8,5);
    m.write<std::uint16_t>(disc->table+2*0x18+0xA,10);
    m.write<std::uint16_t>(disc->table+2*0x18+0xE,10);
    openrac::Mod mod(&api,reinterpret_cast<openrac_mod*>(1));
    load_rac1(mod); tick();
    check(hud_calls==1 && hud_clears==0,"initial quote published");
    check_hud(pad_label+" Buy All - 35 bolts");
    keyboard.keys[0x09]=OPENRAC_KEY_PRESSED;
    tick(); check_hud("F Buy All - 35 bolts");
    keyboard.keys[0x09]=0;
    tick(); check(hud_calls==2,"unchanged HUD not repeatedly submitted");
    check(visible && plays==0,"idle quote silent and touch action available");
    pad(true); tick();
    check(balance==65 && m.read<std::int32_t>(disc->ammo+8)==10 && plays==1 && last_sound==7,
          "one paid refill and native purchase sound");
    check_hud(pad_label+" Buy All - Bought");
    tick(); check(plays==1 && balance==65,"holding Square does not buy twice");
    pad(false); tick(); pad(true); tick();
    check(balance==65 && plays==2 && last_sound==2,"full ammo rejects without charge");
    pad(false); tick(); balance=1; m.write<std::int32_t>(disc->ammo+8,3);
    pad(true); tick();
    check(balance==1 && plays==3 && last_sound==2 && m.read<std::int32_t>(disc->ammo+8)==3,
          "unaffordable ammo rejects without modifying ammo");
    pad(false); tick(); balance=55; m.write<std::int32_t>(disc->vendor+0x40,1);
    pad(true); tick();
    check(balance==5 && m.read<std::int32_t>(disc->ammo+8)==8 && last_sound==7,
          "partial PDA refill uses alternate price");
    pad(false); tick(); m.write<std::int32_t>(disc->vendor+0x5C,2);
    const int before=plays; pad(true); tick();
    check(plays==before && balance==5 && !visible,"manual purchase dialog blocks transactions");
    check(hud_clears==1 && hud.empty(),"manual dialog clears HUD");
    m.write<std::int32_t>(disc->vendor+0x5C,0);
    m.write<std::int32_t>(disc->mode,0); pad(false); tick(); pad(true); tick();
    check(plays==before && balance==5 && hud_clears==1,"outside vendor blocks transactions");
    std::cout << "RAC1 " << version << " " << controller_setting
              << " runtime purchase, audio, input label and compact HUD checks passed\n";
}

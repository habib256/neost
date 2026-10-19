// GEM : repérage ABI Line-A et mouvements absolus par le vrai pilote EmuTOS.
#include "core/Machine.hpp"
#include "util/DesktopPointer.hpp"
#include <memory>
#include <filesystem>
#include <cstdio>

int testDesktopMouse() {
    int checks=0,failures=0;
    auto eq=[&](const char* label,int64_t got,int64_t expected) {
        ++checks; if(got==expected) return;
        ++failures; std::fprintf(stderr,"[desktop-mouse] FAIL %s: %lld != %lld\n",label,(long long)got,(long long)expected);
    };
    neost::DesktopPointer p;
    p.x0=100; p.y0=50; p.x1=932; p.y1=602;
    p.bufferW=416; p.bufferH=276; p.left=48; p.top=38; p.width=320; p.height=200;
    int x=0,y=0;
    eq("image avec bordures",p.point(516,326,x,y),true); eq("centre X",x,160); eq("centre Y",y,100);
    eq("bordure sans clic",p.point(110,60,x,y),false); eq("letterbox sans clic",p.point(50,300,x,y),false);
    p.u0=48.0/416; p.u1=368.0/416; p.v0=38.0/276; p.v1=238.0/276;
    eq("zoom et UV",p.point(100,50,x,y),true); eq("zoom origine X",x,0); eq("zoom origine Y",y,0);
    p.barrel=0.2;
    eq("géométrie CRT",p.point(308,326,x,y),true); eq("courbure CRT X",x,77); eq("courbure CRT Y",y,100);
    p.barrel=0;
    p.hovered=true; p.button(false,true,516,326); p.button(false,false,950,700);
    eq("clic relâché hors image",p.clicks.size(),2); eq("relâche propriété",p.leftHeld,false);
    eq("relâche bord X",p.clicks.back().x,319); eq("relâche bord Y",p.clicks.back().y,199);
    p.reset(); p.hovered=false; p.button(false,true,516,326); eq("overlay bloque clic",p.clicks.empty(),true);
    p.button(true,false,516,326); eq("release étrangère ignorée",p.clicks.empty(),true);
    p.hovered=true;
    for(int i=0;i<2;++i) { p.button(false,true,516,326); p.button(false,false,516,326); }
    eq("double clic conservé",p.clicks.size(),4);
    eq("premier appui",p.clicks[0].left,true); eq("premier relâchement",p.clicks[1].left,false);
    {
        auto m=std::make_unique<Machine>(1024u*1024u,CpuCore::Moira,MachineType::St);
        auto& bus=m->bus;
        auto table=[&](uint32_t base) {
            const int width=m->shifter.activeWidth(), height=m->shifter.activeHeight();
            bus.write16(base-692,uint16_t(width-1)); bus.write16(base-690,uint16_t(height-1));
            bus.write16(base-12,uint16_t(width)); bus.write16(base-4,uint16_t(height));
            bus.write16(base,4); bus.write16(base+2,uint16_t(width/2)); bus.write16(base-2,uint16_t(width/2));
            bus.write16(base-602,10); bus.write16(base-600,20);
        };
        int width=0,height=0;
        eq("pas de tableau avant GEM",m->desktopMousePosition(x,y,width,height),false);
        table(0x3000); table(0x4000);
        eq("tableau ambigu refusé",m->desktopMousePosition(x,y,width,height),false);
        bus.write16(0x4000,0);
        eq("tableau unique accepté",m->desktopMousePosition(x,y,width,height),true);
        eq("position ABI X",x,10); eq("position ABI Y",y,20);
        bus.write16(0x3000,0);
        eq("cache périmé invalidé",m->desktopMousePosition(x,y,width,height),false);
    }
    if(!std::filesystem::exists("roms/etos192us.img")) {
        std::fprintf(stderr,"[desktop-mouse] missing bundled EmuTOS ROM\n"); return failures+1;
    }
    for(bool mono : {false,true}) {
        auto m=std::make_unique<Machine>(1024u*1024u,CpuCore::Moira,MachineType::St);
        m->bus.loadTos("roms/etos192us.img"); m->mfp.setColorMonitor(!mono); m->reset();
        for(int i=0;i<1200;++i) m->runFrame();
        int w=0,h=0;
        eq("EmuTOS initialise Line-A",m->desktopMousePosition(x,y,w,h),true);
        eq("résolution GEM X",w,mono?640:320); eq("résolution GEM Y",h,mono?400:200);
        for(const auto target : {std::pair<int,int>{15,12},{w-10,h-10},{w/2,h/2},{0,0}}) {
            bool arrived=false;
            for(int frame=0;frame<50;++frame) {
                arrived=m->desktopMouseMove(target.first,target.second,false,false);
                m->runFrame(); if(arrived) break;
            }
            eq("curseur atteint cible",arrived,true); eq("position GEM lisible",m->desktopMousePosition(x,y,w,h),true);
            eq("position cible X",x,target.first); eq("position cible Y",y,target.second);
        }
    }
    std::printf("[desktop-mouse] %d checks, %d FAIL\n",checks,failures);
    return failures;
}

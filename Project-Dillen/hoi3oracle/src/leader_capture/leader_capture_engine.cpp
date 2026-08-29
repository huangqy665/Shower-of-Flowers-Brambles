// HOI3 TFH 4.02 Leader Capture Auto-Transfer R06B-D328
// REVIEW candidate. x86 MSVC only.
//
// First-version product rule:
//   combat permanent destruction -> capture leader -> auto-transfer to resolved Captor Country.
//
// Attribution rule:
//   sole winning country -> Captor immediately;
//   coalition -> Captor = Province.Controller only if Controller is in winning-side country set;
//   otherwise UNKNOWN (leader remains CAPTURED-HOLD; no guessed transfer).
//
// Future-policy seams are intentionally explicit:
//   EvaluateCaptureEligibility / ResolveCaptor / EvaluateTransferPolicy.
// Current R06B implementations are deliberately minimal; R06B closes the observed COMBAT_A pre-outcome attribution race.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <intrin.h>
#include "engine_registry.h"
#include "leader_capture_engine.h"
#include "native_save_load_barrier.h"
#pragma comment(lib, "user32.lib")

static constexpr const auto& EngineSymbols = core::engine::Symbols;
using namespace core::engine::field;

static SIZE_T SharedPatchLength()
{
    return static_cast<SIZE_T>(
        static_cast<std::uintptr_t>(LeaderSharedRemovePatchSize)
    );
}

static const unsigned MAX_CONTEXT = 512;
static const unsigned MAX_HELD    = 256;
static const unsigned MAX_TRANSFER_WATCH = 256;
static const DWORD CONTEXT_TTL_MS = 20u * 60u * 1000u;
static const DWORD TRANSFER_WATCH_TTL_MS = 5u * 60u * 1000u;

using SharedRemove_t = void (__thiscall*)(void* self, void* unit, uint32_t arg2, uint32_t arg3);
using CountryLeaderAdd_t = void (__stdcall*)(void* country, void* leader, uint8_t notifyFlag);

struct PatchCallRec { uint8_t* site; uint8_t original[5]; bool installed; };
struct InlineHookState { BYTE original[16]; void* target; void* trampoline; bool installed; };
struct WinnerCountry { uint32_t tag; uint32_t id; };

enum CaptureRoute : uint32_t {
    ROUTE_NONE = 0,
    ROUTE_COMBAT_A_DIRECT = 1,
    ROUTE_COMBAT_B_WITHDRAW_DEFERRED = 2
};

enum CaptorReason : uint32_t {
    CAPTOR_UNKNOWN = 0,
    CAPTOR_SOLE_WINNER = 1,
    CAPTOR_CONTROLLER_INTERSECTION = 2
};

enum CaptureEligibility : uint32_t {
    CAPTURE_INELIGIBLE = 0,
    CAPTURE_ELIGIBLE = 1
};

enum TransferPolicyAction : uint32_t {
    TRANSFER_HOLD = 0,
    TRANSFER_AUTO_TO_CAPTOR = 1
};

struct PendingContext {
    LONG active;
    uint32_t key0, key1;
    uintptr_t province;
    uint32_t controller_initial;
    uint32_t controller_last;
    uint32_t winner_count;
    WinnerCountry winners[16];
    uint32_t resolved_captor;
    uint32_t resolve_reason;
    uint32_t remove_seen;
    uint32_t remove_route;
    uint32_t defer_withdraw_seen;
    DWORD created_ms;
    DWORD last_touch_ms;
};

struct HeldLeader {
    void* leader;
    void* sourceCountry;
    uint32_t sourceCountryId;
    char sourceTag[4];
    uint32_t unitKey0, unitKey1;
    uint32_t route;
    uint32_t serial;
    DWORD capturedTick;
};

struct TransferWatch {
    LONG active;
    uint32_t serial;
    void* leader;
    void* sourceCountry;
    void* targetCountry;
    uint32_t sourceCountryId;
    uint32_t targetCountryId;
    char sourceTag[4];
    char targetTag[4];
    DWORD startedTick;
    bool lastSourcePresent;
    bool lastTargetPresent;
    bool lastSourceReadable;
    bool lastTargetReadable;
    bool reversionSuspectLogged;
};

static uint8_t* g_exe = 0;
static DWORD g_exeSize = 0;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logLock;
static CRITICAL_SECTION g_ctxLock;
static CRITICAL_SECTION g_stateLock;
static volatile LONG g_initialized = 0;
static volatile LONG g_primitivesInitialized = 0;
static volatile LONG g_hooksInstalled = 0;
static volatile LONG g_gameplayActive = 0;
static core::NativeSaveLoadBarrier* g_saveLoadBarrier = nullptr;
static volatile LONG g_validationFailureLogged = 0;
static uint64_t g_nextTickMilliseconds = 0;

static std::optional<core::NativeSaveLoadWriteLease>
AcquireNativeStateLease(){return g_saveLoadBarrier?g_saveLoadBarrier->TryAcquireWriteLease():std::nullopt;}

static PatchCallRec g_callPatches[8] = {};
static int g_callPatchCount = 0;
static InlineHookState g_sharedHook = {};
static SharedRemove_t g_origSharedRemove = 0;
static void* g_originalBattleOutcome = 0;
static void* g_originalDeferEnqueue = 0;
static void* g_sharedRemoveEntry = 0;
static CountryLeaderAdd_t g_countryLeaderAdd = 0;
static void* g_countryLeaderRemoveAddr = 0;

static PendingContext g_ctx[MAX_CONTEXT] = {};
static uint32_t g_replaceCursor = 0;
static HeldLeader g_held[MAX_HELD] = {};
static unsigned g_heldCount = 0;
static uint32_t g_nextSerial = 1;
static TransferWatch g_transferWatch[MAX_TRANSFER_WATCH] = {};
static unsigned g_transferWatchCount = 0;
static volatile LONG g_reversionSuspects = 0;

static volatile LONG g_totalCaptured = 0;
static volatile LONG g_totalTransferred = 0;
static volatile LONG g_totalReleased = 0;
static volatile LONG g_combatAHits = 0;
static volatile LONG g_combatBHits = 0;
static volatile LONG g_noContextRefusals = 0;
static volatile LONG g_preconditionRefusals = 0;
static volatile LONG g_transferFailures = 0;
static volatile LONG g_manualHits = 0;
static volatile LONG g_combatAPreseedCreated = 0;
static volatile LONG g_combatAPreseedRefused = 0;

// ---------------- basic safe reads / logging ----------------
static uintptr_t to_rva(void* p) {
    uintptr_t v=(uintptr_t)p;
    return (v>=(uintptr_t)g_exe && v<(uintptr_t)g_exe+g_exeSize) ? v-(uintptr_t)g_exe : 0xFFFFFFFFu;
}
static bool ReadU32(uintptr_t p, uint32_t* out) {
    if(!p||!out)return false;
    __try { *out=*(volatile uint32_t*)p; return true; }
    __except(EXCEPTION_EXECUTE_HANDLER){*out=0;return false;}
}
static bool ReadPtr(uintptr_t p, uintptr_t* out) {
    uint32_t v=0; if(!ReadU32(p,&v))return false; *out=(uintptr_t)v; return true;
}
static bool safe_read_tag3(void* country, char out[4]) {
    if(!out)return false; out[0]=out[1]=out[2]='?';out[3]=0;if(!country)return false;
    __try { const char* p=(const char*)((uint8_t*)country+CountryTagOffset);out[0]=p[0];out[1]=p[1];out[2]=p[2];return true; }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void FormatTag(uint32_t tag, char out[4]) {
    out[0]=(char)(tag&0xff);out[1]=(char)((tag>>8)&0xff);out[2]=(char)((tag>>16)&0xff);out[3]=0;
    for(int i=0;i<3;i++)if(out[i]<32||out[i]>126)out[i]='?';
}
static void Log(const char* fmt, ...) {
    if(g_log==INVALID_HANDLE_VALUE)return;
    char buf[2300];SYSTEMTIME st;GetLocalTime(&st);
    int n=_snprintf_s(buf,sizeof(buf),_TRUNCATE,"%02u:%02u:%02u.%03u ",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds);
    if(n<0)n=0;va_list ap;va_start(ap,fmt);_vsnprintf_s(buf+n,sizeof(buf)-n,_TRUNCATE,fmt,ap);va_end(ap);
    size_t len=strlen(buf);if(len+2<sizeof(buf)){buf[len++]='\r';buf[len++]='\n';buf[len]=0;}
    EnterCriticalSection(&g_logLock);DWORD w=0;WriteFile(g_log,buf,(DWORD)len,&w,0);FlushFileBuffers(g_log);LeaveCriticalSection(&g_logLock);
}
static bool OpenLog() {
    char temp[MAX_PATH]={0}, path[MAX_PATH]={0};if(!GetTempPathA(MAX_PATH,temp))return false;
    _snprintf_s(path,sizeof(path),_TRUNCATE,"%sHOI3_LEADER_CAPTURE_AUTO_TRANSFER_R06B.log",temp);
    g_log=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    return g_log!=INVALID_HANDLE_VALUE;
}

// ---------------- native country/registry primitives ----------------
static bool resolve_country_from_id(uint32_t id, void** out) {
    if(out)*out=0;if(!out||!g_exe||id>4095)return false;
    uintptr_t state=0,table=0,country=0;
    if(!ReadPtr((uintptr_t)g_exe+EngineSymbols.countryDatabaseSingleton,&state)||!state)return false;
    if(!ReadPtr(state+CountryPointerTableOffset,&table)||!table)return false;
    if(!ReadPtr(table+(uintptr_t)id*4,&country)||!country)return false;
    *out=(void*)country;return true;
}
static bool resolve_country_from_unit(void* unit,uint32_t* idOut,void** countryOut) {
    if(idOut)*idOut=0;if(countryOut)*countryOut=0;if(!unit||!countryOut)return false;
    uint32_t id=0;if(!ReadU32((uintptr_t)unit+UnitCountryIdOffset,&id))return false;
    void* c=0;if(!resolve_country_from_id(id,&c))return false;if(idOut)*idOut=id;*countryOut=c;return true;
}
static bool registry_contains(void* country,void* leader,bool* readable) {
    if(readable)*readable=false;if(!country||!leader)return false;
    uintptr_t node=0;if(!ReadPtr((uintptr_t)country+CountryLeaderRegistryOffset,&node))return false;
    for(unsigned i=0;i<8192&&node;i++){
        uintptr_t item=0,next=0;if(!ReadPtr(node,&item)||!ReadPtr(node+8,&next))return false;
        if(item==(uintptr_t)leader){if(readable)*readable=true;return true;}
        if(next==node)return false;node=next;
    }
    if(readable)*readable=true;return false;
}
static void call_country_remove(void* country,void* leader) {
    void* fn=g_countryLeaderRemoveAddr;
    __asm {
        mov eax, country
        mov edx, leader
        call fn
    }
}

// ---------------- attribution context ----------------
static bool WinnerContains(const PendingContext& c,uint32_t id,uint32_t* tagOut) {
    for(uint32_t i=0;i<c.winner_count;i++)if(c.winners[i].id==id){if(tagOut)*tagOut=c.winners[i].tag;return true;}return false;
}
static PendingContext* FindContextUnlocked(uint32_t k0,uint32_t k1) {
    for(unsigned i=0;i<MAX_CONTEXT;i++)if(g_ctx[i].active&&g_ctx[i].key0==k0&&g_ctx[i].key1==k1)return &g_ctx[i];return 0;
}
static PendingContext* AllocContextUnlocked(uint32_t k0,uint32_t k1) {
    PendingContext* e=FindContextUnlocked(k0,k1);if(e)return e;
    for(unsigned i=0;i<MAX_CONTEXT;i++)if(!g_ctx[i].active){ZeroMemory(&g_ctx[i],sizeof(g_ctx[i]));g_ctx[i].active=1;g_ctx[i].key0=k0;g_ctx[i].key1=k1;return &g_ctx[i];}
    PendingContext* c=&g_ctx[g_replaceCursor++%MAX_CONTEXT];ZeroMemory(c,sizeof(*c));c->active=1;c->key0=k0;c->key1=k1;return c;
}
static void CopyWinners(uintptr_t winnerSide,WinnerCountry* out,uint32_t* count) {
    *count=0;uintptr_t node=0;if(!ReadPtr(winnerSide+CombatSideWinnerCountryListOffset,&node))return;
    for(int guard=0;node&&guard<64&&*count<16;guard++){
        uint32_t tag=0,id=0;uintptr_t next=0;if(!ReadU32(node+WinnerCountryNodeTagOffset,&tag)||!ReadU32(node+WinnerCountryNodeCountryIdOffset,&id)||!ReadPtr(node+WinnerCountryNodeNextOffset,&next))break;
        out[*count].tag=tag;out[*count].id=id;(*count)++;node=next;
    }
}
static void WinnerListText(const WinnerCountry* w,uint32_t n,char* out,size_t cap) {
    out[0]=0;for(uint32_t i=0;i<n;i++){char t[4];FormatTag(w[i].tag,t);char tmp[64];_snprintf_s(tmp,sizeof(tmp),_TRUNCATE,"%s%s(%u)",i?",":"",t,w[i].id);strncat_s(out,cap,tmp,_TRUNCATE);}
}

static bool WinnerArrayContains(const WinnerCountry* w,uint32_t n,uint32_t id) {
    for(uint32_t i=0;i<n;i++)if(w[i].id==id)return true;
    return false;
}

// Policy seam #1: later probability / leader skill / trait / war-type rules plug in here.
static CaptureEligibility EvaluateCaptureEligibility(CaptureRoute route,const PendingContext* ctxSnapshot,void* unit,void* leader) {
    if(route!=ROUTE_COMBAT_A_DIRECT&&route!=ROUTE_COMBAT_B_WITHDRAW_DEFERRED)return CAPTURE_INELIGIBLE;
    if(!ctxSnapshot||!ctxSnapshot->active||!unit||!leader||ctxSnapshot->winner_count==0)return CAPTURE_INELIGIBLE;
    if(route==ROUTE_COMBAT_B_WITHDRAW_DEFERRED && !ctxSnapshot->defer_withdraw_seen)return CAPTURE_INELIGIBLE;
    return CAPTURE_ELIGIBLE; // R06B: every proven combat permanent destruction captures.
}

// Policy seam #2: attribution resolver. R06B uses only proven native facts.
static uint32_t ResolveCaptorUnlocked(PendingContext* c,uint32_t controller,uint32_t* reasonOut,uint32_t* tagOut) {
    if(reasonOut)*reasonOut=CAPTOR_UNKNOWN;if(tagOut)*tagOut=0;if(!c||!c->active)return 0;
    if(c->winner_count==1){if(reasonOut)*reasonOut=CAPTOR_SOLE_WINNER;if(tagOut)*tagOut=c->winners[0].tag;return c->winners[0].id;}
    uint32_t tag=0;if(c->winner_count>1&&WinnerContains(*c,controller,&tag)){if(reasonOut)*reasonOut=CAPTOR_CONTROLLER_INTERSECTION;if(tagOut)*tagOut=tag;return controller;}
    return 0;
}
static bool RefreshCaptorUnlocked(PendingContext* c,uint32_t controller,const char* trigger,uint32_t* newlyResolved) {
    if(newlyResolved)*newlyResolved=0;uint32_t reason=0,tag=0;uint32_t id=ResolveCaptorUnlocked(c,controller,&reason,&tag);
    if(id&&(c->resolved_captor!=id||c->resolve_reason!=reason)){
        c->resolved_captor=id;c->resolve_reason=reason;if(newlyResolved)*newlyResolved=id;char t[4];FormatTag(tag,t);
        Log("[CAPTOR_RESOLVE] trigger=%s unit=%08X:%08X controller_id=%u captor_id=%u captor_tag=%s reason=%s",trigger,c->key0,c->key1,controller,id,t,reason==CAPTOR_SOLE_WINNER?"SOLE_WINNER":"CONTROLLER_INTERSECTION");
        return true;
    }
    return false;
}

// Policy seam #3: later POW/recruit chance/delay/diplomacy rules plug in here.
static TransferPolicyAction EvaluateTransferPolicy(const HeldLeader& held,uint32_t captorId) {
    if(!held.leader||!captorId||captorId==held.sourceCountryId)return TRANSFER_HOLD;
    return TRANSFER_AUTO_TO_CAPTOR; // R06B: immediate automatic transfer whenever attribution is reliable.
}

// ---------------- held leader state / transfer ----------------
static bool HeldContainsLeaderUnlocked(void* leader) { for(unsigned i=0;i<g_heldCount;i++)if(g_held[i].leader==leader)return true;return false; }
static bool HeldPush(void* leader,void* source,uint32_t sourceId,const char tag[4],uint32_t k0,uint32_t k1,CaptureRoute route,HeldLeader* out) {
    bool ok=false;EnterCriticalSection(&g_stateLock);
    if(g_heldCount<MAX_HELD&&!HeldContainsLeaderUnlocked(leader)){
        HeldLeader h={};h.leader=leader;h.sourceCountry=source;h.sourceCountryId=sourceId;h.sourceTag[0]=tag[0];h.sourceTag[1]=tag[1];h.sourceTag[2]=tag[2];h.sourceTag[3]=0;h.unitKey0=k0;h.unitKey1=k1;h.route=route;h.serial=g_nextSerial++;h.capturedTick=GetTickCount();g_held[g_heldCount++]=h;if(out)*out=h;ok=true;
    }
    LeaveCriticalSection(&g_stateLock);return ok;
}
static bool HeldFindByUnit(uint32_t k0,uint32_t k1,HeldLeader* out) {
    bool ok=false;EnterCriticalSection(&g_stateLock);for(unsigned i=0;i<g_heldCount;i++)if(g_held[i].unitKey0==k0&&g_held[i].unitKey1==k1){if(out)*out=g_held[i];ok=true;break;}LeaveCriticalSection(&g_stateLock);return ok;
}
static bool HeldRemoveSerial(uint32_t serial) {
    bool ok=false;EnterCriticalSection(&g_stateLock);for(unsigned i=0;i<g_heldCount;i++)if(g_held[i].serial==serial){for(unsigned j=i+1;j<g_heldCount;j++)g_held[j-1]=g_held[j];g_heldCount--;ZeroMemory(&g_held[g_heldCount],sizeof(g_held[g_heldCount]));ok=true;break;}LeaveCriticalSection(&g_stateLock);return ok;
}
static unsigned HeldCount(){EnterCriticalSection(&g_stateLock);unsigned n=g_heldCount;LeaveCriticalSection(&g_stateLock);return n;}

static void AddTransferWatch(const HeldLeader& h, void* target, uint32_t targetId, const char targetTag[4], bool sourcePresent, bool targetPresent, bool sourceReadable, bool targetReadable) {
    EnterCriticalSection(&g_stateLock);
    unsigned slot=MAX_TRANSFER_WATCH;
    for(unsigned i=0;i<MAX_TRANSFER_WATCH;i++) {
        if(!g_transferWatch[i].active){slot=i;break;}
    }
    if(slot==MAX_TRANSFER_WATCH) {
        LeaveCriticalSection(&g_stateLock);
        Log("[TRANSFER_WATCH] serial=%u leader=%p result=REFUSED reason=WATCH_CAPACITY",h.serial,h.leader);
        return;
    }
    TransferWatch& w=g_transferWatch[slot];
    ZeroMemory(&w,sizeof(w));
    w.active=1;
    w.serial=h.serial;
    w.leader=h.leader;
    w.sourceCountry=h.sourceCountry;
    w.targetCountry=target;
    w.sourceCountryId=h.sourceCountryId;
    w.targetCountryId=targetId;
    memcpy(w.sourceTag,h.sourceTag,4);
    if(targetTag)memcpy(w.targetTag,targetTag,4);
    w.startedTick=GetTickCount();
    w.lastSourcePresent=sourcePresent;
    w.lastTargetPresent=targetPresent;
    w.lastSourceReadable=sourceReadable;
    w.lastTargetReadable=targetReadable;
    w.reversionSuspectLogged=false;
    g_transferWatchCount++;
    LeaveCriticalSection(&g_stateLock);
    Log("[TRANSFER_WATCH_START] serial=%u leader=%p source=%s(%u) target=%s(%u) source_present=%s target_present=%s ttl_ms=%lu",
        h.serial,h.leader,h.sourceTag,h.sourceCountryId,targetTag?targetTag:"???",targetId,
        sourcePresent?"YES":"NO",targetPresent?"YES":"NO",(unsigned long)TRANSFER_WATCH_TTL_MS);
}

static void PollTransferWatches() {
    DWORD now=GetTickCount();
    EnterCriticalSection(&g_stateLock);
    for(unsigned i=0;i<MAX_TRANSFER_WATCH;i++) {
        TransferWatch& w=g_transferWatch[i];
        if(!w.active)continue;
        bool rs=false,rt=false;
        bool inS=registry_contains(w.sourceCountry,w.leader,&rs);
        bool inT=registry_contains(w.targetCountry,w.leader,&rt);
        DWORD age=now-w.startedTick;
        bool changed=(rs!=w.lastSourceReadable)||(rt!=w.lastTargetReadable)||(inS!=w.lastSourcePresent)||(inT!=w.lastTargetPresent);
        if(changed) {
            Log("[TRANSFER_WATCH_CHANGE] serial=%u leader=%p age_ms=%lu source=%s(%u) source_present=%s(%s) target=%s(%u) target_present=%s(%s)",
                w.serial,w.leader,(unsigned long)age,w.sourceTag,w.sourceCountryId,
                inS?"YES":"NO",rs?"READ":"N/A",w.targetTag,w.targetCountryId,
                inT?"YES":"NO",rt?"READ":"N/A");
            w.lastSourceReadable=rs;w.lastTargetReadable=rt;w.lastSourcePresent=inS;w.lastTargetPresent=inT;
        }
        if(!w.reversionSuspectLogged && rs && rt && (inS || !inT)) {
            w.reversionSuspectLogged=true;
            InterlockedIncrement(&g_reversionSuspects);
            Log("[TRANSFER_REVERSION_SUSPECT] serial=%u leader=%p age_ms=%lu source_present=%s target_present=%s",
                w.serial,w.leader,(unsigned long)age,inS?"YES":"NO",inT?"YES":"NO");
        }
        if(age>=TRANSFER_WATCH_TTL_MS) {
            Log("[TRANSFER_WATCH_FINAL] serial=%u leader=%p age_ms=%lu source_present=%s(%s) target_present=%s(%s) reversion_suspect=%s",
                w.serial,w.leader,(unsigned long)age,inS?"YES":"NO",rs?"READ":"N/A",
                inT?"YES":"NO",rt?"READ":"N/A",w.reversionSuspectLogged?"YES":"NO");
            w.active=0;
            if(g_transferWatchCount)g_transferWatchCount--;
        }
    }
    LeaveCriticalSection(&g_stateLock);
}

static bool TryAutoTransferUnit(uint32_t k0,uint32_t k1,uint32_t captorId,const char* trigger) {
    HeldLeader h={};if(!HeldFindByUnit(k0,k1,&h))return false;
    if(EvaluateTransferPolicy(h,captorId)!=TRANSFER_AUTO_TO_CAPTOR){Log("[TRANSFER_HOLD] trigger=%s serial=%u unit=%08X:%08X captor_id=%u reason=POLICY_OR_UNKNOWN",trigger,h.serial,k0,k1,captorId);return false;}
    void* target=0;if(!resolve_country_from_id(captorId,&target)||!target){InterlockedIncrement(&g_transferFailures);Log("[AUTO_TRANSFER] trigger=%s serial=%u unit=%08X:%08X captor_id=%u result=REFUSED reason=CAPTOR_RESOLVE_FAILED",trigger,h.serial,k0,k1,captorId);return false;}
    char targetTag[4];bool tagOk=safe_read_tag3(target,targetTag);uintptr_t back=0;bool rb=ReadPtr((uintptr_t)h.leader+LeaderUnitReverseOffset,&back);
    bool rs=false,rt=false;bool inSource=registry_contains(h.sourceCountry,h.leader,&rs);bool inTarget=registry_contains(target,h.leader,&rt);
    bool pre=rb&&back==0&&rs&&rt&&!inSource&&!inTarget&&target!=h.sourceCountry;
    Log("[AUTO_TRANSFER_PRE] trigger=%s serial=%u unit=%08X:%08X leader=%p source=%s(%u) captor=%s(%u) backlink=%p source_present=%s target_present=%s prerequisites=%s",trigger,h.serial,k0,k1,h.leader,h.sourceTag,h.sourceCountryId,tagOk?targetTag:"???",captorId,(void*)back,inSource?"YES":"NO",inTarget?"YES":"NO",pre?"PASS":"FAIL");
    if(!pre){InterlockedIncrement(&g_transferFailures);return false;}
    g_countryLeaderAdd(target,h.leader,0);
    bool rs2=false,rt2=false;bool inSource2=registry_contains(h.sourceCountry,h.leader,&rs2);bool inTarget2=registry_contains(target,h.leader,&rt2);bool pass=rs2&&rt2&&!inSource2&&inTarget2;
    if(pass){
        AddTransferWatch(h,target,captorId,targetTag,inSource2,inTarget2,rs2,rt2);
        HeldRemoveSerial(h.serial);
        InterlockedIncrement(&g_totalTransferred);
    }else InterlockedIncrement(&g_transferFailures);
    Log("[AUTO_TRANSFER_POST] trigger=%s serial=%u unit=%08X:%08X leader=%p captor=%s(%u) source_present=%s target_present=%s held_count=%u result=%s",trigger,h.serial,k0,k1,h.leader,tagOk?targetTag:"???",captorId,inSource2?"YES":"NO",inTarget2?"YES":"NO",HeldCount(),pass?"PASS":"FAIL");
    return pass;
}

static bool ReleaseOldestForSafety() {
    HeldLeader h={};EnterCriticalSection(&g_stateLock);bool have=g_heldCount>0;if(have)h=g_held[0];LeaveCriticalSection(&g_stateLock);if(!have){Log("[CONTROL] F6 RELEASE_REFUSED reason=NO_HELD");return false;}
    uintptr_t back=0;bool rb=ReadPtr((uintptr_t)h.leader+LeaderUnitReverseOffset,&back);bool rs=false;bool inSource=registry_contains(h.sourceCountry,h.leader,&rs);bool pre=rb&&back==0&&rs&&!inSource;
    if(!pre){Log("[CONTROL] F6 RELEASE_REFUSED serial=%u prerequisites=FAIL",h.serial);return false;}
    g_countryLeaderAdd(h.sourceCountry,h.leader,0);bool rs2=false;bool inSource2=registry_contains(h.sourceCountry,h.leader,&rs2);bool pass=rs2&&inSource2;if(pass){HeldRemoveSerial(h.serial);InterlockedIncrement(&g_totalReleased);}Log("[CONTROL] F6 RELEASE serial=%u source=%s(%u) result=%s held_count=%u",h.serial,h.sourceTag,h.sourceCountryId,pass?"PASS":"FAIL",HeldCount());return pass;
}

// ---------------- battle/defer call wrappers ----------------
static void RefuseCombatAPreseed(uint32_t k0,uint32_t k1,uintptr_t unit,uintptr_t currentSide,const char* reason) {
    InterlockedIncrement(&g_combatAPreseedRefused);
    Log("[COMBAT_A_CONTEXT_PRESEED] unit=%08X:%08X unit_ptr=%08X current_side=%08X result=REFUSED reason=%s",
        k0,k1,(uint32_t)unit,(uint32_t)currentSide,reason?reason:"UNKNOWN");
}

extern "C" void __cdecl OnCombatADirectPre(uintptr_t unit,uintptr_t currentSide) {
    if(!leader_capture::IsGameplayActive()||!unit||!currentSide||!g_exe)return;
    auto stateLease=AcquireNativeStateLease();if(!stateLease)return;
    uint32_t k0=0,k1=0;
    if(!ReadU32(unit+UnitId0Offset,&k0)||!ReadU32(unit+UnitId1Offset,&k1))return;

    // If BattleOutcome already seeded this victim, preserve that higher-context snapshot.
    EnterCriticalSection(&g_ctxLock);
    PendingContext* existing=FindContextUnlocked(k0,k1);
    bool alreadyTracked=existing!=0;
    LeaveCriticalSection(&g_ctxLock);
    if(alreadyTracked)return;

    uintptr_t combat=0,sideA=0,sideB=0,winning=0,province=0;
    if(!ReadPtr(currentSide+CombatantCombatOffset,&combat)||!combat){RefuseCombatAPreseed(k0,k1,unit,currentSide,"NO_COMBAT");return;}
    if(!ReadPtr(combat+CombatSideAOffset,&sideA)||!ReadPtr(combat+CombatSideBOffset,&sideB)){RefuseCombatAPreseed(k0,k1,unit,currentSide,"NO_SIDES");return;}
    if(currentSide==sideA)winning=sideB;
    else if(currentSide==sideB)winning=sideA;
    else {RefuseCombatAPreseed(k0,k1,unit,currentSide,"CURRENT_SIDE_NOT_IN_COMBAT");return;}
    if(!winning){RefuseCombatAPreseed(k0,k1,unit,currentSide,"NO_OPPONENT_SIDE");return;}
    ReadPtr(combat+CombatProvinceOffset,&province);

    WinnerCountry winners[16]={};uint32_t wc=0;
    CopyWinners(winning,winners,&wc);
    if(wc==0){RefuseCombatAPreseed(k0,k1,unit,currentSide,"EMPTY_OPPONENT_COUNTRY_SET");return;}

    uint32_t sourceId=0;void* sourceCountry=0;
    if(!resolve_country_from_unit((void*)unit,&sourceId,&sourceCountry)||!sourceCountry){RefuseCombatAPreseed(k0,k1,unit,currentSide,"NO_SOURCE_COUNTRY");return;}
    // Safety invariant: the reconstructed opposite side must not contain the victim's own country.
    if(WinnerArrayContains(winners,wc,sourceId)){RefuseCombatAPreseed(k0,k1,unit,currentSide,"OPPONENT_SET_CONTAINS_SOURCE");return;}

    uint32_t controller=0;if(province)ReadU32(province+ProvinceControllerCountryIndexOffset,&controller);
    bool committed=false;
    EnterCriticalSection(&g_ctxLock);
    PendingContext* c=FindContextUnlocked(k0,k1);
    if(!c)c=AllocContextUnlocked(k0,k1);
    if(c){
        c->province=province;c->controller_initial=controller;c->controller_last=controller;
        c->winner_count=wc;CopyMemory(c->winners,winners,sizeof(WinnerCountry)*wc);
        c->created_ms=GetTickCount();c->last_touch_ms=c->created_ms;c->remove_seen=0;c->remove_route=0;
        c->defer_withdraw_seen=0;c->resolved_captor=0;c->resolve_reason=0;
        uint32_t newly=0;RefreshCaptorUnlocked(c,controller,"COMBAT_A_PRESEED",&newly);
        committed=true;
    }
    LeaveCriticalSection(&g_ctxLock);
    if(!committed){RefuseCombatAPreseed(k0,k1,unit,currentSide,"CONTEXT_ALLOC_FAILED");return;}

    char wl[512];WinnerListText(winners,wc,wl,sizeof(wl));
    InterlockedIncrement(&g_combatAPreseedCreated);
    Log("[COMBAT_A_CONTEXT_PRESEED] unit=%08X:%08X unit_ptr=%08X current_side=%08X combat=%08X province=%08X winner_count=%u winners=%s controller=%u result=PASS",
        k0,k1,(uint32_t)unit,(uint32_t)currentSide,(uint32_t)combat,(uint32_t)province,wc,wl,controller);
}

extern "C" void __cdecl OnBattleOutcomePre(uintptr_t retAddr,uintptr_t battle,uint32_t selector) {
    if(!leader_capture::IsGameplayActive()||!battle||!g_exe)return;uint32_t retRva=(uint32_t)(retAddr-(uintptr_t)g_exe);uintptr_t losing=0,winning=0,province=0;
    auto stateLease=AcquireNativeStateLease();if(!stateLease)return;
    if(retRva==EngineSymbols.leaderOutcomeReturnA){if(!ReadPtr(battle+CombatSideAOffset,&losing)||!ReadPtr(battle+CombatSideBOffset,&winning))return;}
    else if(retRva==EngineSymbols.leaderOutcomeReturnB){if(!ReadPtr(battle+CombatSideBOffset,&losing)||!ReadPtr(battle+CombatSideAOffset,&winning))return;}else return;
    ReadPtr(battle+CombatProvinceOffset,&province);WinnerCountry winners[16]={};uint32_t wc=0;CopyWinners(winning,winners,&wc);uint32_t controller=0;if(province)ReadU32(province+ProvinceControllerCountryIndexOffset,&controller);char wl[512];WinnerListText(winners,wc,wl,sizeof(wl));
    Log("[BATTLE_CONTEXT] ret_rva=%08X battle=%08X province=%08X winner_count=%u winners=%s controller_at_outcome=%u",retRva,(uint32_t)battle,(uint32_t)province,wc,wl,controller);
    uintptr_t node=0;if(!ReadPtr(losing+CombatSideUnitListOffset,&node))return;
    for(int guard=0;node&&guard<512;guard++){
        uintptr_t unit=0,next=0;if(!ReadPtr(node,&unit)||!ReadPtr(node+8,&next))break;uint32_t k0=0,k1=0;
        if(unit&&ReadU32(unit+UnitId0Offset,&k0)&&ReadU32(unit+UnitId1Offset,&k1)){
            EnterCriticalSection(&g_ctxLock);PendingContext* c=AllocContextUnlocked(k0,k1);c->province=province;c->controller_initial=controller;c->controller_last=controller;c->winner_count=wc;if(wc)CopyMemory(c->winners,winners,sizeof(WinnerCountry)*wc);c->created_ms=GetTickCount();c->last_touch_ms=c->created_ms;c->remove_seen=0;c->remove_route=0;c->defer_withdraw_seen=0;c->resolved_captor=0;c->resolve_reason=0;uint32_t newly=0;RefreshCaptorUnlocked(c,controller,"BATTLE_OUTCOME",&newly);LeaveCriticalSection(&g_ctxLock);
            Log("[TRACK_VICTIM] unit=%08X:%08X unit_ptr=%08X province=%08X winner_count=%u",k0,k1,(uint32_t)unit,(uint32_t)province,wc);
        }node=next;
    }
}
extern "C" void __cdecl OnDeferEnqueuePre(uintptr_t object,uintptr_t retAddr) {
    if(!leader_capture::IsGameplayActive()||!object||!g_exe)return;uint32_t k0=0,k1=0;if(!ReadU32(object+UnitId0Offset,&k0)||!ReadU32(object+UnitId1Offset,&k1))return;
    auto stateLease=AcquireNativeStateLease();if(!stateLease)return;
    uint32_t retRva=(uint32_t)(retAddr-(uintptr_t)g_exe);
    EnterCriticalSection(&g_ctxLock);PendingContext* c=FindContextUnlocked(k0,k1);bool tracked=c!=0;if(c){c->last_touch_ms=GetTickCount();if(retRva==EngineSymbols.leaderWithdrawDeferredReturn)c->defer_withdraw_seen=1;}LeaveCriticalSection(&g_ctxLock);if(!tracked)return;
    Log("[DEFER_ENQUEUE] unit=%08X:%08X object_ptr=%08X return_rva=%08X exact_withdraw_route=%s",k0,k1,(uint32_t)object,retRva,retRva==EngineSymbols.leaderWithdrawDeferredReturn?"YES":"NO");
}
extern "C" __declspec(naked) void HookBattleOutcome() {
    __asm {
        pushfd
        pushad
        lea edx, [esp+36]
        push dword ptr [edx+8]
        push dword ptr [edx+4]
        push dword ptr [edx]
        call OnBattleOutcomePre
        add esp, 12
        popad
        popfd
        jmp dword ptr [g_originalBattleOutcome]
    }
}
extern "C" __declspec(naked) void HookDeferEnqueue() {
    __asm {
        pushfd
        pushad
        lea edx, [esp+36]
        push dword ptr [edx]
        push dword ptr [esp+32]
        call OnDeferEnqueuePre
        add esp, 8
        popad
        popfd
        jmp dword ptr [g_originalDeferEnqueue]
    }
}

// Replacement for the registered combat-A direct shared-remove call site.
// It snapshots attribution while the caller's EBP/EDI combat frame is still alive,
// then tail-jumps into the registered shared-remove function.
extern "C" __declspec(naked) void HookCombatADirectCall() {
    __asm {
        pushfd
        pushad
        mov eax, dword ptr [ebp+8]
        push eax
        push edi
        call OnCombatADirectPre
        add esp, 8
        popad
        popfd
        jmp dword ptr [g_sharedRemoveEntry]
    }
}

// ---------------- shared remove capture hook ----------------
static void __fastcall hkSharedRemove(void* self,void*,void* unit,uint32_t arg2,uint32_t arg3) {
    if(!leader_capture::IsGameplayActive()){g_origSharedRemove(self,unit,arg2,arg3);return;}
    auto writeLease=AcquireNativeStateLease();if(!writeLease){g_origSharedRemove(self,unit,arg2,arg3);return;}
    void* caller=_ReturnAddress();uintptr_t rva=to_rva(caller);
    if(rva==EngineSymbols.leaderManualReturn){InterlockedIncrement(&g_manualHits);g_origSharedRemove(self,unit,arg2,arg3);return;}
    CaptureRoute route=ROUTE_NONE;if(rva==EngineSymbols.leaderCombatAReturn)route=ROUTE_COMBAT_A_DIRECT;else if(rva==EngineSymbols.leaderCombatBReturn)route=ROUTE_COMBAT_B_WITHDRAW_DEFERRED;
    if(route==ROUTE_NONE){g_origSharedRemove(self,unit,arg2,arg3);return;}
    if(route==ROUTE_COMBAT_A_DIRECT)InterlockedIncrement(&g_combatAHits);else InterlockedIncrement(&g_combatBHits);

    uint32_t k0=0,k1=0;if(!unit||!ReadU32((uintptr_t)unit+UnitId0Offset,&k0)||!ReadU32((uintptr_t)unit+UnitId1Offset,&k1)){g_origSharedRemove(self,unit,arg2,arg3);return;}
    uint32_t captorId=0,winnerCount=0,controller=0;bool ctxMatch=false;PendingContext ctxSnapshot={};
    EnterCriticalSection(&g_ctxLock);PendingContext* ctx=FindContextUnlocked(k0,k1);if(ctx){ctxMatch=true;ctx->remove_seen=1;ctx->remove_route=route;ctx->last_touch_ms=GetTickCount();captorId=ctx->resolved_captor;winnerCount=ctx->winner_count;controller=ctx->controller_last;ctxSnapshot=*ctx;}LeaveCriticalSection(&g_ctxLock);
    if(!ctxMatch){InterlockedIncrement(&g_noContextRefusals);Log("[CAPTURE_REFUSED] route=%s unit=%08X:%08X reason=NO_ATTRIBUTION_CONTEXT",route==ROUTE_COMBAT_A_DIRECT?"COMBAT_A":"COMBAT_B",k0,k1);g_origSharedRemove(self,unit,arg2,arg3);return;}

    uintptr_t leader=0,rev=0;uint32_t sourceId=0;void* source=0;char sourceTag[4]={'?','?','?',0};
    bool rl=ReadPtr((uintptr_t)unit+UnitLeaderOffset,&leader);bool rr=leader&&ReadPtr(leader+LeaderUnitReverseOffset,&rev);bool rc=resolve_country_from_unit(unit,&sourceId,&source);bool rt=rc&&safe_read_tag3(source,sourceTag);bool argsOk=(arg2==0&&arg3==1);
    CaptureEligibility eligible=EvaluateCaptureEligibility(route,&ctxSnapshot,unit,(void*)leader);
    bool duplicate=false;EnterCriticalSection(&g_stateLock);duplicate=leader&&HeldContainsLeaderUnlocked((void*)leader);bool capacity=g_heldCount<MAX_HELD;LeaveCriticalSection(&g_stateLock);
    bool pre=eligible==CAPTURE_ELIGIBLE&&rl&&leader&&rr&&rev==(uintptr_t)unit&&rc&&source&&rt&&argsOk&&!duplicate&&capacity;
    Log("[CAPTURE_PRE] route=%s unit=%08X:%08X leader=%p source=%s(%u) winner_count=%u controller=%u captor_id=%u defer_exact=%s reverse_match=%s args=%u,%u context=YES eligibility=%s preconditions=%s",route==ROUTE_COMBAT_A_DIRECT?"COMBAT_A":"COMBAT_B",k0,k1,(void*)leader,sourceTag,sourceId,winnerCount,controller,captorId,ctxSnapshot.defer_withdraw_seen?"YES":"NO",(rr&&rev==(uintptr_t)unit)?"PASS":"FAIL",arg2,arg3,eligible==CAPTURE_ELIGIBLE?"YES":"NO",pre?"PASS":"FAIL");

    g_origSharedRemove(self,unit,arg2,arg3);
    if(!pre){InterlockedIncrement(&g_preconditionRefusals);Log("[CAPTURE_POST] unit=%08X:%08X result=REFUSED reason=PRECONDITIONS",k0,k1);return;}
    uintptr_t back=0;bool rb=ReadPtr(leader+LeaderUnitReverseOffset,&back);bool readable=false;bool inSource=registry_contains(source,(void*)leader,&readable);
    if(!(rb&&back==0&&readable&&inSource)){InterlockedIncrement(&g_preconditionRefusals);Log("[CAPTURE_POST] unit=%08X:%08X result=REFUSED reason=POST_VANILLA_STATE backlink=%p source_present=%s",k0,k1,(void*)back,inSource?"YES":"NO");return;}

    call_country_remove(source,(void*)leader);bool readable2=false;bool inSource2=registry_contains(source,(void*)leader,&readable2);bool removed=readable2&&!inSource2;HeldLeader held={};bool queued=false;
    if(removed)queued=HeldPush((void*)leader,source,sourceId,sourceTag,k0,k1,route,&held);
    if(removed&&!queued){g_countryLeaderAdd(source,(void*)leader,0);Log("[CAPTURE_ROLLBACK] unit=%08X:%08X reason=HELD_QUEUE_COMMIT_FAILED source_readd=ATTEMPTED",k0,k1);}
    if(removed&&queued)InterlockedIncrement(&g_totalCaptured);
    Log("[CAPTURE_HOLD] unit=%08X:%08X serial=%u leader=%p source=%s(%u) captor_id=%u queued=%s held_count=%u result=%s",k0,k1,held.serial,(void*)leader,sourceTag,sourceId,captorId,queued?"YES":"NO",HeldCount(),(removed&&queued)?"PASS":"FAIL");
    // Re-read after queue commit: the module Tick may have resolved the coalition captor
    // in the small window between our pre-snapshot and the completed Vanilla removal.
    uint32_t latestCaptor=captorId;
    if(removed&&queued){EnterCriticalSection(&g_ctxLock);PendingContext* latest=FindContextUnlocked(k0,k1);if(latest&&latest->resolved_captor)latestCaptor=latest->resolved_captor;LeaveCriticalSection(&g_ctxLock);}
    if(removed&&queued&&latestCaptor)TryAutoTransferUnit(k0,k1,latestCaptor,"CAPTURE_POST");
}

// ---------------- patching ----------------
static bool IsImageRangeAvailable(uint32_t rva,SIZE_T length){return g_exe&&rva<=g_exeSize&&length<=g_exeSize-rva;}
static bool PatchCall(uint32_t rva,void* hook){if(g_callPatchCount>=8||!IsImageRangeAvailable(rva,5)||!hook)return false;uint8_t* site=g_exe+rva;PatchCallRec& rec=g_callPatches[g_callPatchCount];rec.site=site;rec.installed=false;CopyMemory(rec.original,site,5);DWORD old=0;if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&old))return false;intptr_t rel=(uint8_t*)hook-(site+5);site[0]=0xE8;*(int32_t*)(site+1)=(int32_t)rel;FlushInstructionCache(GetCurrentProcess(),site,5);DWORD tmp=0;VirtualProtect(site,5,old,&tmp);rec.installed=true;g_callPatchCount++;return true;}
static void RestoreCallPatches(){for(int i=g_callPatchCount-1;i>=0;i--){PatchCallRec& r=g_callPatches[i];if(!r.installed||!r.site)continue;DWORD old=0;if(VirtualProtect(r.site,5,PAGE_EXECUTE_READWRITE,&old)){CopyMemory(r.site,r.original,5);FlushInstructionCache(GetCurrentProcess(),r.site,5);DWORD tmp=0;VirtualProtect(r.site,5,old,&tmp);}r.installed=false;}ZeroMemory(g_callPatches,sizeof(g_callPatches));g_callPatchCount=0;}
static bool write_rel_jmp(void* at,void* dst,SIZE_T span){if(span<5)return false;DWORD old=0;if(!VirtualProtect(at,span,PAGE_EXECUTE_READWRITE,&old))return false;BYTE* p=(BYTE*)at;intptr_t rel=(BYTE*)dst-(p+5);p[0]=0xE9;*(int32_t*)(p+1)=(int32_t)rel;for(SIZE_T i=5;i<span;i++)p[i]=0x90;FlushInstructionCache(GetCurrentProcess(),at,span);DWORD tmp=0;VirtualProtect(at,span,old,&tmp);return true;}
static bool InstallSharedHook(void* target){
    const SIZE_T patchLength=SharedPatchLength();
    if(!target||patchLength<5||patchLength>sizeof(g_sharedHook.original))return false;
    g_sharedHook.target=target;memcpy(g_sharedHook.original,target,patchLength);
    BYTE* trampoline=(BYTE*)g_sharedHook.trampoline;
    if(!trampoline){
        trampoline=(BYTE*)VirtualAlloc(0,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
        if(!trampoline)return false;
        memcpy(trampoline,target,patchLength);BYTE* jump=trampoline+patchLength;jump[0]=0xE9;*(int32_t*)(jump+1)=(int32_t)(((BYTE*)target+patchLength)-(jump+5));
        g_sharedHook.trampoline=trampoline;
    }
    g_origSharedRemove=(SharedRemove_t)trampoline;
    if(!write_rel_jmp(target,(void*)&hkSharedRemove,patchLength)){g_sharedHook.target=0;return false;}
    g_sharedHook.installed=true;return true;
}
static void UninstallSharedHook(){
    if(!g_sharedHook.installed)return;
    const SIZE_T patchLength=SharedPatchLength();
    DWORD old=0;if(patchLength&&VirtualProtect(g_sharedHook.target,patchLength,PAGE_EXECUTE_READWRITE,&old)){memcpy(g_sharedHook.target,g_sharedHook.original,patchLength);FlushInstructionCache(GetCurrentProcess(),g_sharedHook.target,patchLength);DWORD tmp=0;VirtualProtect(g_sharedHook.target,patchLength,old,&tmp);}
    g_sharedHook.target=0;ZeroMemory(g_sharedHook.original,sizeof(g_sharedHook.original));g_sharedHook.installed=false;
}

// ---------------- worker / safety controls ----------------
static bool key_pressed_once(int vk){static bool was[256]={};bool now=(GetAsyncKeyState(vk)&0x8000)!=0;bool press=now&&!was[vk&0xff];was[vk&0xff]=now;return press;}
static void LogState(){
    unsigned held=HeldCount();
    EnterCriticalSection(&g_stateLock);
    unsigned watches=g_transferWatchCount;
    LeaveCriticalSection(&g_stateLock);
    Log("[STATE] held=%u/%u captured=%ld transferred=%ld released=%ld combatA=%ld combatB=%ld no_context_refusals=%ld precondition_refusals=%ld transfer_failures=%ld transfer_watches=%u reversion_suspects=%ld combatA_preseed_created=%ld combatA_preseed_refused=%ld do_not_save_while_held=%s",
        held,MAX_HELD,(long)g_totalCaptured,(long)g_totalTransferred,(long)g_totalReleased,(long)g_combatAHits,(long)g_combatBHits,
        (long)g_noContextRefusals,(long)g_preconditionRefusals,(long)g_transferFailures,watches,(long)g_reversionSuspects,
        (long)g_combatAPreseedCreated,(long)g_combatAPreseedRefused,held?"YES":"NO");
    EnterCriticalSection(&g_stateLock);
    for(unsigned i=0;i<g_heldCount;i++){
        HeldLeader h=g_held[i];
        Log("[HELD] index=%u serial=%u unit=%08X:%08X leader=%p source=%s(%u) route=%s age_ms=%lu",
            i,h.serial,h.unitKey0,h.unitKey1,h.leader,h.sourceTag,h.sourceCountryId,
            h.route==ROUTE_COMBAT_A_DIRECT?"COMBAT_A":"COMBAT_B",(unsigned long)(GetTickCount()-h.capturedTick));
    }
    for(unsigned i=0;i<MAX_TRANSFER_WATCH;i++){
        TransferWatch& w=g_transferWatch[i];if(!w.active)continue;
        bool rs=false,rt=false;bool inS=registry_contains(w.sourceCountry,w.leader,&rs);bool inT=registry_contains(w.targetCountry,w.leader,&rt);
        Log("[TRANSFERRED_WATCH] serial=%u leader=%p age_ms=%lu source=%s(%u) source_present=%s(%s) target=%s(%u) target_present=%s(%s) reversion_suspect=%s",
            w.serial,w.leader,(unsigned long)(GetTickCount()-w.startedTick),w.sourceTag,w.sourceCountryId,
            inS?"YES":"NO",rs?"READ":"N/A",w.targetTag,w.targetCountryId,inT?"YES":"NO",rt?"READ":"N/A",
            w.reversionSuspectLogged?"YES":"NO");
    }
    LeaveCriticalSection(&g_stateLock);
}
static void PollRuntime(DWORD now){
    struct ResolveEvent{uint32_t k0,k1,captor;} ev[64];unsigned evn=0;
    EnterCriticalSection(&g_ctxLock);
    for(unsigned i=0;i<MAX_CONTEXT;i++){
        PendingContext* c=&g_ctx[i];if(!c->active||!c->province)continue;if((DWORD)(now-c->last_touch_ms)>CONTEXT_TTL_MS){c->active=0;continue;}
        uint32_t controller=0;if(!ReadU32(c->province+ProvinceControllerCountryIndexOffset,&controller))continue;if(controller!=c->controller_last){uint32_t old=c->controller_last;c->controller_last=controller;c->last_touch_ms=now;uint32_t tag=0;bool member=WinnerContains(*c,controller,&tag);char tt[4]={'-','-','-',0};if(member)FormatTag(tag,tt);Log("[CONTROLLER_CHANGE] unit=%08X:%08X province=%08X old_id=%u new_id=%u new_tag_if_winner=%s winner_member=%s",c->key0,c->key1,(uint32_t)c->province,old,controller,tt,member?"YES":"NO");}
        uint32_t newly=0;if(RefreshCaptorUnlocked(c,controller,"CONTROLLER_POLL",&newly)&&newly&&c->remove_seen&&evn<64){ev[evn].k0=c->key0;ev[evn].k1=c->key1;ev[evn].captor=newly;evn++;}
    }
    LeaveCriticalSection(&g_ctxLock);
    for(unsigned i=0;i<evn;i++)TryAutoTransferUnit(ev[i].k0,ev[i].k1,ev[i].captor,"CONTROLLER_RESOLVED");
    PollTransferWatches();
    if(key_pressed_once(VK_F6))ReleaseOldestForSafety();
    if(key_pressed_once(VK_F8))LogState();
}

static bool ValidateAll(){
    const core::engine::SymbolId mandatory[]={
        core::engine::SymbolId::LeaderOutcomeCallA,
        core::engine::SymbolId::LeaderOutcomeCallB,
        core::engine::SymbolId::LeaderWithdrawDeferredCall,
        core::engine::SymbolId::LeaderCombatADirectCall,
        core::engine::SymbolId::LeaderSharedRemove,
        core::engine::SymbolId::LeaderManualSite,
        core::engine::SymbolId::LeaderCombatASite,
        core::engine::SymbolId::LeaderCombatBSite,
        core::engine::SymbolId::CountryLeaderAdd,
        core::engine::SymbolId::CountryLeaderRemove
    };
    auto& registry=core::engine::GetEngineRegistry();
    for(const auto symbol:mandatory){std::string validationError;if(!registry.ValidateSymbol(symbol,validationError)){Log("[VERIFY] registry validation failed symbol=%u reason=%s",(unsigned)symbol,validationError.c_str());return false;}}
    return true;
}

namespace leader_capture
{

bool Initialize(std::string& error){
    if(InterlockedCompareExchange(&g_initialized,0,0)){error.clear();return true;}
    if(InterlockedCompareExchange(&g_primitivesInitialized,1,0)==0){InitializeCriticalSection(&g_logLock);InitializeCriticalSection(&g_ctxLock);InitializeCriticalSection(&g_stateLock);}
    if(g_log==INVALID_HANDLE_VALUE&&!OpenLog()){error="leader_capture_log_open_failed";return false;}
    auto& registry=core::engine::GetEngineRegistry();if(!registry.InitializeCurrentProcess(error))return false;
    g_exe=(uint8_t*)registry.ModuleBase();g_exeSize=registry.Identity().imageSize;
    if(!g_exe||!g_exeSize){error="leader_capture_executable_unavailable";return false;}
    g_originalBattleOutcome=(uint8_t*)registry.Resolve(core::engine::SymbolId::LeaderBattleOutcome);g_originalDeferEnqueue=(uint8_t*)registry.Resolve(core::engine::SymbolId::LeaderDeferredEnqueue);g_sharedRemoveEntry=(uint8_t*)registry.Resolve(core::engine::SymbolId::LeaderSharedRemove);g_countryLeaderAdd=(CountryLeaderAdd_t)registry.Resolve(core::engine::SymbolId::CountryLeaderAdd);g_countryLeaderRemoveAddr=(void*)registry.Resolve(core::engine::SymbolId::CountryLeaderRemove);
    InterlockedExchange(&g_initialized,1);
    Log("[BOOT] LEADER_CAPTURE_AUTO_TRANSFER_R06B_D328 exe_base=%08X image_size=%08X owner=NEW_CORE_MODULE policy=CAPTURE_ALL_PROVEN_COMBAT_DESTRUCTIONS_THEN_AUTO_TRANSFER",(uint32_t)g_exe,g_exeSize);
    Log("[BOOT] attribution=SOLE_WINNER_OR_CONTROLLER_INTERSECTION unknown=HOLD_ONLY hooks=BattleOutcomeCalls+CombatADirectPreseedCall+ExactWithdrawDeferCaller+SharedRemoveInline future_policy_seams=ENABLED");
    Log("[BOOT] post_transfer_registry_watch_ms=%lu diagnostic_only=YES",(unsigned long)TRANSFER_WATCH_TTL_MS);
    error.clear();return true;
}

bool InstallHooks(std::string& error){
    if(AreHooksInstalled()){error.clear();return true;}
    if(!Initialize(error))return false;
    if(!ValidateAll()){error="leader_capture_signature_validation_failed";if(InterlockedCompareExchange(&g_validationFailureLogged,1,0)==0)Log("[FATAL] signature/call validation failed. NO PATCHES OR GAMEPLAY WRITES INSTALLED.");return false;}
    InterlockedExchange(&g_validationFailureLogged,0);
    Log("[VERIFY] all mandatory signatures/calls PASS");
    bool ok=true;
    ok=ok&&PatchCall((uint32_t)EngineSymbols.leaderOutcomeCallA,(void*)&HookBattleOutcome);
    ok=ok&&PatchCall((uint32_t)EngineSymbols.leaderOutcomeCallB,(void*)&HookBattleOutcome);
    ok=ok&&PatchCall((uint32_t)EngineSymbols.leaderWithdrawDeferredCall,(void*)&HookDeferEnqueue);
    ok=ok&&PatchCall((uint32_t)EngineSymbols.leaderCombatADirectCall,(void*)&HookCombatADirectCall);
    ok=ok&&InstallSharedHook((void*)core::engine::GetEngineRegistry().Resolve(core::engine::SymbolId::LeaderSharedRemove));
    if(!ok){RestoreCallPatches();UninstallSharedHook();InterlockedExchange(&g_hooksInstalled,0);error="leader_capture_patch_install_failed";Log("[FATAL] hook installation failed; restored partial hooks");return false;}
    InterlockedExchange(&g_hooksInstalled,1);
    Log("[READY] owner=NEW_CORE_MODULE gameplay_writes=GATED_BY_LIFECYCLE scope=LEADER_REGISTRY_ONLY province_writes=DISABLED owner_tuple_writes=DISABLED manual_route=IGNORE");
    Log("[CONTROL] F6=RELEASE_OLDEST_HELD_SAFETY F8=LOG_STATE. No configured target tag; all transfers require native Captor resolution.");
    error.clear();return true;
}

void UninstallHooks(){
    SetGameplayActive(false);RestoreCallPatches();UninstallSharedHook();InterlockedExchange(&g_hooksInstalled,0);
}

bool AreHooksInstalled(){
    if(!InterlockedCompareExchange(&g_hooksInstalled,0,0)||g_callPatchCount!=4||!g_sharedHook.installed)return false;
    for(int i=0;i<g_callPatchCount;i++)if(!g_callPatches[i].installed)return false;
    return true;
}

void SetGameplayActive(bool active){InterlockedExchange(&g_gameplayActive,active?1:0);}

bool IsGameplayActive(){return InterlockedCompareExchange(&g_gameplayActive,0,0)!=0;}

void SetSaveLoadBarrier(core::NativeSaveLoadBarrier* barrier){g_saveLoadBarrier=barrier;}

void ResetSessionState(){
    if(!InterlockedCompareExchange(&g_initialized,0,0))return;
    EnterCriticalSection(&g_ctxLock);ZeroMemory(g_ctx,sizeof(g_ctx));g_replaceCursor=0;LeaveCriticalSection(&g_ctxLock);
    EnterCriticalSection(&g_stateLock);ZeroMemory(g_held,sizeof(g_held));g_heldCount=0;g_nextSerial=1;ZeroMemory(g_transferWatch,sizeof(g_transferWatch));g_transferWatchCount=0;LeaveCriticalSection(&g_stateLock);
    InterlockedExchange(&g_reversionSuspects,0);InterlockedExchange(&g_totalCaptured,0);InterlockedExchange(&g_totalTransferred,0);InterlockedExchange(&g_totalReleased,0);InterlockedExchange(&g_combatAHits,0);InterlockedExchange(&g_combatBHits,0);InterlockedExchange(&g_noContextRefusals,0);InterlockedExchange(&g_preconditionRefusals,0);InterlockedExchange(&g_transferFailures,0);InterlockedExchange(&g_manualHits,0);InterlockedExchange(&g_combatAPreseedCreated,0);InterlockedExchange(&g_combatAPreseedRefused,0);g_nextTickMilliseconds=0;
    Log("[LIFECYCLE] session state reset");
}

void Tick(uint64_t nowMilliseconds){
    if(!IsGameplayActive()||!AreHooksInstalled()||nowMilliseconds<g_nextTickMilliseconds)return;
    auto writeLease=AcquireNativeStateLease();if(!writeLease)return;
    g_nextTickMilliseconds=nowMilliseconds+100;PollRuntime(GetTickCount());
}

}

// SPDX-License-Identifier: GPL-2.0-or-later
#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include "common/bounded_client.hpp"
#include "common/trace.hpp"
#include "overlay/brightness_control.hpp"
#include "overlay/brightness_scale.hpp"
#include "overlay/brightness_service.hpp"
#include <cstdio>

namespace {
using namespace touchpad;
BoundedClient client;
Trace trace;
bool sdMounted = false;
Settings draft;
Status status;
std::string message;
u64 pendingToken = 0;
bool pending = false;
bool online = false;
bool showAllowed = true;
Result lastCallResult = 0;
std::string launchMessage = "后台需手动启动";
BrightnessService brightnessDevice;
u64 showEpoch = 0;

void launchBackend() {
    trace.write("button-start-backend",online);
    if (online) { launchMessage = "后台已运行，请选择启用并返回游戏"; trace.write("start-skipped-online"); return; }
    // A stale/failed IPC status must never result in launching a duplicate PID.
    u64 existing=0;
    trace.write("start-pid-query-begin");
    Result lookup=pmdmntGetProcessId(&existing,0x4200000054504144ULL);
    trace.write("start-pid-query-end",existing,lookup);
    if (R_SUCCEEDED(lookup)) {
        launchMessage = "后台进程已存在；连接异常请重启主机";
        return;
    }
    // Atmosphere pm::ResultProcessNotFound; other lookup failures are unknown.
    if (R_VALUE(lookup)!=MAKERESULT(15,1)) {
        char error[80]; std::snprintf(error,sizeof(error),"查询后台失败：%08X",lookup);
        launchMessage=error; return;
    }
    // Tesla only keeps SM open inside doWithSmSession callbacks. UI listeners
    // run outside those callbacks, so acquire our own reference before PM init.
    const char* stage = "连接服务管理器";
    Result rc = smInitialize();
    const bool smReady = R_SUCCEEDED(rc);
    if (smReady) {
        stage = "连接启动服务";
        rc = pmshellInitialize();
    }
    u64 pid = 0;
    if (R_SUCCEEDED(rc)) {
        stage = "启动后台进程";
        NcmProgramLocation location{};
        location.program_id = 0x4200000054504144ULL;
        location.storageID = NcmStorageId_None;
        trace.write("pm-launch-begin");
        rc = pmshellLaunchProgram(0, &location, &pid);
        trace.write("pm-launch-end",pid,rc);
        pmshellExit();
    }
    if (smReady) smExit();
    char text[100];
    if (R_SUCCEEDED(rc)) std::snprintf(text,sizeof(text),"已请求启动，等待后台就绪");
    else std::snprintf(text,sizeof(text),"%s失败：%08X",stage,rc);
    launchMessage = text;
}

bool call(Command command, uint64_t token = 0) {
    Request request;
    request.command = command; request.token = token; request.settings = draft;
    if (command!=Command::Status) trace.write("ipc-begin",static_cast<u32>(command));
    Status next{};
    Result rc = client.call(request,next);
    lastCallResult=rc;
    if (command!=Command::Status || (online && R_FAILED(rc))) trace.write("ipc-end",static_cast<u32>(command),rc);
    if (R_FAILED(rc)) {
        online = false;
        status = {};
        char buffer[96]; std::snprintf(buffer,sizeof(buffer),"后台未连接：%08X",rc);
        message = R_VALUE(rc)==KERNELRESULT(TimedOut) ? "后台请求超时；可按 HOME，返回后重开插件" : buffer;
        return false;
    }
    status=next;
    if (!online) message.clear();
    online = true;
    if (status.error != Error::Ok) {
        switch (status.error) {
            case Error::NoApplication: message = "请先启动游戏或全内存测试程序"; break;
            case Error::Unavailable: message = "后台或显示层尚未就绪"; break;
            case Error::Storage: message = "设置保存失败，请检查 SD 卡"; break;
            case Error::InvalidTransition: message = "启用已取消，请重新打开菜单"; break;
            default: message = "请求失败，请检查插件与后台版本"; break;
        }
        return false;
    }
    return true;
}
class SettingItem : public tsl::elm::ListItem {
    uint32_t* value_;
    uint32_t low_, high_;
    void change(int direction) {
        *value_ = static_cast<uint32_t>(std::clamp(static_cast<int>(*value_)+5*direction,static_cast<int>(low_),static_cast<int>(high_)));
        sync();
    }
public:
    SettingItem(const char* name, uint32_t* value, uint32_t low, uint32_t high)
        : ListItem(name), value_(value), low_(low), high_(high) {
        sync();
        setClickListener([this](u64 keys) { if (!(keys & HidNpadButton_A)) return false; change(*value_ == high_ ? -1 : 1); return true; });
    }
    void sync() { const auto text = std::to_string(*value_)+"%"; if (getValue() != text) setValue(text); }
    bool handleInput(u64 down,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) override {
        if (down & HidNpadButton_AnyLeft) { change(-1); return true; }
        if (down & HidNpadButton_AnyRight) { change(1); return true; }
        return false;
    }
};
template<class Enum>
class ChoiceItem : public tsl::elm::ListItem {
    Enum* value_;
    const char* labels_[2];
    void select(unsigned index) { *value_=static_cast<Enum>(index); sync(); }
public:
    ChoiceItem(const char* name, Enum* value, const char* first, const char* second)
        : ListItem(name), value_(value), labels_{first,second} {
        sync();
        setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            select(static_cast<unsigned>(*value_)==0 ? 1 : 0); return true;
        });
    }
    void sync() {
        const auto* text=labels_[static_cast<unsigned>(*value_)==0 ? 0 : 1];
        if (getValue()!=text) setValue(text);
    }
    bool handleInput(u64 down,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) override {
        if (down & HidNpadButton_AnyLeft) { select(0); return true; }
        if (down & HidNpadButton_AnyRight) { select(1); return true; }
        return false;
    }
};
class BrightnessGui : public tsl::Gui {
    BrightnessControl<BrightnessService> control_{brightnessDevice};
    tsl::elm::ListItem *level_ = nullptr, *automatic_ = nullptr, *result_ = nullptr;
    tsl::elm::StepTrackBar* slider_ = nullptr;
    u64 epoch_ = 0;
    bool saved_ = false;
    bool backRequested_ = false;
    void sync() {
        level_->setValue(control_.available ? std::to_string(control_.percent)+"%" : "读取失败");
        automatic_->setValue(control_.available ? control_.automatic ? "开启（保持系统设置）" : "关闭" : "未知");
        if (control_.available) slider_->setProgress(BrightnessScale::toStep(control_.percent));
        if (control_.result) {
            constexpr const char* names[]{"操作","连接","读取","设置","应用","保存"};
            char text[100];
            std::snprintf(text,sizeof(text),"亮度%s失败：%08X",names[static_cast<unsigned>(control_.stage)],control_.result);
            result_->setText(text);
        } else result_->setText(saved_ ? "已保存系统亮度" : control_.unsaved ? "已调整；可点保存记住设置" : "拖动立即生效；自动亮度保持原样");
    }
    void read() {
        saved_ = false;
        control_.read();
        trace.write("brightness-read",control_.percent,control_.result);
        sync();
    }
public:
    ~BrightnessGui() override { brightnessDevice.close(); }
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Touchpad", "屏幕亮度");
        auto* list = new tsl::elm::List();
        level_ = new tsl::elm::ListItem("系统亮度"); list->addItem(level_);
        slider_ = new tsl::elm::StepTrackBar("\u2600",BrightnessScale::Steps);
        slider_->setValueChangedListener([this](u8 step) {
            const auto value = BrightnessScale::toPercent(step);
            saved_ = false;
            control_.set(value);
            trace.write("brightness-set",value,control_.result);
            sync();
        });
        list->addItem(slider_);
        automatic_ = new tsl::elm::ListItem("自动亮度"); list->addItem(automatic_);
        result_ = new tsl::elm::ListItem(""); list->addItem(result_);
        auto* save = new tsl::elm::ListItem("保存系统亮度");
        save->setClickListener([this](u64 keys) {
            if (!(keys&HidNpadButton_A)) return false;
            saved_ = control_.save();
            trace.write("brightness-save",control_.percent,control_.result);
            sync(); return true;
        });
        list->addItem(save);
        auto* refresh = new tsl::elm::ListItem("重新读取亮度");
        refresh->setClickListener([this](u64 keys) {
            if (!(keys&HidNpadButton_A)) return false;
            read(); return true;
        });
        list->addItem(refresh);
        auto* back = new tsl::elm::ListItem("返回 Touchpad");
        back->setClickListener([this](u64 keys) {
            if (!(keys&HidNpadButton_A)) return false;
            backRequested_ = true; return true;
        });
        list->addItem(back);
        frame->setContent(list);
        epoch_ = showEpoch;
        read();
        return frame;
    }
    void update() override {
        // List touch dispatch still traverses its children after onClick.
        // Pop only after dispatch returns, never delete that live iterator.
        if (backRequested_) { tsl::goBack(); return; }
        // Re-read on every show, including sleep/wake, without replaying an old
        // slider value or writing while the menu is hidden.
        if (epoch_ != showEpoch) { epoch_ = showEpoch; read(); }
    }
    bool handleInput(u64 down,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) override {
        // Covers controller B and libtesla's touch footer Back action too.
        if (down&HidNpadButton_B) { backRequested_ = true; return true; }
        return false;
    }
};
class Gui : public tsl::Gui {
    tsl::elm::ListItem* state_ = nullptr;
    tsl::elm::ListItem* detail_ = nullptr;
    tsl::elm::ListItem* info_ = nullptr;
    tsl::elm::ListItem* display_ = nullptr;
    tsl::elm::ListItem* error_ = nullptr;
    tsl::elm::ListItem* firmware_ = nullptr;
    tsl::elm::ListItem* launch_ = nullptr;
    SettingItem* settings_[4]{};
    ChoiceItem<StickMode>* stickMode_ = nullptr;
    ChoiceItem<ArcadeOutput>* arcadeOutput_ = nullptr;
    u64 lastPoll_ = 0;
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("Touchpad", "1.1.0-beta.5");
        auto* list = new tsl::elm::List();
        state_ = new tsl::elm::ListItem("后台状态");
        list->addItem(state_);
        auto* start = new tsl::elm::ListItem("启动后台（本次开机）");
        start->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            launchBackend(); return true;
        });
        list->addItem(start);
        launch_ = new tsl::elm::ListItem(launchMessage);
        list->addItem(launch_);
        list->addItem(new tsl::elm::CategoryHeader("请在游戏画面中启用"));
        auto* enable = new tsl::elm::ListItem("启用并返回游戏");
        enable->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            trace.write("button-enable");
            if (call(Command::Configure) && call(Command::Prepare)) {
                pendingToken = status.token; pending = true; message.clear();
                tsl::Overlay::get()->hide();
            }
            return true;
        });
        list->addItem(enable);
        auto* stop = new tsl::elm::ListItem("关闭并断开虚拟手柄");
        stop->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            trace.write("button-stop");
            pending = false;
            if (call(Command::Stop)) message = "已关闭";
            return true;
        });
        list->addItem(stop);
        auto* brightness = new tsl::elm::ListItem("屏幕亮度", "触摸调整");
        brightness->setClickListener([](u64 keys) {
            if (!(keys&HidNpadButton_A)) return false;
            tsl::changeTo<BrightnessGui>(); return true;
        });
        list->addItem(brightness);
        list->addItem(new tsl::elm::CategoryHeader("外观与手感 · 左右调整"));
        stickMode_=new ChoiceItem("摇杆模式",&draft.stick_mode,"模拟","街机八方向");
        arcadeOutput_=new ChoiceItem("街机输出",&draft.arcade_output,"十字键","左摇杆");
        list->addItem(stickMode_);
        list->addItem(arcadeOutput_);
        settings_[0] = new SettingItem("不透明度",&draft.opacity,10,80);
        settings_[1] = new SettingItem("控件大小",&draft.scale,75,125);
        settings_[2] = new SettingItem("模拟摇杆死区",&draft.deadzone,5,30);
        settings_[3] = new SettingItem("街机回中区",&draft.arcade_deadzone,5,30);
        for (auto* setting : settings_) list->addItem(setting);
        auto* save = new tsl::elm::ListItem("保存设置");
        save->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (call(Command::Configure)) message = "设置已保存";
            return true;
        });
        list->addItem(save);
        list->addItem(new tsl::elm::CategoryHeader("首次连接 · 系统更改握法/顺序"));
        auto* pair = new tsl::elm::ListItem("发送一次 L + R 并返回");
        pair->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            trace.write("button-pair");
            if (call(Command::PreparePair)) {
                pendingToken = status.token; pending = true; message.clear();
                tsl::Overlay::get()->hide();
            }
            return true;
        });
        list->addItem(pair);
        list->addItem(new tsl::elm::CategoryHeader("诊断 · HOME 或 Tesla 可停止"));
        detail_ = new tsl::elm::ListItem("虚拟手柄"); list->addItem(detail_);
        display_ = new tsl::elm::ListItem("显示层"); list->addItem(display_);
        error_ = new tsl::elm::ListItem("错误码"); list->addItem(error_);
        firmware_ = new tsl::elm::ListItem("系统版本"); list->addItem(firmware_);
        info_ = new tsl::elm::ListItem("提示"); list->addItem(info_);
        frame->setContent(list);
        return frame;
    }
    void update() override {
        const u64 now = armTicksToNs(armGetSystemTick())/1000000;
        if (now-lastPoll_ >= 500) {
            call(Command::Status); lastPoll_ = now;
        }
        state_->setValue(online ? modeName(status.mode) : "未连接");
        if (launch_->getText() != launchMessage) launch_->setText(launchMessage);
        char text[100];
        detail_->setValue(!online ? "未知" : status.connected ? "已连接" : "未启用时不连接");
        display_->setValue(!online ? "未知" : status.renderer_ready ? "已就绪" : "未就绪");
        std::snprintf(text,sizeof(text),"%08X",status.backend_result);
        error_->setValue(online ? text : "无法读取");
        std::snprintf(text,sizeof(text),"%u.%u.%u",(status.hos_version>>16)&255,(status.hos_version>>8)&255,status.hos_version&255);
        firmware_->setValue(text);
        const auto info = message.empty() ? std::string(reasonName(status.reason)) : message;
        if (info_->getText() != info) info_->setText(info);
        for (auto* setting : settings_) setting->sync();
        stickMode_->sync();
        arcadeOutput_->sync();
    }
};
class Overlay : public tsl::Overlay {
    void initServices() override {
        sdMounted=R_SUCCEEDED(fsdevMountSdmc());
        if (sdMounted) trace.open("sdmc:/config/touchpad/overlay.log","sdmc:/config/touchpad/overlay.previous.log");
        trace.write("1.1.0-beta.5-overlay-start");
        if (call(Command::Status)) draft = status.settings;
    }
    void onShow() override {
        ++showEpoch;
        trace.write("overlay-show");
        pending = false;
        showAllowed=call(Command::Pause);
        if (showAllowed) draft = status.settings;
        // A missing named port is the expected pre-launch state. Any other
        // failure leaves the previous layer's removal unconfirmed.
        else showAllowed=!online && R_VALUE(lastCallResult)==KERNELRESULT(NotFound);
        if (!showAllowed) trace.write("show-blocked-unconfirmed-detach",0,lastCallResult);
    }
    bool canShow() override { return showAllowed; }
    void onHide() override {
        brightnessDevice.close();
        trace.write("overlay-hide-request",pending);
    }
    void onFocusChange(bool foreground, Result result) override {
        trace.write(foreground ? "focus-taken" : "focus-returned",0,result);
        if (R_FAILED(result)) {
            pending=false;
            char text[96]; std::snprintf(text,sizeof(text),"输入恢复失败：%08X；请按 HOME",result);
            message=text;
        }
    }
    void onLifecycle(const char* stage) override { trace.write(stage); }
    void onHideCause(u32 reason) override { trace.write("hide-cause-bits",reason); }
    void onLayerChange(bool visible,u64 id,s32 z,Result result) override {
        trace.write(visible ? "menu-layer-attached" : "menu-layer-detached",id,result);
        trace.write("menu-layer-z",static_cast<u64>(z));
        if (R_FAILED(result)) {
            pending=false;
            char text[96]; std::snprintf(text,sizeof(text),"菜单显示切换失败：%08X；请按 HOME",result);
            message=text;
        }
    }
    void onHidden() override {
        trace.write("overlay-hidden",pending);
        if (pending) { pending = false; call(Command::Commit,pendingToken); }
    }
    bool canClose() override {
        if (client.canClose()) return true;
        message="后台请求未结束；可按 HOME，暂不能卸载界面";
        return false;
    }
    void exitServices() override {
        brightnessDevice.close();
        client.close(); trace.write("overlay-exit"); trace.close();
        if (sdMounted) fsdevUnmountDevice("sdmc");
    }
    std::unique_ptr<tsl::Gui> loadInitialGui() override { return std::make_unique<Gui>(); }
};
}
int main(int argc,char** argv) { return tsl::loop<Overlay>(argc,argv); }

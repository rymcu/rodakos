#include "test_framework.h"

#include <fstream>
#include <string>

#include "apps/wol/wol_app.h"
#include "apps/wol/wol_device_codec.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/wake_on_lan_service.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "settings.h"

#include <src/others/test/lv_test.h>

namespace {

void Pump(uint32_t ms = 20) {
    lv_test_wait(ms);
    lv_obj_update_layout(lv_screen_active());
}

void Screenshot(const char* name) {
    Pump(300);
    const auto* frame = lv_display_get_buf_active(lv_display_get_default());
    std::ofstream out(name, std::ios::binary);
    out << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            const auto* p = frame->data + y * frame->header.stride + x * 4;
            const char rgb[] = {static_cast<char>(p[2]), static_cast<char>(p[1]), static_cast<char>(p[0])};
            out.write(rgb, 3);
        }
    }
}

bool HasText(lv_obj_t* root, const std::string& expected) {
    if (root == nullptr || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return false;
    if (lv_obj_check_type(root, &lv_label_class) && expected == lv_label_get_text(root)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (HasText(lv_obj_get_child(root, i), expected)) return true;
    }
    return false;
}

bool OnScreen(const std::string& text) {
    return HasText(lv_screen_active(), text) || HasText(lv_layer_top(), text);
}

std::string Text(lv_obj_t* label) {
    RODAK_CHECK(label != nullptr);
    return label != nullptr ? lv_label_get_text(label) : "";
}

void Click(lv_obj_t* obj) {
    RODAK_CHECK(obj != nullptr);
    lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
    Pump();
}

std::string StoredJson(std::initializer_list<rodakos::WolDevice> devices) {
    const auto encoded = rodakos::EncodeWolDevices(std::vector<rodakos::WolDevice>(devices));
    RODAK_CHECK(encoded.status == rodakos::WolDeviceCodecStatus::kOk);
    return encoded.json;
}

const rodakos::WolDevice kDesk{"书房台式机", "AA:BB:CC:DD:EE:FF", "255.255.255.255", 9};
const rodakos::WolDevice kNas{"客厅 NAS", "D8:3A:DD:42:19:0C", "255.255.255.255", 9};

struct Fixture {
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    WiFiAdapter wifi;
    rodakos::WakeOnLanService wol{&wifi};
    WolApp app;
    bool created = false;

    explicit Fixture(const char* theme = "dark") {
        rodakos_theme_init_from_name(theme);
        ui.SyncThemeName(theme);
        rodakos_home_ui_test::ResetSettings();
        services.SetWiFi(&wifi);
        services.SetWakeOnLan(&wol);
    }
    ~Fixture() {
        if (created) app.OnDestroy();
        lv_test_wait(2500);
        lv_obj_clean(lv_screen_active());
        lv_obj_clean(lv_layer_top());
        rodakos_theme_init(RODAKOS_THEME_DARK);
    }
    void Store(std::initializer_list<rodakos::WolDevice> devices) {
        rodakos_home_ui_test::SetCommittedSetting("wol", "devices", StoredJson(devices));
    }
    void Create() {
        RODAK_CHECK(app.OnCreate(context));
        created = true;
        Pump();
    }
    lv_obj_t* Card(int index) { return lv_obj_get_child(app.device_list_, index); }
    lv_obj_t* CardInfo(int index) { return lv_obj_get_child(Card(index), 0); }
    lv_obj_t* CardPower(int index) { return lv_obj_get_child(Card(index), 2); }
    lv_obj_t* FormRow(int index) { return lv_obj_get_child(lv_obj_get_child(app.editor_, 1), index); }
    void Type(const char* text) {
        RODAK_CHECK(app.input_textarea_ != nullptr);
        lv_textarea_set_text(app.input_textarea_, text);
        app.SubmitInput();
        Pump();
    }
    std::vector<rodakos::WolDevice> Saved() {
        const auto json = rodakos_home_ui_test::GetCommittedSetting("wol", "devices");
        return json.empty() ? std::vector<rodakos::WolDevice>{} : rodakos::DecodeWolDevices(json).devices;
    }
};

}  // namespace

RODAK_TEST("device list shows Wi-Fi, the count and one card per device") {
    Fixture f;
    f.Store({kDesk, kNas});
    f.Create();
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("网络唤醒"));
    RODAK_CHECK_EQ(Text(f.app.strip_label_), std::string("书房-5G"));
    RODAK_CHECK(OnScreen("2 / 8"));
    RODAK_CHECK_EQ(lv_obj_get_child_count(f.app.device_list_), 2u);
    RODAK_CHECK(OnScreen("书房台式机"));
    RODAK_CHECK(OnScreen("AA:BB:CC:DD:EE:FF · 端口 9"));
    // The power button is its own 46x40 target, separate from the edit area.
    lv_area_t power, info;
    lv_obj_get_coords(f.CardPower(0), &power);
    lv_obj_get_coords(f.CardInfo(0), &info);
    RODAK_CHECK_EQ(lv_area_get_width(&power), 46);
    RODAK_CHECK_EQ(lv_area_get_height(&power), 40);
    RODAK_CHECK(info.x2 < power.x1);
    Screenshot("wol-list.ppm");
}

RODAK_TEST("power sends the magic packet and shows a 2 s confirmation on the card") {
    Fixture f;
    f.Store({kDesk, kNas});
    f.Create();
    Click(f.CardPower(1));
    RODAK_CHECK_EQ(f.wol.sent.size(), 1u);
    RODAK_CHECK_EQ(f.wol.sent[0].mac, std::string("D8:3A:DD:42:19:0C"));
    RODAK_CHECK_EQ(f.wol.sent[0].port, 9);
    RODAK_CHECK(OnScreen("已发送唤醒包 · 端口 9"));
    RODAK_CHECK(f.app.editor_ == nullptr);
    Screenshot("wol-sent.ppm");
    Pump(WolApp::kSentFeedbackMs + 200);
    RODAK_CHECK_FALSE(OnScreen("已发送唤醒包 · 端口 9"));
    RODAK_CHECK(OnScreen("D8:3A:DD:42:19:0C · 端口 9"));
}

RODAK_TEST("a failed send says why and leaves the card unchanged") {
    Fixture f;
    f.Store({kDesk});
    f.wol.result = rodakos::WakeOnLanResult::kSendFailed;
    f.Create();
    Click(f.CardPower(0));
    RODAK_CHECK(OnScreen("唤醒包发送失败"));
    RODAK_CHECK_FALSE(OnScreen("已发送唤醒包 · 端口 9"));
}

RODAK_TEST("offline: the strip offers Wi-Fi settings and power is blocked") {
    Fixture f;
    f.Store({kDesk});
    f.wifi.status = WiFiStatus::kDisconnected;
    f.Create();
    RODAK_CHECK_EQ(Text(f.app.strip_label_), std::string("Wi-Fi 未连接"));
    RODAK_CHECK(OnScreen("去设置"));
    // The whole link stays inside the screen and right of the status text.
    lv_area_t link, label;
    lv_obj_get_coords(f.app.strip_right_, &link);
    lv_obj_get_coords(f.app.strip_label_, &label);
    RODAK_CHECK(link.x2 < 320);
    RODAK_CHECK(link.x1 > 120);
    auto* link_text = lv_obj_get_child(f.app.strip_right_, 0);
    lv_area_t text_area;
    lv_obj_get_coords(link_text, &text_area);
    // Text sits inside its button: LV_SIZE_CONTENT plus END alignment used to push 去 off the left.
    RODAK_CHECK(text_area.x1 >= link.x1);
    Click(f.CardPower(0));
    RODAK_CHECK(f.wol.sent.empty());
    RODAK_CHECK(OnScreen("请先连接 Wi-Fi"));
    Screenshot("wol-offline.ppm");
    Click(f.app.strip_right_);
    RODAK_CHECK_EQ(f.navigation.launches.size(), 1u);
    RODAK_CHECK_EQ(f.navigation.launches[0], std::string("settings<wol:wifi"));
    // Editing still works offline.
    Click(f.CardInfo(0));
    RODAK_CHECK(f.app.editor_ != nullptr);
}

RODAK_TEST("empty list explains the app and opens the add form") {
    Fixture f;
    f.Create();
    RODAK_CHECK(OnScreen("还没有设备"));
    RODAK_CHECK(OnScreen("0 / 8"));
    Screenshot("wol-empty.ppm");
    auto* add = lv_obj_get_child(f.app.status_box_, -1);
    Click(add);
    RODAK_CHECK(f.app.editor_ != nullptr);
    RODAK_CHECK_EQ(Text(f.app.editor_title_), std::string("添加设备"));
    RODAK_CHECK_EQ(Text(f.app.editor_values_[2]), std::string("255.255.255.255"));
    RODAK_CHECK_EQ(Text(f.app.editor_values_[3]), std::string("9"));
}

RODAK_TEST("adding a device through the single-field inputs normalises and saves it") {
    Fixture f;
    f.Create();
    Click(f.app.add_button_);
    RODAK_CHECK(f.app.editor_ != nullptr);
    Screenshot("wol-form-new.ppm");

    Click(f.FormRow(0));
    RODAK_CHECK(f.app.input_ != nullptr);
    RODAK_CHECK(f.app.soft_keyboard_.IsVisible());
    f.Type("  测试主机 ");
    RODAK_CHECK(f.app.input_ == nullptr);
    RODAK_CHECK_EQ(Text(f.app.editor_values_[0]), std::string("测试主机"));

    Click(f.FormRow(1));
    f.Type("00-1a-2b-3c-4d-5e");
    RODAK_CHECK_EQ(Text(f.app.editor_values_[1]), std::string("00:1A:2B:3C:4D:5E"));

    Click(f.FormRow(3));
    Screenshot("wol-input-port.ppm");
    // Preset chip fills 7.
    auto* box = lv_obj_get_child(f.app.input_, 0);
    auto* presets = lv_obj_get_child(box, 1);
    Click(lv_obj_get_child(presets, 1));
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(f.app.input_textarea_)), std::string("7"));
    f.app.SubmitInput();
    Pump();
    RODAK_CHECK_EQ(Text(f.app.editor_values_[3]), std::string("7"));

    f.app.SaveDraft();
    Pump();
    RODAK_CHECK(f.app.editor_ == nullptr);
    const auto saved = f.Saved();
    RODAK_CHECK_EQ(saved.size(), 1u);
    RODAK_CHECK_EQ(saved[0].name, std::string("测试主机"));
    RODAK_CHECK_EQ(saved[0].mac_address, std::string("00:1A:2B:3C:4D:5E"));
    RODAK_CHECK_EQ(saved[0].port, 7);
    RODAK_CHECK(OnScreen("1 / 8"));
}

RODAK_TEST("invalid input is shown on the input page and not accepted") {
    Fixture f;
    f.Create();
    Click(f.app.add_button_);
    Click(f.FormRow(1));
    f.Type("01:00:5E:00:00:01");  // multicast, not a valid target
    RODAK_CHECK(f.app.input_ != nullptr);
    RODAK_CHECK_EQ(Text(f.app.input_hint_), std::string("MAC 地址无效，需 12 位十六进制"));
    Screenshot("wol-input-error.ppm");
    f.Type("AA:BB:CC:DD:EE:F");
    RODAK_CHECK(f.app.input_ != nullptr);
    f.Type("AA:BB:CC:DD:EE:FF");
    RODAK_CHECK(f.app.input_ == nullptr);

    Click(f.FormRow(3));
    f.Type("0");
    RODAK_CHECK_EQ(Text(f.app.input_hint_), std::string("端口范围 1-65535"));
    f.app.CloseInput();
    Pump();

    // Saving with no name keeps the form open and says what is missing.
    f.app.SaveDraft();
    Pump();
    RODAK_CHECK(f.app.editor_ != nullptr);
    RODAK_CHECK(OnScreen("请填写设备名称"));
    RODAK_CHECK(f.Saved().empty());
}

RODAK_TEST("duplicate MAC addresses are refused") {
    Fixture f;
    f.Store({kDesk});
    f.Create();
    Click(f.app.add_button_);
    Click(f.FormRow(0));
    f.Type("副本");
    Click(f.FormRow(1));
    f.Type("aa:bb:cc:dd:ee:ff");
    f.app.SaveDraft();
    Pump();
    RODAK_CHECK(OnScreen("这个 MAC 地址已经添加过"));
    RODAK_CHECK_EQ(f.Saved().size(), 1u);
}

RODAK_TEST("editing shows the device; delete asks first; back walks input, form, list, home") {
    Fixture f;
    f.Store({kDesk, kNas});
    f.Create();
    Click(f.CardInfo(0));
    RODAK_CHECK_EQ(Text(f.app.editor_title_), std::string("编辑设备"));
    RODAK_CHECK_EQ(Text(f.app.editor_values_[0]), std::string("书房台式机"));
    RODAK_CHECK_EQ(Text(f.app.editor_values_[1]), std::string("AA:BB:CC:DD:EE:FF"));
    Screenshot("wol-form-edit.ppm");

    Click(f.FormRow(0));
    RODAK_CHECK(f.app.OnBackRequested());
    Pump();
    RODAK_CHECK(f.app.input_ == nullptr);
    RODAK_CHECK(f.app.editor_ != nullptr);

    Click(f.app.editor_delete_);
    RODAK_CHECK(OnScreen("删除这台设备？"));
    Screenshot("wol-delete.ppm");
    RODAK_CHECK(f.app.OnBackRequested());
    Pump();
    RODAK_CHECK_FALSE(OnScreen("删除这台设备？"));
    RODAK_CHECK_EQ(f.Saved().size(), 2u);

    Click(f.app.editor_delete_);
    f.app.ConfirmDialog();
    Pump();
    RODAK_CHECK(f.app.editor_ == nullptr);
    RODAK_CHECK_EQ(f.Saved().size(), 1u);
    RODAK_CHECK_EQ(f.Saved()[0].name, std::string("客厅 NAS"));
    RODAK_CHECK(f.app.OnBackRequested());
    Pump();
    RODAK_CHECK_EQ(f.navigation.home_count, 1);
}

RODAK_TEST("a corrupt list explains itself and resets only after confirmation") {
    Fixture f;
    rodakos_home_ui_test::SetCommittedSetting("wol", "devices", "{not json");
    f.Create();
    RODAK_CHECK(OnScreen("设备列表数据损坏"));
    RODAK_CHECK(lv_obj_has_state(f.app.add_button_, LV_STATE_DISABLED));
    Screenshot("wol-corrupt.ppm");
    auto* reset = lv_obj_get_child(f.app.status_box_, -1);
    Click(reset);
    RODAK_CHECK(OnScreen("重置设备列表？"));
    RODAK_CHECK_EQ(rodakos_home_ui_test::GetCommittedSetting("wol", "devices"), std::string("{not json"));
    f.app.ConfirmDialog();
    Pump();
    RODAK_CHECK(OnScreen("还没有设备"));
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.add_button_, LV_STATE_DISABLED));
}

RODAK_TEST("a newer-schema list is read-only: no add, no edit, strip says so") {
    Fixture f;
    rodakos_home_ui_test::SetCommittedSetting(
        "wol", "devices",
        "{\"version\":99,\"devices\":[{\"name\":\"x\",\"mac\":\"AA:BB:CC:DD:EE:FF\",\"broadcast\":\"255.255.255.255\",\"port\":9}]}");
    f.Create();
    RODAK_CHECK(lv_obj_has_state(f.app.add_button_, LV_STATE_DISABLED));
    RODAK_CHECK(OnScreen("列表只读"));
    Click(f.app.add_button_);
    RODAK_CHECK(f.app.editor_ == nullptr);
}

RODAK_TEST("a failed save keeps the form and disables further writes") {
    Fixture f;
    f.Store({kDesk});
    f.Create();
    Click(f.CardInfo(0));
    Click(f.FormRow(0));
    f.Type("新名字");
    rodakos_home_ui_test::SettingsState().commit_result = false;
    f.app.SaveDraft();
    Pump();
    RODAK_CHECK(OnScreen("保存失败"));
    RODAK_CHECK(f.app.editor_ != nullptr);
    RODAK_CHECK_EQ(f.Saved()[0].name, std::string("书房台式机"));
}

RODAK_TEST("theme change rebuilds the page and keeps an open editor's draft") {
    Fixture f("light");
    f.Store({kDesk});
    f.Create();
    Screenshot("wol-light.ppm");
    Click(f.CardInfo(0));
    Click(f.FormRow(0));
    f.Type("改过的名字");
    rodakos_theme_init_from_name("blue");
    f.ui.SyncThemeName("blue");
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    Pump();
    RODAK_CHECK(f.app.editor_ != nullptr);
    RODAK_CHECK_EQ(Text(f.app.editor_values_[0]), std::string("改过的名字"));
}

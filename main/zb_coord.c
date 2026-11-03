#include "zb_coord.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "status_led.h"
#include "wav_player.h"
#include "nwk/esp_zigbee_nwk.h"
#include "zboss_api.h"

#define COORD_ENDPOINT 1
#define BUTTON_ENDPOINT 1
#define PAIRING_SECONDS 180
#define MAX_CHILDREN 10
#define NVS_NS "kf"
#define NVS_KEY_BUTTON "button_ieee"
#define NVS_KEY_SHORT "button_short"
#define OUTSIDE_WAV "desi_outside.wav"
#define PAIRING_MODE_WAV "pairing_mode.wav"
#define PAIRING_OK_WAV "pairing_successful.wav"
#define PAIRING_FAIL_WAV "pairing_unsuccessful.wav"

static const char *TAG = "zb_coord";

static esp_zb_ieee_addr_t s_button_ieee;
static volatile bool s_have_button;
static volatile bool s_pairing;
static volatile bool s_stack_ready;
static uint16_t s_button_short = 0xFFFF;

static void ieee_to_str(const uint8_t *a, char *out)
{
    snprintf(out, 24, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", a[7], a[6], a[5], a[4], a[3], a[2], a[1], a[0]);
}

static void load_button(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_button_ieee);
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        s_have_button = nvs_get_blob(h, NVS_KEY_BUTTON, s_button_ieee, &len) == ESP_OK && len == sizeof(s_button_ieee);
        if (s_have_button) {
            nvs_get_u16(h, NVS_KEY_SHORT, &s_button_short);
        }
        nvs_close(h);
    }
    if (s_have_button) {
        char s[24];
        ieee_to_str(s_button_ieee, s);
        ESP_LOGI(TAG, "paired button: %s", s);
    }
}

static void save_button(const esp_zb_ieee_addr_t ieee)
{
    memcpy(s_button_ieee, ieee, sizeof(s_button_ieee));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY_BUTTON, s_button_ieee, sizeof(s_button_ieee));
        nvs_commit(h);
        nvs_close(h);
    }
    s_have_button = true;
}

static void save_button_short(uint16_t short_addr)
{
    s_button_short = short_addr;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u16(h, NVS_KEY_SHORT, short_addr);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void erase_button(void)
{
    s_button_short = 0xFFFF;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_BUTTON);
        nvs_erase_key(h, NVS_KEY_SHORT);
        nvs_commit(h);
        nvs_close(h);
    }
    s_have_button = false;
}

static void update_led(void)
{
    if (s_pairing) {
        status_led_set(KF_STATE_PAIRING);
    } else {
        status_led_set(s_have_button ? KF_STATE_PAIRED : KF_STATE_NOT_PAIRED);
    }
}

static void bind_cb(esp_zb_zdp_status_t status, void *ctx)
{
    ESP_LOGI(TAG, "bind on/off to coordinator: status %d", status);
}

// Sleepy end devices only report to a bound destination, so bind them to us.
static void send_bind(uint16_t short_addr)
{
    esp_zb_zdo_bind_req_param_t req = {0};
    memcpy(req.src_address, s_button_ieee, sizeof(req.src_address));
    req.src_endp = BUTTON_ENDPOINT;
    req.cluster_id = ESP_ZB_ZCL_CLUSTER_ID_ON_OFF;
    req.dst_addr_mode = ESP_ZB_ZDO_BIND_DST_ADDR_MODE_64_BIT_EXTENDED;
    esp_zb_get_long_address(req.dst_address_u.addr_long);
    req.dst_endp = COORD_ENDPOINT;
    req.req_dst_addr = short_addr;
    esp_zb_zdo_device_bind_req(&req, bind_cb, NULL);
}

static void open_pairing_locked(void)
{
    s_pairing = true;
    update_led();
    wav_player_enqueue(PAIRING_MODE_WAV);
    esp_zb_bdb_open_network(PAIRING_SECONDS);
}

void zb_coord_start_pairing(void)
{
    if (!s_stack_ready) {
        return;
    }
    ESP_LOGI(TAG, "pairing mode for %d s", PAIRING_SECONDS);
    if (esp_zb_lock_acquire(pdMS_TO_TICKS(2000))) {
        open_pairing_locked();
        esp_zb_lock_release();
    }
}

void zb_coord_unpair(void)
{
    ESP_LOGW(TAG, "unpairing: erasing Zigbee state");
    erase_button();
    status_led_set(KF_STATE_NOT_PAIRED);
    if (esp_zb_lock_acquire(pdMS_TO_TICKS(2000))) {
        esp_zb_factory_reset();
        esp_zb_lock_release();
    }
}

// Observes every incoming ZCL command; returns false so the stack still handles it.
static bool raw_command_cb(uint8_t bufid)
{
    zb_zcl_parsed_hdr_t *hdr = ZB_BUF_GET_PARAM(bufid, zb_zcl_parsed_hdr_t);
    if (hdr->cluster_id != ESP_ZB_ZCL_CLUSTER_ID_ON_OFF || hdr->is_common_command) {
        return false;
    }
    uint16_t src = hdr->addr_data.common_data.source.u.short_addr;
    ESP_LOGI(TAG, "on/off cmd 0x%02x from 0x%04x", hdr->cmd_id, src);
    if (!s_have_button) {
        return false;
    }
    // The stack's IEEE->short map is not guaranteed to survive a reboot, so fall back to the saved short address.
    uint16_t known = esp_zb_address_short_by_ieee(s_button_ieee);
    ESP_LOGI(TAG, "button short: stack 0x%04x saved 0x%04x", known, s_button_short);
    if (known != src && !(known == 0xFFFF && s_button_short == src)) {
        return false;
    }
    if (s_button_short != src) {
        save_button_short(src);
    }
    switch (hdr->cmd_id) {
    case ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID:
    case ESP_ZB_ZCL_CMD_ON_OFF_ON_ID:
    case ESP_ZB_ZCL_CMD_ON_OFF_TOGGLE_ID:
        wav_player_play(OUTSIDE_WAV);
        break;
    default:
        break;
    }
    return false;
}

static void start_commissioning_cb(uint8_t mode_mask)
{
    esp_zb_bdb_start_top_level_commissioning(mode_mask);
}

static void dump_neighbors(void)
{
    esp_zb_nwk_info_iterator_t it = ESP_ZB_NWK_INFO_ITERATOR_INIT;
    esp_zb_nwk_neighbor_info_t n;
    int count = 0;
    ESP_LOGI(TAG, "nwk frame counter %lu", (unsigned long)esp_zb_nwk_get_frame_counter());
    while (esp_zb_nwk_get_next_neighbor(&it, &n) == ESP_OK) {
        char s[24];
        ieee_to_str(n.ieee_addr, s);
        ESP_LOGI(TAG, "neighbor %s short 0x%04x rel %d type %d rxidle %d lqi %d timeout %lus", s, n.short_addr, n.relationship,
                 n.device_type, n.rx_on_when_idle, n.lqi, (unsigned long)n.device_timeout);
        count++;
    }
    ESP_LOGI(TAG, "%d neighbors", count);
}

static void on_network_ready(void)
{
    dump_neighbors();
    s_stack_ready = true;
    ESP_LOGI(TAG, "PAN 0x%04x channel %d short 0x%04x, button %s", esp_zb_get_pan_id(), esp_zb_get_current_channel(),
             esp_zb_get_short_address(), s_have_button ? "stored" : "none");
    if (!s_have_button) {
        open_pairing_locked();
    } else {
        update_led();
    }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig = *p_sg_p;

    switch (sig) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "stack start failed: %s", esp_err_to_name(err));
        } else if (esp_zb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "forming network");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
        } else {
            ESP_LOGI(TAG, "network restored");
            on_network_ready();
        }
        break;
    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "network formed: PAN 0x%04x channel %d", esp_zb_get_pan_id(), esp_zb_get_current_channel());
            if (s_have_button) {
                ESP_LOGW(TAG, "new network formed; stored button belongs to a lost network, clearing");
                erase_button();
            }
            on_network_ready();
        } else {
            ESP_LOGW(TAG, "formation failed (%s), retrying", esp_err_to_name(err));
            esp_zb_scheduler_alarm((esp_zb_callback_t)start_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        esp_zb_zdo_signal_device_annce_params_t *p = esp_zb_app_signal_get_params(p_sg_p);
        char s[24];
        ieee_to_str(p->ieee_addr, s);
        ESP_LOGI(TAG, "device announce: %s short 0x%04x", s, p->device_short_addr);
        if (s_pairing) {
            save_button(p->ieee_addr);
            save_button_short(p->device_short_addr);
            s_pairing = false;
            esp_zb_bdb_close_network();
            update_led();
            wav_player_enqueue(PAIRING_OK_WAV);
            ESP_LOGI(TAG, "paired with %s", s);
        }
        if (s_have_button && !memcmp(p->ieee_addr, s_button_ieee, sizeof(s_button_ieee))) {
            if (s_button_short != p->device_short_addr) {
                save_button_short(p->device_short_addr);
            }
            send_bind(p->device_short_addr);
        }
        break;
    }
    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        uint8_t *dur = esp_zb_app_signal_get_params(p_sg_p);
        if (err == ESP_OK && dur && *dur == 0 && s_pairing) {
            ESP_LOGW(TAG, "pairing window closed without a device");
            s_pairing = false;
            update_led();
            wav_player_enqueue(PAIRING_FAIL_WAV);
        }
        break;
    }
    default:
        ESP_LOGI(TAG, "signal %s (0x%x) status %s", esp_zb_zdo_signal_to_string(sig), sig, esp_err_to_name(err));
        break;
    }
}

static void zb_task(void *arg)
{
    esp_zb_cfg_t cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg.max_children = MAX_CHILDREN,
    };
    esp_zb_init(&cfg);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(clusters, esp_zb_basic_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(clusters, esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint = COORD_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_gateway_ep(ep_list, clusters, ep_cfg);
    esp_zb_device_register(ep_list);
    esp_zb_raw_command_handler_register(raw_command_cb);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

esp_err_t zb_coord_start(void)
{
    esp_zb_platform_config_t pcfg = {
        .radio_config = {.radio_mode = ZB_RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE},
    };
    ESP_RETURN_ON_ERROR(esp_zb_platform_config(&pcfg), TAG, "platform config");
    load_button();
    update_led();
    xTaskCreate(zb_task, "zigbee", 8192, NULL, 5, NULL);
    return ESP_OK;
}

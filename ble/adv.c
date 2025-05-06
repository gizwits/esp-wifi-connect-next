/* Configures extended advertsing params*/
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "nimble/ble.h"
#include "esp_peripheral.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "adv.h"
#include "ble.h"
#include "esp_log.h"

static const char *tag = "nimble_adv";
struct ble_instance_cb_register ble_instance_cb[BLE_ADV_INSTANCES];
static uint8_t ext_adv_pattern_1[64] = {0};
static size_t ext_adv_pattern_1_len = 0;
static void ble_multi_perform_gatt_proc(ble_addr_t addr);
static void ble_multi_adv_conf_set_addr(uint16_t instance, struct ble_gap_ext_adv_params *params,
                            uint8_t *pattern, int size_pattern, int duration);
static int ble_adv_set_addr(uint16_t instance);
static void ble_multi_adv_print_conn_desc(struct ble_gap_conn_desc *desc);

static int
ble_connectable_ext_cb(uint16_t instance)
{
    ESP_LOGI(tag, "In %s, instance = %d", __func__, instance);
    return 0;
}

static int
ble_scannable_legacy_ext_cb(uint16_t instance)
{
    ESP_LOGI(tag, "In %s, instance = %d", __func__, instance);
    return 0;
}


void ble_set_network_status(bool configured)
{
    if (configured) {
        ext_adv_pattern_1[VERSION_TYPE_INDEX + 1] |= NETWORK_CONFIG;
    } else {
        ext_adv_pattern_1[VERSION_TYPE_INDEX + 1] &= ~NETWORK_CONFIG;
    }
}
esp_err_t ble_gen_adv_data(const char *device_name, uint32_t pk, const uint8_t *mac)
{
    ESP_LOGI(tag, "Setting BLE advertisement data...");
    
    if (!device_name || !mac) {
        ESP_LOGE(tag, "Invalid parameters: device_name or mac is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    size_t name_len = strlen(device_name);
    if (name_len > 29) {
        ESP_LOGE(tag, "Device name too long: %d bytes (max 29)", name_len);
        return ESP_ERR_INVALID_ARG;
    }

    // ESP_LOGI(tag, "Configuring advertisement data with name: %s, PK: 0x%08X, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
    //          device_name, pk, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    size_t index = 0;

    // 1. Flags
    ext_adv_pattern_1[index++] = 0x02;
    ext_adv_pattern_1[index++] = 0x01;
    ext_adv_pattern_1[index++] = 0x06;
    ESP_LOGD(tag, "Added flags data at index 0");

    // 3. Service UUID
    ext_adv_pattern_1[index++] = 0x03;
    ext_adv_pattern_1[index++] = 0x03;
    ext_adv_pattern_1[index++] = 0xD0;
    ext_adv_pattern_1[index++] = 0xAB;
    ESP_LOGD(tag, "Added second service UUID (0xD0AB) at index 7");

    // 4. Device Name
    ext_adv_pattern_1[index++] = name_len + 1;
    ext_adv_pattern_1[index++] = 0x09;
    memcpy(&ext_adv_pattern_1[index], device_name, name_len);
    index += name_len;

    // 填充到 31
    for (int i = index; i < 31; i++) {
        ext_adv_pattern_1[i] = 0x00;
    }
    index = 31;

    // 自定义部分
    ext_adv_pattern_1[index++] = 0x0F;
    ext_adv_pattern_1[index++] = 0xFF;
    ext_adv_pattern_1[index++] = 0x3D;
    ext_adv_pattern_1[index++] = 0x00;
    ESP_LOGD(tag, "Added manufacturer specific data header at index %d", index - 4);
    
    ext_adv_pattern_1[index++] = VERSION_TYPE;
    uint8_t function_mask = BLE_VERSION_5_0 | SUPPORT_OTA | NO_SECURITY_AUTH | ONE_DEVICE_SECRET;
    ext_adv_pattern_1[index++] = function_mask;
    ESP_LOGD(tag, "Version: 0x%02X, Function Mask: 0x%02X", VERSION_TYPE, function_mask);

    // PK
    ext_adv_pattern_1[index++] = (pk >> 24) & 0xFF;
    ext_adv_pattern_1[index++] = (pk >> 16) & 0xFF;
    ext_adv_pattern_1[index++] = (pk >> 8) & 0xFF;
    ext_adv_pattern_1[index++] = pk & 0xFF;
    ESP_LOGD(tag, "Added PK at index %d", index - 4);

    // MAC
    memcpy(&ext_adv_pattern_1[index], mac, 6);
    index += 6;
    ESP_LOGD(tag, "Added MAC address at index %d", index - 6);
    
    ESP_LOGD(tag, "Added device name at index 11, length: %d", name_len);
    ext_adv_pattern_1_len = index;

    return ESP_OK;
}

void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    /* Check connection handle */
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(tag, "subscribe event; conn_handle=%d attr_handle=%d",
                event->subscribe.conn_handle, event->subscribe.attr_handle);
    } else {
        ESP_LOGI(tag, "subscribe by nimble stack; attr_handle=%d",
                event->subscribe.attr_handle);
    }

    /* Check attribute handle */
    if (event->subscribe.attr_handle == get_notify_chr_val_handle()) {
        /* Update heart rate subscription status */
        ble_set_notify_handle(event->subscribe.attr_handle);

    }
}

static int
ble_multi_adv_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        MODLOG_DFLT(INFO, "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            ble_set_conn_handle(event->connect.conn_handle);
            ble_multi_adv_print_conn_desc(&desc);
            ble_multi_perform_gatt_proc(desc.our_id_addr);
        }
        MODLOG_DFLT(INFO, "\n");
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        ble_multi_adv_print_conn_desc(&event->disconnect.conn);
        MODLOG_DFLT(INFO, "\n");
        ble_set_conn_handle(BLE_HS_CONN_HANDLE_NONE);
        ble_multi_advertise(ble_instance_cb[event->adv_complete.instance].addr);

        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        MODLOG_DFLT(INFO, "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        ble_multi_adv_print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        MODLOG_DFLT(INFO, "advertise complete; reason=%d",
                    event->adv_complete.reason);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        MODLOG_DFLT(INFO, "encryption change event; status=%d ",
                    event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        ble_multi_adv_print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        MODLOG_DFLT(INFO, "notify_tx event; conn_handle=%d attr_handle=%d "
                    "status=%d is_indication=%d",
                    event->notify_tx.conn_handle,
                    event->notify_tx.attr_handle,
                    event->notify_tx.status,
                    event->notify_tx.indication);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        MODLOG_DFLT(INFO, "subscribe event; conn_handle=%d attr_handle=%d "
                    "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.reason,
                    event->subscribe.prev_notify,
                    event->subscribe.cur_notify,
                    event->subscribe.prev_indicate,
                    event->subscribe.cur_indicate);
        /* GATT subscribe event callback */
        gatt_svr_subscribe_cb(event);
        return 0;

    case BLE_GAP_EVENT_MTU:
        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(tag, "PASSKEY_ACTION_EVENT started");
        struct ble_sm_io pkey = {0};
        int key = 0;

        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = 123456; // This is the passkey to be entered on peer
            ESP_LOGI(tag, "Enter passkey %" PRIu32 "on the peer side", pkey.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            
        } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
            static uint8_t tem_oob[16] = {0};
            pkey.action = event->passkey.params.action;
            for (int i = 0; i < 16; i++) {
                pkey.oob[i] = tem_oob[i];
            }
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGI(tag, "ble_sm_inject_io result: %d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            
        }
        return 0;

    case BLE_GAP_EVENT_AUTHORIZE:
        MODLOG_DFLT(INFO, "authorize event: conn_handle=%d attr_handle=%d is_read=%d",
                    event->authorize.conn_handle,
                    event->authorize.attr_handle,
                    event->authorize.is_read);

        /* The default behaviour for the event is to reject authorize request */
        event->authorize.out_response = BLE_GAP_AUTHORIZE_REJECT;
        return 0;
    }
    return 0;
}



static void
ble_multi_perform_gatt_proc(ble_addr_t addr)
{
    /* GATT procedures like notify, indicate can be performed now */
    for (int i = 0; i < BLE_ADV_INSTANCES; i++) {
        if (memcmp(&addr, &ble_instance_cb[i].addr, sizeof(addr)) == 0) {
            if (ble_instance_cb[i].cb) {
                ble_instance_cb[i].cb(i);
            }
        }
    }
    return;
}

void
start_connectable_ext(void)
{
    uint8_t instance = 0;
    struct ble_gap_ext_adv_params params;
    int size_pattern = ext_adv_pattern_1_len / sizeof(ext_adv_pattern_1[0]);

    memset (&params, 0, sizeof(params));

    params.connectable = 1;
    params.scannable = 1;
    params.legacy_pdu = 1;
    params.own_addr_type = BLE_OWN_ADDR_PUBLIC;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_2M;
    params.sid = 0;
    params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MIN;

    ble_multi_adv_conf_set_addr(instance, &params, ext_adv_pattern_1,
                                size_pattern, 0);
    ble_instance_cb[instance].cb = &ble_connectable_ext_cb;

}


void
ble_multi_advertise(ble_addr_t addr)
{
    for (int i = 0; i < BLE_ADV_INSTANCES; i++) {
        if (memcmp(&addr, &ble_instance_cb[i].addr, sizeof(addr)) == 0) {
            switch(i) {
                case 0:
                    start_connectable_ext();
                    break;
                default:
                    ESP_LOGI(tag, "Instance not found");
            }
        }
    }
}
static void
ble_multi_adv_conf_set_addr(uint16_t instance, struct ble_gap_ext_adv_params *params,
                            uint8_t *pattern, int size_pattern, int duration)
{
    int rc;
    struct os_mbuf *data;

    if (ble_gap_ext_adv_active(instance)) {
        ESP_LOGI(tag, "Instance already advertising");
        return;
    }

    rc = ble_gap_ext_adv_configure(instance, params, NULL,
                                   ble_multi_adv_gap_event, NULL);

    if (rc != 0) {
        ESP_LOGE(tag, "ble_gap_ext_adv_configure failed: %d", rc);
        return;
    }

    rc = ble_adv_set_addr(instance);
    if (rc != 0) {
        ESP_LOGE(tag, "ble_adv_set_addr failed: %d", rc);
        return;
    }

    /* get mbuf for adv data */
    data = os_msys_get_pkthdr(size_pattern, 0);
    if (data == NULL) {
        ESP_LOGE(tag, "os_msys_get_pkthdr failed");
        return;
    }

    /* fill mbuf with adv data */
    rc = os_mbuf_append(data, pattern, 31);
    if (rc != 0) {
        ESP_LOGE(tag, "os_mbuf_append failed: %d", rc);
        return;
    }

    rc = ble_gap_ext_adv_set_data(instance, data);
    if (rc != 0) {
        ESP_LOGE(tag, "ble_gap_ext_adv_set_data failed: %d", rc);
        return;
    }

    /* get mbuf for scan rsp data */
    data = os_msys_get_pkthdr(31, 0);
    if (data == NULL) {
        ESP_LOGE(tag, "os_msys_get_pkthdr failed");
        return;
    }

    /* fill mbuf with scan rsp data */
    rc = os_mbuf_append(data, pattern + 31, 31);
    if (rc != 0) {
        ESP_LOGE(tag, "os_mbuf_append failed: %d", rc);
        return;
    }

    rc = ble_gap_ext_adv_rsp_set_data(instance, data);
    if (rc != 0) {
        ESP_LOGE(tag, "ble_gap_ext_adv_rsp_set_data failed: %d", rc);
        return;
    }

    /* start advertising */
    rc = ble_gap_ext_adv_start(instance, duration, 0);
    if (rc != 0) {
        ESP_LOGE(tag, "ble_gap_ext_adv_start failed: %d", rc);
        return;
    }

    ESP_LOGI(tag, "Instance %d started", instance);
}



static int ble_adv_set_addr(uint16_t instance)
{
    ble_addr_t addr;
    int rc;

    /* generate new non-resolvable private address */
    rc = ble_hs_id_gen_rnd(1, &addr);
    if (rc != 0) {
        return rc;
    }

    /* Set generated address */
    rc = ble_gap_ext_adv_set_addr(instance, &addr);
    if (rc != 0) {
        return rc;
    }
    memcpy(&ble_instance_cb[instance].addr, &addr, sizeof(addr));
    return 0;
}


static void
ble_multi_adv_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    MODLOG_DFLT(INFO, "handle=%d our_ota_addr_type=%d our_ota_addr=",
                desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);
    MODLOG_DFLT(INFO, " our_id_addr_type=%d our_id_addr=",
                desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);
    MODLOG_DFLT(INFO, " peer_ota_addr_type=%d peer_ota_addr=",
                desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);
    MODLOG_DFLT(INFO, " peer_id_addr_type=%d peer_id_addr=",
                desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);
    MODLOG_DFLT(INFO, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

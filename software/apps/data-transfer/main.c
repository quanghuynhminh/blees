/*
 * Advertises Sensor Data
 */

// Standard Libraries
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Nordic Libraries
#include "nordic_common.h"
#include "softdevice_handler.h"
#include "nrf.h"
#include "nrf_sdm.h"
#include "ble.h"
#include "ble_db_discovery.h"
#include "app_util.h"
#include "app_error.h"
#include "ble_advdata_parser.h"
#include "ble_conn_params.h"
#include "ble_hci.h"
#include "nrf_gpio.h"
#include "pstorage.h"
#include "app_trace.h"
#include "ble_hrs_c.h"
#include "ble_bas_c.h"
#include "app_util.h"
#include "app_timer.h"

// Platform, Peripherals, Devices, Services
#include "blees.h"
#include "led.h"


/*******************************************************************************
 *   DEFINES
 ******************************************************************************/
#include "ble_config.h"
#include "nrf_drv_config.h"

#define STARTUP_DELAY   APP_TIMER_TICKS(300, APP_TIMER_PRESCALER)
#define UPDATE_RATE     APP_TIMER_TICKS(1000, APP_TIMER_PRESCALER)

// Maximum size is 17 characters, counting URLEND if used
//#define PHYSWEB_URL     "goo.gl/XMRl3M"
#define PHYSWEB_URL     "umich.edu"

#define APP_BEACON_INFO_LENGTH 16


/*******************************************************************************
 *   STATIC AND GLOBAL VARIABLES
 ******************************************************************************/

uint8_t MAC_ADDR[6] = {0x00, 0x00, 0x30, 0xe5, 0x98, 0xc0};

static ble_app_t            app;
static ble_gap_adv_params_t m_adv_params;
static ble_advdata_t        advdata;

// Security requirements for this application.
static ble_gap_sec_params_t m_sec_params = {
    SEC_PARAM_BOND,
    SEC_PARAM_MITM,
    SEC_PARAM_IO_CAPABILITIES,
    SEC_PARAM_OOB,
    SEC_PARAM_MIN_KEY_SIZE,
    SEC_PARAM_MAX_KEY_SIZE,
};

static app_timer_id_t startup_timer;
static app_timer_id_t sample_timer;

static bool PHYSWEB_MODE = false;

static struct {
    float temp;
    float humidity;
    float light;
    float pressure;
    float acceleration;
} m_sensor_info = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

static uint8_t m_beacon_info[APP_BEACON_INFO_LENGTH];

static data_transfer_t data_transfer;


/*******************************************************************************
 *   FUNCTION PROTOTYPES
 ******************************************************************************/
static void advertising_start(void);
static void advertising_stop(void);
static void adv_sensors(void);
static void adv_physweb(void);


/*******************************************************************************
 *   HANDLERS AND CALLBACKS
 ******************************************************************************/

/**@brief Function for error handling, which is called when an error has occurred.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of error.
 *
 * @param[in] error_code  Error code supplied to the handler.
 * @param[in] line_num    Line number where the handler is called.
 * @param[in] p_file_name Pointer to the file name.
 */
void app_error_handler(uint32_t error_code, uint32_t line_num, const uint8_t * p_file_name) {
    // APPL_LOG("[APPL]: ASSERT: %s, %d, error 0x%08x\r\n", p_file_name, line_num, error_code);
    // nrf_gpio_pin_set(ASSERT_LED_PIN_NO);

    // This call can be used for debug purposes during development of an application.
    // @note CAUTION: Activating this code will write the stack to flash on an error.
    //                This function should NOT be used in a final product.
    //                It is intended STRICTLY for development/debugging purposes.
    //                The flash write will happen EVEN if the radio is active, thus interrupting
    //                any communication.
    //                Use with care. Un-comment the line below to use.
    // ble_debug_assert_handler(error_code, line_num, p_file_name);

    // On assert, the system can only recover with a reset.
    //NVIC_SystemReset();

    led_on(SQUALL_LED_PIN);
    while(1);
}

/**@brief Function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num     Line number of the failing ASSERT call.
 * @param[in] p_file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name) {
    app_error_handler(0xDEADBEEF, line_num, p_file_name);
}

// service error callback
static void service_error_handler(uint32_t nrf_error) {
    APP_ERROR_HANDLER(nrf_error);
}

// connection parameters event handler callback
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt) {
    uint32_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
        err_code = sd_ble_gap_disconnect(app.conn_handle,
                                         BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}

// connection parameters error callback
static void conn_params_error_handler(uint32_t nrf_error) {
    APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for handling the Application's BLE Stack events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
static void on_ble_evt(ble_evt_t * p_ble_evt) {
    uint32_t                         err_code;

    switch (p_ble_evt->header.evt_id) {
        case BLE_GAP_EVT_CONNECTED:
            // continue advertising nonconnectably
            app.conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            m_adv_params.type = BLE_GAP_ADV_TYPE_ADV_NONCONN_IND;
            advertising_start();
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            app.conn_handle = BLE_CONN_HANDLE_INVALID;

            // advertise connectivity
            advertising_stop();
            m_adv_params.type   = BLE_GAP_ADV_TYPE_ADV_IND;
            advertising_start();
            break;

        case BLE_GATTS_EVT_WRITE:
            {
                ble_gatts_evt_write_t* write_data = &(p_ble_evt->evt.gatts_evt.params.write);
                if (write_data->context.char_uuid.uuid == test_char_uuid16) {
                    if (write_data->data[0] == 0x42) {
                        //led_on(BLEES_LED_PIN);

                        // enable higher connection interval. Only lasts for this connection
                        ble_gap_conn_params_t   gap_conn_params;
                        memset(&gap_conn_params, 0, sizeof(gap_conn_params));
                        gap_conn_params.min_conn_interval = 0x06; // 7.5 ms
                        gap_conn_params.max_conn_interval = MSEC_TO_UNITS(30, UNIT_1_25_MS);
                        gap_conn_params.slave_latency     = 0;
                        gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;
                        err_code = sd_ble_gap_conn_param_update(app.conn_handle, &gap_conn_params);
                        APP_ERROR_CHECK(err_code);
                    }
                }
            }
            break;

        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            err_code = sd_ble_gap_sec_params_reply(app.conn_handle,
                    BLE_GAP_SEC_STATUS_SUCCESS, &m_sec_params, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            err_code = sd_ble_gatts_sys_attr_set(app.conn_handle, NULL, 0, 0);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GAP_EVT_AUTH_STATUS:
            break;

        case BLE_GAP_EVT_SEC_INFO_REQUEST:
            // No keys found for this device.
            err_code = sd_ble_gap_sec_info_reply(app.conn_handle, NULL, NULL, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GAP_EVT_TIMEOUT:
            if (p_ble_evt->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_ADVERTISING) {
                err_code = sd_power_system_off();
                APP_ERROR_CHECK(err_code);
            }
            break;

        default:
            break;
    }
}

/**@brief Function for dispatching a BLE stack event to all modules with a BLE stack event handler.
 *
 * @details This function is called from the scheduler in the main loop after a BLE stack event has
 *  been received.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
    on_ble_evt(p_ble_evt);
    ble_conn_params_on_ble_evt(p_ble_evt);
}


/**@brief Function for dispatching a system event to interested modules.
 *
 * @details This function is called from the System event interrupt handler after a system
 *          event has been received.
 *
 * @param[in]   sys_evt   System stack event.
 */
static void sys_evt_dispatch(uint32_t sys_evt) {
}

// complete startup
static void startup_handler(void* p_context) {
    uint32_t err_code = app_timer_start(sample_timer, UPDATE_RATE, NULL);
    APP_ERROR_CHECK(err_code);
}

// timer callback
static void timer_handler (void* p_context) {
    //led_toggle(BLEES_LED_PIN);

    if (PHYSWEB_MODE) {
        PHYSWEB_MODE = false;
        adv_sensors();
    } else {
        //PHYSWEB_MODE = true;
        //adv_physweb();
    }
}


/*******************************************************************************
 *   INIT FUNCTIONS
 ******************************************************************************/

/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init (void) {
    uint32_t err_code;

    // Initialize the SoftDevice handler module.
    SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_RC_250_PPM_8000MS_CALIBRATION,
            false);

    // Enable BLE stack
    ble_enable_params_t ble_enable_params;
    memset(&ble_enable_params, 0, sizeof(ble_enable_params));
    ble_enable_params.gatts_enable_params.service_changed = IS_SRVC_CHANGED_CHARACT_PRESENT;
    err_code = sd_ble_enable(&ble_enable_params);
    APP_ERROR_CHECK(err_code);

    //Register with the SoftDevice handler module for BLE events.
    err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
    APP_ERROR_CHECK(err_code);

    // Register with the SoftDevice handler module for BLE events.
    err_code = softdevice_sys_evt_handler_set(sys_evt_dispatch);
    APP_ERROR_CHECK(err_code);

    // Set the MAC address of the device
    {
        ble_gap_addr_t gap_addr;

        // Get the current original address
        sd_ble_gap_address_get(&gap_addr);

        // Set the new BLE address with the Michigan OUI
        gap_addr.addr_type = BLE_GAP_ADDR_TYPE_PUBLIC;
        memcpy(gap_addr.addr+2, MAC_ADDR+2, sizeof(gap_addr.addr)-2);
        err_code = sd_ble_gap_address_set(BLE_GAP_ADDR_CYCLE_MODE_NONE,
                &gap_addr);
        APP_ERROR_CHECK(err_code);
    }
}

// gap name/appearance/connection parameters
static void gap_params_init (void) {
    uint32_t                err_code;
    ble_gap_conn_sec_mode_t sec_mode;
    ble_gap_conn_params_t   gap_conn_params;

    // Full strength signal
    sd_ble_gap_tx_power_set(4);

    // Let anyone connect and set the name given the platform
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
    err_code = sd_ble_gap_device_name_set(&sec_mode,
            (const uint8_t *)"BEES", strlen("BEES"));
            //(const uint8_t *)DEVICE_NAME, strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    // Not sure what this is useful for, but why not set it
    err_code = sd_ble_gap_appearance_set(BLE_APPEARANCE_GENERIC_COMPUTER);
    APP_ERROR_CHECK(err_code);

    // Specify parameters for a connection
    memset(&gap_conn_params, 0, sizeof(gap_conn_params));
    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}

// initialize advertising
static void advertising_init(void) {
    memset(&m_adv_params, 0, sizeof(m_adv_params));
    //m_adv_params.type               = BLE_GAP_ADV_TYPE_ADV_NONCONN_IND;
    m_adv_params.type               = BLE_GAP_ADV_TYPE_ADV_IND;
    m_adv_params.p_peer_addr        = NULL;
    m_adv_params.fp                 = BLE_GAP_ADV_FP_ANY;
    m_adv_params.interval           = APP_ADV_INTERVAL;
    m_adv_params.timeout            = APP_ADV_TIMEOUT_IN_SECONDS;
}

// Initialize connection parameters
static void conn_params_init(void) {
    uint32_t err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));
    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}

// init services
static void services_init (void) {
    uint32_t err_code;

    // set up service uuid
    ble_uuid_t data_transfer_uuid;
    data_transfer_uuid.uuid = data_transfer_srvc_uuid16;
    err_code = sd_ble_uuid_vs_add(&data_transfer_uuid128, &(data_transfer_uuid.type));
    APP_ERROR_CHECK(err_code);
    app.uuid_type = data_transfer_uuid.type;

    // add the custom service to the system
    err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,
            &data_transfer_uuid, &(app.service_handle));
    APP_ERROR_CHECK(err_code);

    // add test characteristic
    {
        ble_gatts_char_md_t char_md;
        ble_uuid_t          char_uuid;
        ble_gatts_attr_md_t attr_md;
        ble_gatts_attr_t    attr_char_value;
        uint8_t* char_name = (uint8_t*)"Test Characteristic";

        // initial value
        data_transfer.test_char = 0x23;
        //memset(data_transfer.test_char, 0xBA, 500);

        // characteristic settings
        memset(&char_md, 0, sizeof(char_md));
        char_md.char_props.read         = true;
        char_md.char_props.write        = true;
        char_md.p_char_user_desc        = char_name;
        char_md.char_user_desc_max_size = sizeof(char_name);
        char_md.char_user_desc_size     = sizeof(char_name);

        // characteristic uuid
        char_uuid.type = app.uuid_type;
        char_uuid.uuid = test_char_uuid16;

        // attribute metadata
        memset(&attr_md, 0, sizeof(attr_md));
        BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
        //BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.write_perm);
        BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
        attr_md.vloc    = BLE_GATTS_VLOC_USER;
        attr_md.rd_auth = 0;
        attr_md.wr_auth = 0;
        attr_md.vlen    = 1;

        // gatt attributes
        memset(&attr_char_value, 0, sizeof(attr_char_value));
        attr_char_value.p_uuid    = &char_uuid;
        attr_char_value.p_attr_md = &attr_md;
        attr_char_value.init_offs = 0;
        attr_char_value.init_len  = 1;
        attr_char_value.max_len   = 1;
        attr_char_value.p_value   = (uint8_t*)&data_transfer.test_char;
        //attr_char_value.init_len  = 500;
        //attr_char_value.max_len   = 500;
        //attr_char_value.p_value   = (uint8_t*)data_transfer.test_char;

        err_code = sd_ble_gatts_characteristic_add(app.service_handle,
                &char_md, &attr_char_value, &data_transfer.test_char_handles);
        APP_ERROR_CHECK(err_code);
    }
}

static void timers_init(void) {
    uint32_t err_code;

    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_MAX_TIMERS,
            APP_TIMER_OP_QUEUE_SIZE, false);

    err_code = app_timer_create(&startup_timer, APP_TIMER_MODE_SINGLE_SHOT,
            startup_handler);
    APP_ERROR_CHECK(err_code);

    err_code = app_timer_create(&sample_timer, APP_TIMER_MODE_REPEATED,
            timer_handler);
    APP_ERROR_CHECK(err_code);
}


/*******************************************************************************
 *   HELPER FUNCTIONS
 ******************************************************************************/

static void advertising_start(void) {
    uint32_t err_code = sd_ble_gap_adv_start(&m_adv_params);
    APP_ERROR_CHECK(err_code);
}

static void advertising_stop(void) {
    uint32_t err_code = sd_ble_gap_adv_stop();
    APP_ERROR_CHECK(err_code);
}

static void timers_start(void) {
    uint32_t err_code = app_timer_start(startup_timer, STARTUP_DELAY, NULL);
    APP_ERROR_CHECK(err_code);
}

/** @brief Function for the Power manager.
 */
static void power_manage(void) {
    uint32_t err_code = sd_app_evt_wait();
    APP_ERROR_CHECK(err_code);
}

static void adv_physweb(void) {
    uint32_t err_code;

    // Physical Web data
    uint8_t url_frame_length = 3 + strlen(PHYSWEB_URL); // Change to 4 if URLEND is applied
    uint8_t m_url_frame[url_frame_length];
    m_url_frame[0] = PHYSWEB_URL_TYPE;
    m_url_frame[1] = PHYSWEB_TX_POWER;
    m_url_frame[2] = PHYSWEB_URLSCHEME_HTTP;
    for (uint8_t i=0; i<strlen(PHYSWEB_URL); i++) {
        m_url_frame[i+3] = PHYSWEB_URL[i];
    }
    //m_url_frame[url_frame_length-1] = PHYSWEB_URLEND_COMSLASH; // Remember to change url_frame_length

    // Physical web service
    ble_advdata_service_data_t service_data;
    service_data.service_uuid   = PHYSWEB_SERVICE_ID;
    service_data.data.p_data    = m_url_frame;
    service_data.data.size      = url_frame_length;

    // Build and set advertising data
    memset(&advdata, 0, sizeof(advdata));
    advdata.flags                   = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    advdata.p_service_data_array    = &service_data;
    advdata.service_data_count      = 1;
    advdata.uuids_complete          = PHYSWEB_SERVICE_LIST;

    // Actually set advertisement data
    err_code = ble_advdata_set(&advdata, NULL);
    APP_ERROR_CHECK(err_code);
}

static void adv_sensors(void) {
    uint32_t err_code;
    uint8_t flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    ble_advdata_manuf_data_t manuf_specific_data;

    memcpy(&m_beacon_info[0],  &m_sensor_info.temp, 4);
    memcpy(&m_beacon_info[4],  &m_sensor_info.humidity, 4);
    memcpy(&m_beacon_info[8],  &m_sensor_info.light, 4);
    memcpy(&m_beacon_info[12], &m_sensor_info.pressure, 4);
    //memcpy(&m_beacon_info[16], &m_sensor_info.acceleration, 2);

    manuf_specific_data.company_identifier = APP_COMPANY_IDENTIFIER;
    manuf_specific_data.data.p_data        = (uint8_t *) m_beacon_info;
    manuf_specific_data.data.size          = APP_BEACON_INFO_LENGTH;

    // Build and set advertising data.
    memset(&advdata, 0, sizeof(advdata));

    advdata.name_type               = BLE_ADVDATA_FULL_NAME;
    advdata.flags                   = flags;
    advdata.p_manuf_specific_data   = &manuf_specific_data;

    err_code = ble_advdata_set(&advdata, NULL);
    APP_ERROR_CHECK(err_code);
}


/*******************************************************************************
 *   MAIN LOOP
 ******************************************************************************/

int main(void) {

    // Initialization
    led_init(SQUALL_LED_PIN);
    led_init(BLEES_LED_PIN);
    led_on(SQUALL_LED_PIN);
    led_on(BLEES_LED_PIN);
    timers_init(); // must start before conn_params

    // Setup BLE and services
    ble_stack_init();
    gap_params_init();
    advertising_init();
    services_init();
    conn_params_init();

    // Advertise data
    adv_physweb();
    advertising_start();

    // Initialization complete
    led_off(SQUALL_LED_PIN);
    led_off(BLEES_LED_PIN);
    timers_start();

    while (1) {
        power_manage();
    }
}


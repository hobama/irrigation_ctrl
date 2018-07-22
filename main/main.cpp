#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "user_config.h"
#include "mqtt_client.h"
#include "console.h"
#include "wifiEvents.h"
#include "globalComponents.h"
#include "irrigationController.h"
#include "irrigationPlanner.h"


// ********************************************************************
// global objects, vars and prototypes
// ********************************************************************
// TBD: encapsulate Wifi events/system/handling
// event group to signal WiFi events
EventGroupHandle_t wifiEvents;
const int wifiEventConnected = (1<<0);
const int wifiEventDisconnected = (1<<1);

FillSensorPacketizer fillSensorPacketizer;
FillSensorProtoHandler<FillSensorPacketizer> fillSensor(&fillSensorPacketizer);
PowerManager pwrMgr;
OutputController outputCtrl;
MqttManager mqttMgr;
IrrigationController irrigCtrl;
IrrigationPlanner irrigPlanner;

// ********************************************************************
// WiFi handling
// ********************************************************************
static esp_err_t wifiEventHandler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifiEvents, wifiEventConnected);
            xEventGroupClearBits(wifiEvents, wifiEventDisconnected);
            mqttMgr.start();
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            xEventGroupClearBits(wifiEvents, wifiEventConnected);
            xEventGroupSetBits(wifiEvents, wifiEventDisconnected);
            mqttMgr.stop();
            TimeSystem_SntpStop();
            /* This is a workaround as ESP32 WiFi libs don't currently auto-reassociate. */
            esp_wifi_connect();
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void initialiseWifi(void)
{
    ESP_LOGI(LOG_TAG_WIFI, "Initializing WiFi.");

    tcpip_adapter_init();

    wifiEvents = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(wifiEventHandler, NULL) );

    static wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );

    static wifi_config_t wifiConfig;
    memcpy(wifiConfig.sta.ssid, STA_SSID, sizeof(STA_SSID) / sizeof(uint8_t));
    memcpy(wifiConfig.sta.password, STA_PASS, sizeof(STA_PASS) / sizeof(uint8_t));

    ESP_LOGI(LOG_TAG_WIFI, "Setting WiFi configuration for SSID %s.", wifiConfig.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifiConfig) );
}

#if 0
// ********************************************************************
// MQTT callbacks + setup
// ********************************************************************
#define MQTT_OTA_UPGRADE_TOPIC_PRE              "whan/ota_upgrade/"
#define MQTT_OTA_UPGRADE_TOPIC_PRE_LEN          17
#define MQTT_OTA_UPGRADE_TOPIC_POST_REQ         "/req"
#define MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN     4

void mqtt_connected_cb(mqtt_client* client, mqtt_event_data_t* event_data)
{
    int i;
    char *topic;

    ESP_LOGI(LOG_TAG_MQTT_CB, "Connected.");

    // prepare subscription topic
    topic = (char*) malloc(MQTT_DIMMERS_TOPIC_PRE_LEN + MQTT_DIMMERS_TOPIC_CH_WILDCARD_LEN + MQTT_DIMMERS_TOPIC_POST_SET_LEN + 12 + 1);
    memcpy(topic, MQTT_DIMMERS_TOPIC_PRE, MQTT_DIMMERS_TOPIC_PRE_LEN);
    for(i=0; i<6; i++) {
        sprintf(&topic[MQTT_DIMMERS_TOPIC_PRE_LEN + i*2], "%02x", mac_addr[i]);
    }

    // subscribe to set topic
    memcpy(&topic[MQTT_DIMMERS_TOPIC_PRE_LEN + 12], MQTT_DIMMERS_TOPIC_CH_WILDCARD, MQTT_DIMMERS_TOPIC_CH_WILDCARD_LEN);
    memcpy(&topic[MQTT_DIMMERS_TOPIC_PRE_LEN + MQTT_DIMMERS_TOPIC_CH_WILDCARD_LEN + 12], MQTT_DIMMERS_TOPIC_POST_SET, MQTT_DIMMERS_TOPIC_POST_SET_LEN);
    topic[MQTT_DIMMERS_TOPIC_PRE_LEN + MQTT_DIMMERS_TOPIC_CH_WILDCARD_LEN + MQTT_DIMMERS_TOPIC_POST_SET_LEN + 12] = 0;
    mqtt_subscribe(client, topic, 2);

    free(topic);

    // subscribe to OTA topic
    topic = (char*) malloc(MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN + 12 + 1);
    memcpy(topic, MQTT_OTA_UPGRADE_TOPIC_PRE, MQTT_OTA_UPGRADE_TOPIC_PRE_LEN);
    for(i=0; i<6; i++) {
        sprintf(&topic[MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + i*2], "%02x", mac_addr[i]);
    }
    memcpy(&topic[MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + 12], MQTT_OTA_UPGRADE_TOPIC_POST_REQ, MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN);
    topic[MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN + 12] = 0;
    mqtt_subscribe(client, topic, 2);

    free(topic);
}

void mqtt_data_cb(mqtt_client* client, mqtt_event_data_t* event_data)
{
    unsigned int topic_len = event_data->topic_length;
    unsigned int data_len = event_data->data_length;

    char *topicBuf = (char*) malloc(topic_len+1);
    char *dataBuf = (char*) malloc(data_len+1);

    if((NULL != topicBuf) && (NULL != dataBuf))
    {
        memcpy(topicBuf, event_data->topic, topic_len);
        topicBuf[topic_len] = 0;

        memcpy(dataBuf, event_data->data, data_len);
        dataBuf[data_len] = 0;

        ESP_LOGD(LOG_TAG_MQTT_CB, "topic: %s, data: %s", topicBuf, dataBuf);

        // check prefix of topic, but expect the MAC address to match
        if(strncmp(topicBuf, MQTT_DIMMERS_TOPIC_PRE, MQTT_DIMMERS_TOPIC_PRE_LEN) == 0) {
            // check length (pre+set + at least two more chars (xxx/0/set) and set command at the end
            if( (topic_len >= (MQTT_DIMMERS_TOPIC_PRE_LEN+MQTT_DIMMERS_TOPIC_POST_SET_LEN + 12 + 2)) &&
            (topicBuf[MQTT_DIMMERS_TOPIC_PRE_LEN+12] == '/') &&
            (strncmp(topicBuf+topic_len-MQTT_DIMMERS_TOPIC_POST_SET_LEN, MQTT_DIMMERS_TOPIC_POST_SET, MQTT_DIMMERS_TOPIC_POST_SET_LEN) == 0) ) {
                // try to parse channel number
                char* parseEndPtr = &topicBuf[MQTT_DIMMERS_TOPIC_PRE_LEN+12+1];
                long channel = strtol(&topicBuf[MQTT_DIMMERS_TOPIC_PRE_LEN+12+1], &parseEndPtr, 10);
                //long channel = 0;

                // check if conversion was successfull / non-empty and is within range
                if((parseEndPtr != &topicBuf[MQTT_DIMMERS_TOPIC_PRE_LEN+12+1]) && (channel < DIMMER_CHANNELS)) {
                    // parse JSON request inspired by https://home-assistant.io/components/light.mqtt_json/
                    const nx_json* msg_root = nx_json_parse_utf8(dataBuf);
                    if(!msg_root) {
                        ESP_LOGI(LOG_TAG_MQTT_CB, "Received unparsable JSON data on set topic!");
                    } else {
                        const nx_json* msg_state = nx_json_get(msg_root, "state");
                        if(msg_state && (msg_state->type == NX_JSON_STRING)) {
                            #if 0
                            ESP_LOGI(LOG_TAG_MQTT_CB, "set state: %s", msg_state->text_value);
                            #endif
                            if((strcmp(msg_state->text_value, "ON") == 0) || (strcmp(msg_state->text_value, "on") == 0)) {
                                dimmer_set_switch(channel, true);
                            } else if((strcmp(msg_state->text_value, "OFF") == 0) || (strcmp(msg_state->text_value, "off") == 0)) {
                                dimmer_set_switch(channel, false);
                            }
                        }
                        
                        const nx_json* msg_brightness = nx_json_get(msg_root, "brightness");
                        if(msg_brightness && (msg_brightness->type == NX_JSON_INTEGER)) {
                            #if 0
                            ESP_LOGI(LOG_TAG_MQTT_CB, "set dim: %d", (uint32_t) msg_brightness->int_value);
                            #endif
                            uint32_t dim = msg_brightness->int_value;
                            if((dim >= 0) && (dim <= 255)) {
                                dimmer_set_dim(channel, dim);
                            }
                        }
                        
                        nx_json_free(msg_root);
                    }
                } else {
                    ESP_LOGI(LOG_TAG_MQTT_CB, "Received unparsable channel number!");
                }
            }
        }
        // check prefix of OTA topic, but expect the MAC address to match
        else if(strncmp(topicBuf, MQTT_OTA_UPGRADE_TOPIC_PRE, MQTT_OTA_UPGRADE_TOPIC_PRE_LEN) == 0) {
            // check length and req command
            if( (topic_len == (MQTT_OTA_UPGRADE_TOPIC_PRE_LEN + MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN + 12)) &&
            (strncmp(topicBuf+topic_len-MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN, MQTT_OTA_UPGRADE_TOPIC_POST_REQ, MQTT_OTA_UPGRADE_TOPIC_POST_REQ_LEN) == 0) ) {
                ESP_LOGI(LOG_TAG_MQTT_CB, "OTA topic matched!");
                
                if(!ota_is_in_progress()) {
                    ota_start(dataBuf);
                }
            } else {
                ESP_LOGI(LOG_TAG_MQTT_CB, "Another OTA update is still in progress!");
            }
        }
    } else {
        ESP_LOGE(LOG_TAG_MQTT_CB, "Couldn't allocate memory for topic/data buffer. Ignoring data.");
    }

    if(NULL != topicBuf) free(topicBuf);
    if(NULL != dataBuf) free(dataBuf);
}
#endif

esp_err_t initialiseMqttMgr(void)
{
    static uint8_t mac_addr[6];

    int ret = ESP_OK;

    int i;
    size_t mqtt_client_id_len;
    char *clientName;

    mqtt_client_id_len = strlen(MQTT_CLIENT_ID) + 12;
    clientName = (char*) malloc(mqtt_client_id_len+1);

    if(nullptr == clientName) ret = ESP_ERR_NO_MEM;
    if(mqtt_client_id_len > MQTT_MAX_CLIENT_LEN) ret = ESP_ERR_INVALID_ARG;

    if(ESP_OK == ret) {
        ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, mac_addr);
    }

    if(ESP_OK == ret) {
        // prepare MQTT client name (prefix + mac_address)
        memcpy(clientName, MQTT_CLIENT_ID, mqtt_client_id_len-12);
        for(i=0; i<6; i++) {
            sprintf(&clientName[mqtt_client_id_len-12+i*2], "%02x", mac_addr[i]);
        }
        clientName[mqtt_client_id_len] = 0;

        bool ssl = false;
        #if defined(MQTT_SECURITY) && (MQTT_SECURITY == 1)
        ssl = true;
        #endif
        mqttMgr.init(MQTT_HOST, MQTT_PORT, ssl, MQTT_USER, MQTT_PASS, clientName);
    }

    if(nullptr != clientName) free(clientName);

    return ret;
}


// ********************************************************************
// app_main
// ********************************************************************
//static vprintf_like_t previousLogVprintf;
void ConsoleStartHook(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    // alternative: previousLogVprintf = esp_log_set_vprintf();
}

void ConsoleExitHook(void)
{
    esp_log_level_set("*", (esp_log_level_t) CONFIG_LOG_DEFAULT_LEVEL);
    // alternaive: esp_log_set_vprintf(previousLogVprintf);
}

extern "C" void app_main()
{
    // Begin with system init now.
    ESP_ERROR_CHECK( nvs_flash_init() );

    // Initialize WiFi, but don't start yet.
    initialiseWifi();

    // Prepare global mqtt clientName (needed due to lack of named initializers in C99) 
    // and init the manager.
    ESP_ERROR_CHECK( initialiseMqttMgr() );

    // Start WiFi. Events will start/stop MQTT client
    ESP_ERROR_CHECK( esp_wifi_start() );

    // Initialize the time system
    TimeSystem_Init();

    ConsoleInit(true, ConsoleStartHook, ConsoleExitHook);

    irrigCtrl.start();
}

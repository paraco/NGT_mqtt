#include <string.h>
#include "nrc_sdk.h"
#include "MQTTClient.h"
#include "wifi_config_setup.h"
#include "wifi_connect_common.h"
#include "nvs.h"
#include "nvs_config.h"


#define BROKER_IP   "192.168.200.1"
#define BROKER_PORT 1883

#define GET_IP_RETRY_MAX 10
#define CONNECTION_RETRY_MAX 3
#define SET_DEFAULT_SCAN_CHANNEL_THRESHOLD 1

static nvs_handle_t nvs_handle;

void message_arrived(MessageData* data)
{
	nrc_usr_print("Message arrived on topic %.*s: %.*s\n", data->topicName->lenstring.len,
		data->topicName->lenstring.data, data->message->payloadlen, data->message->payload);
}

static nrc_err_t connect_to_ap(WIFI_CONFIG *param)
{
	int retry_count = 0;
	int i = 0;
	uint8_t retry_cnt = 0;

	nrc_usr_print("[%s] Sample App for Wi-Fi  \n",__func__);

	/* set initial wifi configuration */
	wifi_init(param);

	/* connect to AP */
	while(1) {
		if (wifi_connect(param)== WIFI_SUCCESS) {
			nrc_usr_print ("[%s] connect to %s successfully !! \n", __func__, param->ssid);
			break;
		} else {
			nrc_usr_print ("[%s] Fail for connection %s\n", __func__, param->ssid);
			if (retry_cnt > CONNECTION_RETRY_MAX) {
				nrc_usr_print("(connect) Exceeded retry limit (%d). Run sw_reset\n", CONNECTION_RETRY_MAX);
				nrc_sw_reset();
				return NRC_FAIL;
			} else if(retry_cnt == SET_DEFAULT_SCAN_CHANNEL_THRESHOLD){
				if (nvs_open(NVS_DEFAULT_NAMESPACE, NVS_READWRITE, &nvs_handle) == NVS_OK) {
					nrc_usr_print("[%s] NVS_WIFI_CHANNEL:%d...\n", __func__, 0);
					nvs_set_u16(nvs_handle, NVS_WIFI_CHANNEL, 0);
					nrc_set_default_scan_channel(param);
					nvs_close(nvs_handle);
				}
			}
			_delay_ms(1000);
			retry_cnt++;
		}
	}

	/* check if IP is ready */
	retry_cnt = 0;
	while(1){
		if (nrc_addr_get_state(0) == NET_ADDR_SET) {
			nrc_usr_print("[%s] IP ...\n",__func__);
			break;
		}
		if (++retry_cnt > GET_IP_RETRY_MAX) {
			nrc_usr_print("(Get IP) Exceeded retry limit (%d). Run sw_reset\n", GET_IP_RETRY_MAX);
			nrc_sw_reset();
		}
		_delay_ms(1000);
	}


	nrc_usr_print("[%s] Device is online connected to %s\n",__func__, param->ssid);
	return NRC_SUCCESS;
}

/******************************************************************************
 * FunctionName : run_sample_mqtt
 * Description  : sample test for mqtt
 * Parameters   : WIFI_CONFIG
 * Returns      : 0 or -1 (0: success, -1: fail)
 *******************************************************************************/
nrc_err_t run_sample_mqtt(WIFI_CONFIG *param)
{
	int count = 0;
	int interval = 0;
	SCAN_RESULTS results;
	uint32_t channel = 0;

	// MQTT client 메모리를 할당한다
	MQTTClient *mqtt_client = nrc_mem_malloc(sizeof(MQTTClient));
	memset(mqtt_client, 0x0, sizeof(MQTTClient));

	int message_count = 0;

	nrc_usr_print("[%s] Sample App for Mqtt \n",__func__);

	count = 10;
	interval = 1000;

	int i = 0;

	if (nvs_open(NVS_DEFAULT_NAMESPACE, NVS_READONLY, &nvs_handle) == NVS_OK) {
		nvs_get_u16(nvs_handle, NVS_WIFI_CHANNEL, (uint16_t*)&channel);
		nrc_usr_print("[%s] channel:%d...\n", __func__, channel);
		if(channel){
			param->scan_freq_list[0] = channel;
			param->scan_freq_num = 1;
		}
	}

	if (connect_to_ap(param) == NRC_SUCCESS) {
		nrc_usr_print("[%s] Sending data to server...\n", __func__);
		AP_INFO ap_info;
		if (nrc_wifi_get_ap_info(0, &ap_info) == WIFI_SUCCESS) {
			nrc_usr_print("[%s] AP ("MACSTR" %s (len:%d) %c%c bw:%d ch:%d freq:%d security:%d)\n",
				__func__, MAC2STR(ap_info.bssid), ap_info.ssid, ap_info.ssid_len,
				ap_info.cc[0],ap_info.cc[1], ap_info.bw, ap_info.ch, ap_info.freq,
				ap_info.security);
			if (nvs_open(NVS_DEFAULT_NAMESPACE, NVS_READWRITE, &nvs_handle) == NVS_OK) {
				nrc_usr_print("[%s] ap_info.freq:%d\n", __func__, ap_info.freq);
				nvs_set_u16(nvs_handle, NVS_WIFI_CHANNEL, ap_info.freq);
				nvs_close(nvs_handle);
			}
		}
	}

	unsigned char send_buf[80], read_buf[80];
	int rc = 0;
	Network network;
	MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;

	NetworkInit(&network);
	MQTTClientInit(mqtt_client, &network, 30000, send_buf, sizeof(send_buf), read_buf, sizeof(read_buf));

#if defined( SUPPORT_MBEDTLS ) && defined( USE_MQTTS )
	Certs cert;
	cert.ca_cert = ca_cert;
	cert.ca_cert_length = sizeof(ca_cert);
	cert.client_cert = client_cert;
	cert.client_cert_length = sizeof(client_cert);
	cert.client_pk = client_pk;
	cert.client_pk_length = sizeof(client_pk);
	cert.client_pk_pwd = client_pk_pwd;
	cert.client_pk_pwd_length = sizeof(client_pk_pwd) - 1;
#endif

#if defined( SUPPORT_MBEDTLS ) && defined( USE_MQTTS )
	if ((rc = NetworkConnectTLS(&network, BROKER_IP, BROKER_PORT, &cert)) != 0)
#else
	if ((rc = NetworkConnect(&network, BROKER_IP, BROKER_PORT)) != 0)
#endif
		nrc_usr_print("Return code from network connect is %d\n", rc);
	else
		nrc_usr_print("[%s] Network Connected\n", __func__);

	if ((rc = MQTTStartTask(mqtt_client)) != 1)
		nrc_usr_print("Return code from start tasks is %d\n", rc);

	connectData.MQTTVersion = 3;
	connectData.clientID.cstring = "nrc_11ah_mqtt_test";

	nrc_usr_print("[%s] Try to connect to MQTT Broker......\n", __func__);
	if ((rc = MQTTConnect(mqtt_client, &connectData)) != 0)
		nrc_usr_print("Return code from MQTT connect is %d\n", rc);
	else
		nrc_usr_print("[%s] MQTT Connected\n", __func__);

	if ((rc = MQTTSubscribe(mqtt_client, "halow/11ah/mqtt/sample/mytopic", 2, message_arrived)) != 0)
		nrc_usr_print("Return code from MQTT subscribe is %d\n", rc);

	for(i=0; i<count; i++) {
		MQTTMessage message;
		char payload[35];

		message.qos = 1;
		message.retained = 0;
		message.payload = payload;
		sprintf(payload, "message count %d", ++message_count);
		message.payloadlen = strlen(payload);

		if ((rc = MQTTPublish(mqtt_client, "halow/11ah/mqtt/sample/mytopic", &message)) != 0)
		nrc_usr_print("Return code from MQTT publish is %d\n", rc);

		_delay_ms(interval);
	}

	if ((rc = MQTTUnsubscribe(mqtt_client, "halow/11ah/mqtt/sample/mytopic")) != 0){
		nrc_usr_print("Return code from MQTT unsubscribe is %d\n", rc);
	}

	if ((rc = MQTTDisconnect(mqtt_client)) != 0){
		nrc_usr_print("Return code from MQTT disconnect is %d\n", rc);
	}else{
		nrc_usr_print("[%s] MQTT disconnected\n", __func__);
	}

#if defined( SUPPORT_MBEDTLS ) && defined( USE_MQTTS )
	if (NetworkDisconnectTLS(&network) != 0)
#else
	if (NetworkDisconnect(&network) != 0)
#endif
		nrc_usr_print("Network Disconnect Fail\n");
	else
		nrc_usr_print("[%s] Network Disonnected\n", __func__);

	vTaskDelete((TaskHandle_t)mqtt_client->thread.task);

	if (nrc_wifi_get_state(0) == WIFI_STATE_CONNECTED) {
		nrc_usr_print("[%s] Trying to DISCONNECT... for exit\n",__func__);
		if (nrc_wifi_disconnect(0, 5000) != WIFI_SUCCESS) {
			nrc_usr_print ("[%s] Fail for Wi-Fi disconnection (results:%d)\n", __func__);
			return NRC_FAIL;
		}
	}

	nrc_usr_print("[%s] exit \n",__func__);
	return NRC_SUCCESS;
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : Start Code for User Application, Initialize User function
 * Parameters   : none
 * Returns      : none
 *******************************************************************************/
WIFI_CONFIG wifi_config;
WIFI_CONFIG* param = &wifi_config;

void user_init(void)
{
	nrc_err_t ret;
	nrc_uart_console_enable(true);
	int count=  10;

	if(param == NULL)
		return;
	memset(param, 0x0, WIFI_CONFIG_SIZE);
	nrc_wifi_set_config(param);

	ret = run_sample_mqtt(param);
	nrc_usr_print("[%s] test result!! %s \n",__func__, (ret==0) ?  "Success" : "Fail");
}


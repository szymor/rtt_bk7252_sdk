#include <rtthread.h>
#include "task.h"
#include "wifi_conf.h"
#include "wifi_constants.h"
#include "lwip_netconf.h"
#include <platform/platform_stdlib.h>

/*	support both SSID1 and SSID2 open security,
	or both SSID1 and SSID2 wpa2/aes security,
	or SSID1 wpa2/aes and SSID2 open security */
#define SSID1      "test_ap1"
#define PASSWORD1  "12345678"
#define SSID2      "test_ap2"
#define PASSWORD2  ""	// define "" if open security
#define CHANNEL    6

extern struct netif xnetif[];

// wlan variables
extern int (*p_wlan_mgmt_filter)(uint8_t *ie, uint16_t ie_len, uint16_t frame_type);
extern uint8_t b_wlan_mgmt_allow_all;
extern uint8_t psk_essid[36];
extern uint8_t psk_passphrase[65];
extern uint8_t wpa_global_PSK[40];

static int ssid_idx = 0;
static int stop_sw_beacon = 0;
static uint8_t mac2[6];
static uint8_t psk2[40] = {0};

int wlan_mgmt_filter(uint8_t *ie, uint16_t ie_len, uint16_t frame_type)
{
	// ProbeReq
	if(frame_type == 0x40) {
		uint8_t *ptr = ie;
		char ssid[33];
		memset(ssid, 0, sizeof(ssid));

		// find SSID IE
		while(ptr < (ie + ie_len)) {
			if(*ptr == 0) {
				memcpy(ssid, ptr + 2, *(ptr + 1));
				break;
			}
			else {
				ptr += (2 + *(ptr + 1));
			}
		}

		if(strcmp(ssid, SSID1) == 0) {
			p_wlan_mgmt_filter = NULL;
			b_wlan_mgmt_allow_all = 0;
			stop_sw_beacon = 1;
			ssid_idx = 1;
		}
		else if(strcmp(ssid, SSID2) == 0) {
			p_wlan_mgmt_filter = NULL;
			b_wlan_mgmt_allow_all = 0;
			stop_sw_beacon = 1;
			ssid_idx = 2;

			// change to mac of ssid2 ap
			extern void rltk_wlan_change_mac(uint8_t *mac);
			rltk_wlan_change_mac(mac2);
			memcpy(xnetif[0].hwaddr, mac2, 6);
		}
	}

	return 0;
}

static int generate_beacon(uint8_t **beacon_buf, uint32_t *beacon_len, char *ssid, uint8_t *mac, rtw_security_t security, uint8_t channel)
{
	int ret = 0;

	uint8_t beacon_part1[] = {
		0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* MAC */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x21, 0x00};
	uint8_t beacon_part2[] = {
		0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
		0x03, 0x01, 0x00, /* channel */
		0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
		0x2a, 0x01, 0x00,
		0x32, 0x04, 0x30, 0x48, 0x60, 0x6c,
		0x2d, 0x1a, 0x20, 0x00, 0x01, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x3d, 0x16, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00,	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x83, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27,
		0xa4, 0x00, 0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00,
		0xdd, 0x0e, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a, 0x00, 0x01, 0x20, 0x10, 0x44, 0x00, 0x01, 0x02,
		0xdd, 0x06, 0x00, 0x0e, 0x4c, 0x02, 0x01, 0x10};
	uint8_t beacon_part3[] = {
		0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,	0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
		0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00
	};

	// set mac
	memcpy(beacon_part1 + 10, mac, 6);
	memcpy(beacon_part1 + 16, mac, 6);

	// set privacy
	if(security == RTW_SECURITY_WPA2_AES_PSK)
		beacon_part1[34] = 0x31;

	// set channel
	beacon_part2[12] = channel;

	uint32_t len = sizeof(beacon_part1) + 2 + strlen(ssid) + sizeof(beacon_part2);

	if(security == RTW_SECURITY_WPA2_AES_PSK)
		len += sizeof(beacon_part3);

	uint8_t *buf = (uint8_t *) malloc(len);

	if(buf) {
		memcpy(buf, beacon_part1, sizeof(beacon_part1));
		buf[sizeof(beacon_part1)] = 0; // element id of ssid
		buf[sizeof(beacon_part1) + 1] = strlen(ssid); // length of ssid
		memcpy(buf + sizeof(beacon_part1) + 2, ssid, strlen(ssid));
		memcpy(buf + sizeof(beacon_part1) + 2 + strlen(ssid), beacon_part2, sizeof(beacon_part2));

		if(security == RTW_SECURITY_WPA2_AES_PSK)
			memcpy(buf + sizeof(beacon_part1) + 2 + strlen(ssid) + sizeof(beacon_part2), beacon_part3, sizeof(beacon_part3));

		*beacon_buf = buf;
		*beacon_len = len;
		ret = 0;
	}
	else {
		*beacon_buf = NULL;
		*beacon_len = 0;
		ret = -1;
	}

	return ret;
}

void two_ap_thread(void *param)
{
	// start ssid1 ap
	fATW3(SSID1);
	if(strlen(PASSWORD1))
		fATW4(PASSWORD1);
	char ch[3];
	sprintf(ch, "%d", CHANNEL);
	fATW5(ch);
	fATWA(NULL);

	// calculate psk of ssid2 ap if wpa2/aes security
	if(strlen(PASSWORD2)) {
		extern int rom_psk_PasswordHash(unsigned char *password, int passwordlength, unsigned char *ssid, int ssidlength, unsigned char *output);
		rom_psk_PasswordHash(PASSWORD2, strlen(PASSWORD2), SSID2, strlen(SSID2), psk2);
	}

	// mac of ssid2 ap
	uint8_t *mac = (uint8_t *) LwIP_GetMAC(&xnetif[0]);
	memcpy(mac2, mac, 6);
	mac2[5] ++;

	// generate beacon for ssid2 ap
	uint8_t *beacon_buf = NULL;
	uint32_t beacon_len = 0;
	if(generate_beacon(&beacon_buf, &beacon_len, SSID2, mac2, strlen(PASSWORD2)?RTW_SECURITY_WPA2_AES_PSK:RTW_SECURITY_OPEN, CHANNEL) < 0) {
		printf("\n\r ERROR: generate_beacon \n\r");
	}
	else {
		p_wlan_mgmt_filter = wlan_mgmt_filter;
		b_wlan_mgmt_allow_all = 1;
		stop_sw_beacon = 0;
		ssid_idx = 0;

		// tx beacon for ssid2 ap
		while(stop_sw_beacon == 0) {
			static uint8_t count = 0;
			if(count == 10) {
				wext_send_mgnt(WLAN0_NAME, beacon_buf, beacon_len, 0);
				count = 0;
			}
			else {
				vTaskDelay(10);
				count ++;
			}
		}

		free(beacon_buf);

		if(ssid_idx == 2) {
			// copy calcuated psk of ssid2 ap to prevent time to calculate psk when wifi_start_ap
			strcpy(psk_essid, SSID2);
			strcpy(psk_passphrase, PASSWORD2);
			memcpy(wpa_global_PSK, psk2, sizeof(psk2));

			// switch to ssid2 ap without wifi off/on
			if (wifi_set_mode(RTW_MODE_AP) < 0){
		    	printf("\n\r ERROR: wifi_set_mode \n\r");
			}
			else {
				if(wifi_start_ap(SSID2, strlen(PASSWORD2)?RTW_SECURITY_WPA2_AES_PSK:RTW_SECURITY_OPEN
					, strlen(PASSWORD2)?PASSWORD2:NULL, strlen(SSID2), strlen(PASSWORD2)?strlen(PASSWORD2):0, CHANNEL) < 0) {

					printf("\n\r ERROR: wifi_start_ap \n\r");
				}
			}
		}
	}

	vTaskDelete(NULL);
}

void example_two_ap(void)
{
	if(xTaskCreate(two_ap_thread, ((const char*)"two_ap_thread"), 1024, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate(two_ap_thread) failed", __FUNCTION__);
}
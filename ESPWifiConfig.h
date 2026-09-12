
#if ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#if defined(ESP8266)
	#define BOARD_ESP 8266
#elif defined(ESP32)
	#define BOARD_ESP 32
#elif defined(__AVR__)
	#define BOARD_ESP 0
	#error Architecture is AVR instead of ESP8266 OR ESP32
#else
	#define BOARD_ESP 0
	#error Architecture is NOT ESP8266 OR ESP32
#endif

#if BOARD_ESP==32
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiMulti.h>
#elif BOARD_ESP==8266
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#endif

#include <EEPROM.h>
#include <DNSServer.h>
#include <webpages/define_vars.h>



// Number of built-in settings (fixed, never changed).
#define ESP_SETTINGS_BUILTIN 4
#define EEPROM_SIZE 4096

class ESPWifiConfig;


#define AP_MODE_NAME "AP_MODE"
#define CLIENT_MODE_NAME "CLIENT_MODE"

enum ESP_MODES {
  AP_MODE,
  CLIENT_MODE
};

enum settingsIndex {
  WIFI_SSID,
  WIFI_PASS,
  WEB_USER,
  WEB_PASS
};

// Setting names are RAM/PROGMEM only (never stored to flash), so enlarging
// this does not change the flash layout of existing devices.
#define NAME_MAX_SIZE 16
// Built-in settings keep their historical 64-byte flash slots (backward compatible).
#define VALUE_MAX_SIZE 64
// User-defined settings (addSetting()) get a 256-byte flash slot: 255 chars + NUL.
// First user slot starts at 4 * VALUE_MAX_SIZE, so at most
// (EEPROM_SIZE - 4 * VALUE_MAX_SIZE) / VALUE_USER_MAX_SIZE = 14 user slots fit.
#define VALUE_USER_MAX_SIZE 256
#define ESP_MAX_USER_SETTINGS ((EEPROM_SIZE - (ESP_SETTINGS_BUILTIN * VALUE_MAX_SIZE)) / VALUE_USER_MAX_SIZE)

// Placeholders inside the static setup page (webpages/define_vars.h):
// replaced with the "Custom" tab when user settings are registered.
#define CUSTOM_TAB_LINK_PLACEHOLDER "%%ESP_CUSTOM_TAB_LINK%%"
#define CUSTOM_TAB_BODY_PLACEHOLDER "%%ESP_CUSTOM_TAB_BODY%%"

class SettingsObject
{
	public:
		char name[NAME_MAX_SIZE];
		char value[VALUE_MAX_SIZE];
		// uint16_t: user slots are 256 bytes (unsigned char would wrap to 0)
		uint16_t max_size;
		int addr;
		// User-defined settings (index >= ESP_SETTINGS_BUILTIN) use a
		// heap-allocated buffer instead of the fixed 64-byte array, so the
		// built-ins keep their exact historical size in RAM and flash.
		char *user_value = nullptr;
		// Initial value for user-defined settings: used as the fallback when
		// the flash slot is empty (first boot, or after resetAllSettings()).
		// Heap-allocated, only set for user slots (nullptr for the built-ins).
		char *default_value = nullptr;
};



class ESPWifiConfig
{
	const char *sys_name;
	char *fallback_ssid;
	char *fallback_ssid_pass;
	boolean fix_ssid;
	
	boolean fallback_ssid_available = false;
	boolean known_ssid_available = false;
	boolean show_debug = false;
	boolean settings_locked = false;	// set by initialize(); addSetting() is rejected after that
	
	int reset_btn = -1;
	int http_port = 80;
#if BOARD_ESP==32
	WebServer server;
#elif BOARD_ESP==8266
	ESP8266WebServer server;
#endif

	
	public:
		ESPWifiConfig(const char *sys_namex, int port, int thisreset_btn, boolean fix_ssidx, char *fallback_ssidx, char *fallback_ssid_passx, boolean debug): server(port)
		{
			sys_name = sys_namex;
			reset_btn = thisreset_btn;
			fix_ssid = fix_ssidx;
			fallback_ssid = fallback_ssidx;
			fallback_ssid_pass = fallback_ssid_passx;
			show_debug = debug;
			http_port = port;
		};
		IPAddress ESP_IP;
		const byte DNS_PORT = 53;
		String get_AP_name();
		IPAddress apIP = {192, 168, 1, 1};
		DNSServer dnsServer;
		int ESP_mode = 0;
		boolean isHTTPserverRunning = false;
		unsigned long last_conn_to_http = 0;
		// Total number of settings = built-ins + user-defined (addSetting()).
		// All read/save/reset/web loops iterate over this.
		uint8_t ESP_settings_size = ESP_SETTINGS_BUILTIN;
		SettingsObject setting[ESP_SETTINGS_BUILTIN + ESP_MAX_USER_SETTINGS] = {{"WIFI_SSID", "", VALUE_MAX_SIZE, 0}, 
																			{"WIFI_PASS", "", VALUE_MAX_SIZE, VALUE_MAX_SIZE},
																			{"WEB_USER", "admin", VALUE_MAX_SIZE, VALUE_MAX_SIZE*2}, 
																			{"WEB_PASS", "pass_ESP", VALUE_MAX_SIZE, VALUE_MAX_SIZE*3}};

		// ------------------------------------------------------------------
		// User-extensible settings (v2.3.0)
		// ------------------------------------------------------------------
		// Register an additional, user-defined setting.
		// Must be called BEFORE initialize(). The slot is appended after the
		// built-in ones (flash offset 256, then +256 each) and is 255 chars
		// long. Returns the slot index (usable with getSetting(int)) or -1
		// if the name is invalid, a slot with the same name already exists,
		// no free slot is left, or initialize() was already called.
		int addSetting(const char *name, const char *defaultValue = "");
		
		// Read any setting (built-in or user-defined) by name or index.
		// Returns an empty String if the name/index is unknown.
		String getSetting(const char *name);
		String getSetting(int index);
		
		// Persist all settings (built-in + user-defined) to flash.
		void saveAllSettings();
		
		// Wipe all settings (built-in + user-defined) and fall back to defaults.
		void resetAllSettings();
		// ------------------------------------------------------------------
		
		// Value buffer of a setting (fixed array for built-ins, heap for user slots).
		char *setting_value(int index);
		
		boolean is_reset_pressed(int);
		void print_settings(void);
		void ESP_reset_settings(void);
		unsigned long getmacID(void);
		int initialize(void);
		boolean wifi_connected = false;
		String error_msg = "";
		unsigned char reset_pressed_count = 0;
		void handle(unsigned long);
		void Start_HTTP_Server(unsigned long);
		int goWild(void);
		void wifiscan(void);
		void ESP_debug(String);
		String debug_log = "";
		String input = "";
		
		
		
	private:
		//void onStationConnected(const WiFiEventSoftAPModeStationConnected& evt);
		//void onStationDisconnected(const WiFiEventSoftAPModeStationDisconnected& evt);
		void ESP_read_settings(void);
		void ESP_save_settings(void);
		int wifi_scan_count = 0;
		boolean try_wifi_connect();
		#if BOARD_ESP==32
			WiFiMulti wifiMulti;
		#elif BOARD_ESP==8266
			ESP8266WiFiMulti wifiMulti;
		#endif
		String ESP_IP_addresss = "";
		unsigned long last_try_wifi_connect = 0;
		unsigned long wild_scan_timer = 60000;
		unsigned long client_mode_active_time = 60000;
		unsigned int fails_try_wifi_connect = 0;
		String wild_wifis_list = "";
		
		void handleRoot(void);
		void handleNotFound(void);
		void print_login_page(void);
		void print_setup_page(void);
		void print_css_file(void);
		void handle_error(void);
		bool is_authentified(void);
		
		void handle_login(void);
		void handle_setup(void);
		void handle_ssid_list(void);
		void handle_read_data(void);
		void handle_cout(void);
		void handle_reboot(void);
		
};

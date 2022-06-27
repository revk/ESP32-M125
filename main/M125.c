/* M125 app */
/* Copyright ©2022 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

static const char __attribute__((unused)) TAG[] = "M125";

#include "revk.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include <driver/gpio.h>
#include <driver/uart.h>

#ifdef	CONFIG_LWIP_DHCP_DOES_ARP_CHECK
#warning CONFIG_LWIP_DHCP_DOES_ARP_CHECK means DHCP is slow
#endif
#ifndef	CONFIG_LWIP_DHCP_RESTORE_LAST_IP
#warning CONFIG_LWIP_DHCP_RESTORE_LAST_IP may improve speed
#endif
#ifndef	CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP
#warning CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP may speed boot
#endif
#if	CONFIG_BOOTLOADER_LOG_LEVEL > 0
#warning CONFIG_BOOTLOADER_LOG_LEVEL recommended to be no output
#endif

#define BITFIELDS "-"
#define PORT_INV 0x40
#define port_mask(p) ((p)&0xFF)

#define	settings	\
	u8(uart,1)	\
	io(nfcrx,)	\
	io(nfxtx,)	\
	io(button,)	\
	io(rx,)		\
	b(debug)	\

#define u32(n,d)        uint32_t n;
#define s8(n,d) int8_t n;
#define u8(n,d) uint8_t n;
#define b(n) uint8_t n;
#define s(n) char * n;
#define io(n,d)           uint8_t n;
settings
#undef io
#undef u32
#undef s8
#undef u8
#undef b
#undef s
    httpd_handle_t webserver = NULL;

void uart_task(void *arg)
{
   esp_err_t err = 0;
   uart_config_t uart_config = {
      .baud_rate = 600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
   };
   if (!err)
      err = uart_param_config(uart, &uart_config);
   if (!err)
      err = uart_set_pin(uart, -1, port_mask(rx), -1, -1);
   if (!err)
      err = uart_driver_install(uart, 1024, 0, 0, NULL, 0);
   if (err)
   {
      jo_t j = jo_object_alloc();
      jo_string(j, "error", "Failed to uart");
      jo_int(j, "uart", uart);
      jo_int(j, "gpio", port_mask(rx));
      jo_string(j, "description", esp_err_to_name(err));
      revk_error("uart", &j);
      return;
   }
   while (1)
   {
      uint8_t buf[256];
      int len = 0;
      len = uart_read_bytes(uart, buf, sizeof(buf), 100 / portTICK_PERIOD_MS);
      if (len <= 0)
         continue;
      jo_t j = jo_object_alloc();
      jo_int(j, "len", len);
      jo_base16(j, "data", buf, len);
      revk_info("uart", &j);
   }
}

static void web_head(httpd_req_t * req, const char *title)
{
   httpd_resp_set_type(req, "text/html; charset=utf-8");
   httpd_resp_sendstr_chunk(req, "<meta name='viewport' content='width=device-width, initial-scale=1'>");
   httpd_resp_sendstr_chunk(req, "<html><head><title>");
   if (title)
      httpd_resp_sendstr_chunk(req, title);
   httpd_resp_sendstr_chunk(req, "</title></head><style>"       //
                            "body{font-family:sans-serif;background:#8cf;}"     //
                            "</style><body><h1>");
   if (title)
      httpd_resp_sendstr_chunk(req, title);
   httpd_resp_sendstr_chunk(req, "</h1>");
}

static esp_err_t web_foot(httpd_req_t * req)
{
   httpd_resp_sendstr_chunk(req, "<hr><address>");
   char temp[20];
   snprintf(temp, sizeof(temp), "%012llX", revk_binid);
   httpd_resp_sendstr_chunk(req, temp);
   httpd_resp_sendstr_chunk(req, " <a href='wifi'>WiFi Setup</a></address></body></html>");
   httpd_resp_sendstr_chunk(req, NULL);
   return ESP_OK;
}

static esp_err_t web_icon(httpd_req_t * req)
{                               // serve image -  maybe make more generic file serve
   extern const char start[] asm("_binary_apple_touch_icon_png_start");
   extern const char end[] asm("_binary_apple_touch_icon_png_end");
   httpd_resp_set_type(req, "image/png");
   httpd_resp_send(req, start, end - start);
   return ESP_OK;
}

static esp_err_t web_root(httpd_req_t * req)
{
   if (revk_link_down())
      return revk_web_config(req);      // Direct to web set up
   web_head(req, *hostname ? hostname : appname);

   return web_foot(req);
}

void reader_task(void *arg)
{
   while (1)
   {
      sleep(1);
   }
}

const char *app_callback(int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   char value[1000];
   int len = 0;
   *value = 0;
   if (j)
   {
      len = jo_strncpy(j, value, sizeof(value));
      if (len < 0)
         return "Expecting JSON string";
      if (len > sizeof(value))
         return "Too long";
   }
   if (client || !prefix || target || strcmp(prefix, prefixcommand) || !suffix)
      return NULL;              //Not for us or not a command from main MQTT
   if (!strcmp(suffix, "connect"))
   {
   }
   if (!strcmp(suffix, "shutdown"))
      httpd_stop(webserver);
   return NULL;
}

// --------------------------------------------------------------------------------
// Web
#ifdef	CONFIG_REVK_APCONFIG
#error 	Clash with CONFIG_REVK_APCONFIG set
#endif

void app_main()
{
   revk_boot(&app_callback);
#define io(n,d)           revk_register(#n,0,sizeof(n),&n,BITFIELDS" "#d,SETTING_SET|SETTING_BITFIELD);
#define b(n) revk_register(#n,0,sizeof(n),&n,NULL,SETTING_BOOLEAN);
#define u32(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s8(n,d) revk_register(#n,0,sizeof(n),&n,#d,SETTING_SIGNED);
#define u8(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s(n) revk_register(#n,0,0,&n,NULL,0);
   settings
#undef io
#undef u32
#undef s8
#undef u8
#undef b
#undef s
       revk_start();

   // Web interface
   httpd_config_t config = HTTPD_DEFAULT_CONFIG();
   if (!httpd_start(&webserver, &config))
   {
      {
         httpd_uri_t uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = web_root,
            .user_ctx = NULL
         };
         REVK_ERR_CHECK(httpd_register_uri_handler(webserver, &uri));
      }
      {
         httpd_uri_t uri = {
            .uri = "/apple-touch-icon.png",
            .method = HTTP_GET,
            .handler = web_icon,
            .user_ctx = NULL
         };
         REVK_ERR_CHECK(httpd_register_uri_handler(webserver, &uri));
      }
      {
         httpd_uri_t uri = {
            .uri = "/wifi",
            .method = HTTP_GET,
            .handler = revk_web_config,
            .user_ctx = NULL
         };
         REVK_ERR_CHECK(httpd_register_uri_handler(webserver, &uri));
      }
      revk_web_config_start(webserver);
   }

   revk_task("uart", uart_task, 0);
   revk_task("reader", reader_task, 0);

   if (button)
   {
      gpio_reset_pin(port_mask(button));
      gpio_set_level(port_mask(button), (button & PORT_INV) ? 0 : 1);
      gpio_set_direction(port_mask(button), GPIO_MODE_OUTPUT);
   }

   while (1)
   { // Main loop, pick up uart and reader events
      sleep(1);
   }
}

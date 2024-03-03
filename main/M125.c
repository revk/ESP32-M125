/* M125 app */
/* Copyright ©2022 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

static const char __attribute__((unused)) TAG[] = "M125";

#include "revk.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include "pn532.h"
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

pn532_t *pn532 = NULL;
char fobid[21];
char weight[30];
volatile uint8_t tagready = 0;
volatile uint8_t weightready = 0;

void
uart_task (void *arg)
{
   esp_err_t err = 0;
   uart_config_t uart_config = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
   };
   if (!err)
      err = uart_param_config (uart, &uart_config);
   if (!err)
      err = uart_set_pin (uart, -1, rx.num, -1, -1);
   if (!err)
      err = uart_driver_install (uart, 1024, 0, 0, NULL, 0);
   if (err)
   {
      jo_t j = jo_object_alloc ();
      jo_string (j, "error", "Failed to uart");
      jo_int (j, "uart", uart);
      jo_int (j, "gpio", rx.num);
      jo_string (j, "description", esp_err_to_name (err));
      revk_error ("uart", &j);
      return;
   }
   while (1)
   {
      char buf[256];
      int len = 0;
      len = uart_read_bytes (uart, buf, sizeof (buf) - 1, 100 / portTICK_PERIOD_MS);
      if (len <= 0)
         continue;
      buf[len] = 0;
      if (weightready)
         continue;
      // Extract net weight
      char *g = strstr (buf, "NET WEIGHT");
      if (!g)
         continue;
      g += 10;
      while (*g == ' ')
         g++;
      char *e = strchr (g, '\r');
      if (!e || e - g > sizeof (weight))
         continue;
      *e = 0;
      strcpy (weight, g);
      ESP_LOGI (TAG, "Weight:%s", weight);
      weightready = 1;
   }
}

void
reader_task (void *arg)
{
   int cards = 0;
   while (1)
   {
      usleep (100000);
      if (!pn532)
      {
         pn532 = pn532_init (nfcuart, 4, nfctx.num, nfcrx.num, 0);
         if (!pn532)
            continue;
         ESP_LOGI (TAG, "NFC Init OK");
      }
      if (cards)
      {
         if (pn532_Present (pn532))
            continue;
         cards = 0;
         ESP_LOGI (TAG, "Gone");
      }
      if (tagready)
         continue;              // Waiting
      cards = pn532_Cards (pn532);
      if (cards <= 0)
         continue;
      pn532_nfcid (pn532, fobid);
      if (!*fobid)
         continue;
      ESP_LOGI (TAG, "Card %s", fobid);
      tagready = 1;
   }
}

const char *
app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   char value[1000];
   int len = 0;
   *value = 0;
   if (j)
   {
      len = jo_strncpy (j, value, sizeof (value));
      if (len < 0)
         return "Expecting JSON string";
      if (len > sizeof (value))
         return "Too long";
   }
   if (client || !prefix || target || strcmp (prefix, prefixcommand) || !suffix)
      return NULL;              //Not for us or not a command from main MQTT
   if (!strcmp (suffix, "connect"))
   {
   }
   return NULL;
}

// --------------------------------------------------------------------------------
// Web
#ifdef	CONFIG_REVK_APCONFIG
#error 	Clash with CONFIG_REVK_APCONFIG set
#endif

void
app_main ()
{
   *fobid = 0;
   *weight = 0;
   revk_boot (&app_callback);
   revk_start ();
   if (!*cloudhost || !*cloudpass)
   {                            // Special defaults
      jo_t j = jo_object_alloc ();
      if (!*cloudhost)
         jo_string (j, "cloudhost", "weigh.me.uk");
      if (!*cloudpass)
      {
         int i;
         char pass[33];
         for (i = 0; i < sizeof (pass) - 1; i++)
            pass[i] = 'A' + (esp_random () % 26);
         pass[i] = 0;           // End
         jo_string (j, "cloudpass", pass);
      }
      revk_setting (j);
      jo_free (&j);
   }

   revk_task ("uart", uart_task, 0, 8);
   revk_task ("reader", reader_task, 0, 8);

   revk_gpio_output (button);
   revk_gpio_set (button, 0);

   while (1)
   {                            // Main loop, pick up uart and reader events
      usleep (100000);
      if (tagready)
      {                         // We press button until we get weight or give up
         int try = 20;
         if (!button.set)
            sleep (5);
         else
            while (try-- && !weightready)
            {
               ESP_LOGI (TAG, "Pushing send");
               revk_gpio_set (button, 1);
               usleep (100000);
               revk_gpio_set (button, 0);
               sleep (2);
               if (weightready && *weight == '0')
                  weightready = 0;      // Try again. Small weight
            }
      }
      if (tagready || weightready)
      {                         // Send
         ESP_LOGI (TAG, "Send data");
         float kg = -1;
         jo_t j = jo_object_alloc ();
         if (tagready)
            jo_string (j, "id", fobid);
         if (weightready)
         {
            jo_string (j, "weight", weight);
            // NET WEIGHT    0 st  0.0 lb
            // NET WEIGHT         0.00 kg
            float st,
              lb;
            if (strstr (weight, "st") && sscanf (weight, "%f st %f lb", &st, &lb) == 2)
               kg = (st * 14 + lb) / 2.20462;
            else if (strstr (weight, "lb") && sscanf (weight, "%f lb", &lb) == 1)
               kg = lb / 2.20462;
            else if (strstr (weight, "kg") && sscanf (weight, "%f kg", &lb) == 1)
               kg = lb;
            if (kg >= 0)
               jo_litf (j, "kg", "%.2f", kg);
         }
         revk_error ("weight", &j);

         if (weightready && kg > 0 && (*toot || *topic))
         {
            struct tm t;
            time_t now = time (0);
            localtime_r (&now, &t);
            char when[30];
            strftime (when, sizeof (when), "%F %T %Z", &t);

            char *pl = NULL;
            asprintf (&pl, "%s %s %s\r\nGood %s, your weight is %.2fkg (%s)\r\n\r\n",      //
                      when, hostname, fobid,     //
                      t.tm_hour < 12 ? "morning" : t.tm_hour < 18 ? "afternoon" : "evening", kg, weight);
            if (*toot)
               revk_mqtt_send_raw ("toot", 0, pl, 1);
            if (*topic)
               revk_mqtt_send_raw (topic, 0, pl, 1);
            free (pl);
         }
         if (*cloudhost)
         {
            char url[250];
            int m = sizeof (url) - 1,
               p = 0;
            if (p < m)
               p += snprintf (url + p, m - p, "https://%s", cloudhost);
            if (p < m)
               p += snprintf (url + p, m - p, "/weighin.cgi?version=%s", revk_version);
            if (p < m)
               p += snprintf (url + p, m - p, "&scales=%s", revk_id);
            if (p < m && cloudpass)
               p += snprintf (url + p, m - p, "&auth=%s", cloudpass);   // Assume no special characters
            if (p < m && weightready)
               p += snprintf (url + p, m - p, "&weight=%s", weight);
            if (p < m && weightready && kg >= 0)
               p += snprintf (url + p, m - p, "&kg=%.2f", kg);
            if (p < m && tagready)
               p += snprintf (url + p, m - p, "&id=%s", fobid);
            url[p] = 0;
            for (p = 0; url[p]; p++)
               if (url[p] == ' ')
                  url[p] = '+';
            esp_http_client_config_t config = {.url = url,.crt_bundle_attach = esp_crt_bundle_attach };
            esp_http_client_handle_t client = esp_http_client_init (&config);
            if (client)
            {
               esp_http_client_perform (client);
               esp_http_client_flush_response (client, NULL);
               int status = esp_http_client_get_status_code (client);
               if (status == 426)
                  revk_command ("upgrade", NULL);       // Upgrade requested by server
               esp_http_client_cleanup (client);
            }
         }
         *fobid = 0;
         *weight = 0;
         tagready = 0;
         weightready = 0;
      }
   }
}

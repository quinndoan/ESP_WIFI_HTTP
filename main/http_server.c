#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "http_server.h"
#include "task_common.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <queue.h>
#include <esp_ota_ops.h>

static const char TAG[] = "http_server";
// http server task handle
static httpd_handle_t http_server_handle = NULL;

static int g_fw_update_status = OTA_UPDATE_PENDING;

// http server monitor task handle
static TaskHandle_t task_http_server_monitor = NULL;
// queue handle 
static QueueHandle_t http_server_monitor_queue_handle;

// esp32 timer configuration passed to esp_timer create
const esp_timer_create_args_t fw_update_reset_args = {
    .callback = &http_server_fw_update_reset_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "fw_update_reset"
};

esp_timer_handle_t fw_update_reset;

extern const uint8_t jquery_3_3_1_min_js_start[]	asm("_binary_jquery_3_3_1_min_js_start");
extern const uint8_t jquery_3_3_1_min_js_end[]		asm("_binary_jquery_3_3_1_min_js_end");
extern const uint8_t index_html_start[]				asm("_binary_index_html_start");
extern const uint8_t index_html_end[]				asm("_binary_index_html_end");
extern const uint8_t app_css_start[]				asm("_binary_app_css_start");
extern const uint8_t app_css_end[]					asm("_binary_app_css_end");
extern const uint8_t app_js_start[]					asm("_binary_app_js_start");
extern const uint8_t app_js_end[]					asm("_binary_app_js_end");
extern const uint8_t favicon_ico_start[]			asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[]				asm("_binary_favicon_ico_end");


/**
 * Checks the g_fw_update_status and creates the fw_update_reset timer if g_fw_update_status is true.
 */
static void http_server_fw_update_reset_timer(void)
{
	if (g_fw_update_status == OTA_UPDATE_SUCCESSFUL)
	{
		ESP_LOGI(TAG, "http_server_fw_update_reset_timer: FW updated successful starting FW update reset timer");

		// Give the web page a chance to receive an acknowledge back and initialize the timer
		ESP_ERROR_CHECK(esp_timer_create(&fw_update_reset_args, &fw_update_reset));
		ESP_ERROR_CHECK(esp_timer_start_once(fw_update_reset, 8000000));
	}
	else
	{
		ESP_LOGI(TAG, "http_server_fw_update_reset_timer: FW update unsuccessful");
	}
}


// monitor task to track events of the HTTP
static void http_server_monitor(void *parameter){
    http_server_queue_message_t msg;
    for (;;){
        if (xQueueReceive(http_server_monitor_queue_handle, &msg, portMAX_DELAY))
        {
            switch (msg.msgID)
            {
            case HTTP_MSG_WIFI_CONNECT_INIT/* constant-expression */:
                /* code */
                ESP_LOGI_LEVEL(TAG, "HTTP_MSG_WIFI_CONNECT_INIT");
                break;
            case HTTP_MSG_WIFI_CONNECT_SUCCESS:
					ESP_LOGI_LEVEL(TAG, "HTTP_MSG_WIFI_CONNECT_SUCCESS");

					break;

				case HTTP_MSG_WIFI_CONNECT_FAIL:
					ESP_LOGI_LEVEL(TAG, "HTTP_MSG_WIFI_CONNECT_FAIL");

					break;

				case HTTP_MSG_OTA_UPDATE_SUCCESSFUL:
					ESP_LOGI_LEVEL(TAG, "HTTP_MSG_OTA_UPDATE_SUCCESSFUL");
                    http_server_fw_update_reset_timer();
					break;

				case HTTP_MSG_OTA_UPDATE_FAILED:
					ESP_LOGI_LEVEL(TAG, "HTTP_MSG_OTA_UPDATE_FAILED");
                    g_fw_update_status = OTA_UPDATE_FAILED;
					break;

				case HTTP_MSG_OTA_UPDATE_INITIALIZED:
					ESP_LOGI_LEVEL(TAG, "HTTP_MSG_OTA_UPATE_INITIALIZED");

					break;
            default:
                break;
            }
        }
    }
}

static esp_err_t http_server_jquery_handler(httpd_req_t *req){
    ESP_LOGI(TAG, "JQuery requested");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char*) jquery_3_3_1_min_js_start, jquery_3_3_1_min_js_end - jquery_3_3_1_min_js_start);
    return ESP_OK;
}

static esp_err_t http_server_index_html_handler(httpd_req_t *req){
    ESP_LOGI(TAG, "index.html requested");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t http_server_app_css_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "app.css requested");

	httpd_resp_set_type(req, "text/css");
	httpd_resp_send(req, (const char *)app_css_start, app_css_end - app_css_start);

	return ESP_OK;
}

/**
 * app.js get handler is requested when accessing the web page.
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */
static esp_err_t http_server_app_js_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "app.js requested");

	httpd_resp_set_type(req, "application/javascript");
	httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start);

	return ESP_OK;
}

/**
 * Sends the .ico (icon) file when accessing the web page.
 * @param req HTTP request for which the uri needs to be handled.
 * @return ESP_OK
 */
static esp_err_t http_server_favicon_ico_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "favicon.ico requested");

	httpd_resp_set_type(req, "image/x-icon");
	httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_end - favicon_ico_start);

	return ESP_OK;
}

static httpd_handle_t http_server_configure(void){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    xTaskCreatePinnedToCore(&http_server_monitor, "http_server_monitor", HTTP_SERVER_MONITOR_STACK_SIZE, NULL, HTTP_SERVER_MONITOR_PRIORITY, &task_http_server_monitor, HTTP_SERVER_MONITOR_CORE_ID);
    // message queue
    http_server_monitor_queue_handle = xQueueGenericCreate( ( 3 ), ( sizeof(http_server_queue_message_t) ), ( ( ( uint8_t ) 0U ) ) );
    config.core_id = HTTP_SERVER_TASK_CORE_ID;
    config.task_priority = HTTP_SERVER_TASK_PRIORITY;
    config.stack_size = HTTP_SERVER_TASK_STACK_SIZE;
    config.max_uri_handlers = 20;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;


	ESP_LOGI(TAG,
			"http_server_configure: Starting server on port: '%d' with task priority: '%d'",
			config.server_port,
			config.task_priority);
    // start the httpd server
    if (httpd_start(&http_server_handle, &config) == ESP_OK){
        ESP_LOGI(TAG, "http_server_configure: registering URL handlers");
        
        // register query handler
        httpd_uri_t jquery_js = {
            .uri = "/jquery-3.3.1.min.js",
				.method = HTTP_GET,
				.handler = http_server_jquery_handler,
				.user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &jquery_js);
		// register index.html handler
		httpd_uri_t index_html = {
				.uri = "/",
				.method = HTTP_GET,
				.handler = http_server_index_html_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &index_html);

		// register app.css handler
		httpd_uri_t app_css = {
				.uri = "/app.css",
				.method = HTTP_GET,
				.handler = http_server_app_css_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &app_css);

		// register app.js handler
		httpd_uri_t app_js = {
				.uri = "/app.js",
				.method = HTTP_GET,
				.handler = http_server_app_js_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &app_js);

		// register favicon.ico handler
		httpd_uri_t favicon_ico = {
				.uri = "/favicon.ico",
				.method = HTTP_GET,
				.handler = http_server_favicon_ico_handler,
				.user_ctx = NULL
		};
		httpd_register_uri_handler(http_server_handle, &favicon_ico);

		return http_server_handle;
	}

	return NULL;
}

esp_err_t http_server_OTA_update_handler(httpd_req_t *req){
    esp_ota_handle_t ota_handle;
    char ota_buff[1024];
    int content_length = req-> content_len;
    int content_received = 0;
    int recv_len;
    bool is_req_body_started = false;
    bool flash_successful = false;

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition;
    do {
        // read data for request
        if ((recv_len = httpd_req_recv(req, ota_buff, MIN(content_length, sizeof(ota_buff))))< 0){
            // check timeout
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT){
                ESP_LOGI (TAG, "http_server_OTA_update_handler: socket timeout");
                continue;
            }
            ESP_LOGI(TAG, "http_server_OTA_update_handle: Other error %d", recv_len);
            return ESP_FAIL;
        }
        printf("http_server_OTA_update_handler: OTA RX: %d of %d\r", content_received, content_length);
        
        // check xem co phai lan gui dau khong, if first time, have header
        if (!is_req_body_started){
            is_req_body_started = true;
            // get location
            char *body_start_p = strstr(ota_buff, "\r\n\r\n")+ 4;
            int body_part_len = recv_len - (body_start_p - ota_buff);
            printf("http_server_OTA_update_handler: OTA file size: %d\r\n", content_length);
            esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
            if (err!= ESP_OK){
                printf("update_handler: failed");
                return ESP_FAIL;
            }
            else{
                printf("Writing to partition");

            }
        }
        else{
            // write to the first part of the data
            esp_ota_write(ota_handle, ota_buff, recv_len);
            content_received += recv_len;
        
        }
    }while (recv_len>0 && content_received < content_length);
    if (esp_ota_end(ota_handle) == ESP_OK)
	{
		// Lets update the partition
		if (esp_ota_set_boot_partition(update_partition) == ESP_OK)
		{
			const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
			ESP_LOGI(TAG, "http_server_OTA_update_handler: Next boot partition subtype %d at offset 0x%lx", boot_partition->subtype, boot_partition->address);
			flash_successful = true;
		}
		else
		{
			ESP_LOGI(TAG, "http_server_OTA_update_handler: FLASHED ERROR!!!");
		}
	}
	else
	{
		ESP_LOGI(TAG, "http_server_OTA_update_handler: esp_ota_end ERROR!!!");
	}

	// We won't update the global variables throughout the file, so send the message about the status
	if (flash_successful) { http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_SUCCESSFUL); } else { http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_FAILED); }

	return ESP_OK;
}

// ota status handler
esp_err_t http_server_OTA_status_handler(httpd_req_t *req){
    char otaJSON[100];
    ESP_LOGI(TAG, "OTAStatus requested");
    sprintf(otaJSON, "{\"ota_update_status\":%d, \"compile_date\":\"%s\"}", g_fw_update_status, __TIME__, __DATE__);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, otaJSON, strlen(otaJSON));
    return ESP_OK;
}

void http_server_start(void)
{
	if (http_server_handle == NULL)
	{
		http_server_handle = http_server_configure();
	}
}

void http_server_stop(void)
{
	if (http_server_handle)
	{
		httpd_stop(http_server_handle);
		ESP_LOGI(TAG, "http_server_stop: stopping HTTP server");
		http_server_handle = NULL;
	}
	if (task_http_server_monitor)
	{
		vTaskDelete(task_http_server_monitor);
		ESP_LOGI(TAG, "http_server_stop: stopping HTTP server monitor");
		task_http_server_monitor = NULL;
	}
}

BaseType_t http_server_monitor_send_message(http_server_message_e msgID)
{
	http_server_queue_message_t msg;
	msg.msgID = msgID;
	return xQueueSend(http_server_monitor_queue_handle, &msg, portMAX_DELAY);
}

// callback function khi timer het thoi gian de khong treo
void http_server_fw_update_reset_callback(void *arg){
    ESP_LOGI(TAG, "http_server_fw_update_reset_callback: timer timeout");
    esp_restart();
}
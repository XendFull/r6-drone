/*
 * ESP32-CAM (AI-Thinker) — Camera stream (port 80) + WebSocket motor control (port 81)
 *
 * Motor driver: DRV8833
 *   IN1 = GPIO14
 *   IN2 = GPIO15
 *   IN3 = GPIO13
 *   IN4 = GPIO12
 *
 * Requires: "ESP32" board package, "WebSockets" library by Markus Sattler (Links2004)
 *
 * NOTE ON GPIO12: this is a boot strapping pin. It defaults LOW at boot, which is fine for
 * most DRV8833 wiring, but avoid anything that could pull it HIGH during power-up/reset,
 * or the board may fail to boot correctly.
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WebSocketsServer.h>
#include "esp_http_server.h"

// ============ WiFi credentials ============
const char* ssid     = "********";
const char* password = "********";

// ============ AI-Thinker camera pin map ============
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ============ Motor driver pins (DRV8833) ============
#define IN1 14
#define IN2 15
#define IN3 13
#define IN4 12

// ============ PWM speed control ============
const int PWM_FREQ = 5000;
const int PWM_RES  = 8;      // 8-bit -> duty range 0-255

WebSocketsServer webSocket = WebSocketsServer(81);
httpd_handle_t index_httpd = NULL;

// ---------------- Minimal HTML control/preview page ----------------
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><title>ESP32-CAM Robot</title></head>
<body style="text-align:center;font-family:sans-serif;">
<h2>ESP32-CAM Robot</h2>
<img src="/stream" style="width:90%;max-width:480px;border:1px solid #333;">
</body></html>
)HTML";

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

// ---------------- MJPEG stream handler ----------------
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        Serial.println("Non-JPEG frame skipped");
        esp_camera_fb_return(fb);
        continue;
      }
      size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
      if (res == ESP_OK) res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
      if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
      if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
      esp_camera_fb_return(fb);
    }
    if (res != ESP_OK) break;
  }
  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;

  httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };

  if (httpd_start(&index_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(index_httpd, &index_uri);
    httpd_register_uri_handler(index_httpd, &stream_uri);
  }
}

// ---------------- Motor control (independent per-motor PWM speed) ----------------
// speed range: -255 (full reverse) to +255 (full forward), 0 = stop
void setMotorA(int speed) {
  speed = constrain(speed, -255, 255);
  if (speed >= 0) { ledcWrite(IN1, speed); ledcWrite(IN2, 0); }
  else            { ledcWrite(IN1, 0);     ledcWrite(IN2, -speed); }
}

void setMotorB(int speed) {
  speed = constrain(speed, -255, 255);
  if (speed >= 0) { ledcWrite(IN3, speed); ledcWrite(IN4, 0); }
  else            { ledcWrite(IN3, 0);     ledcWrite(IN4, -speed); }
}

void stopMotors() {
  setMotorA(0);
  setMotorB(0);
}

// ---------------- WebSocket event handler ----------------
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected\n", num);
      stopMotors();  // safety: stop if the link drops
      break;

    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Client connected from %u.%u.%u.%u\n", num, ip[0], ip[1], ip[2], ip[3]);
      webSocket.sendTXT(num, "CONNECTED");
      break;
    }

    case WStype_TEXT: {
      String cmd = String((char*)payload).substring(0, length);
      Serial.printf("[%u] Command: %s\n", num, cmd.c_str());
      if      (cmd == "S" || cmd == "STOP") stopMotors();
      else if (cmd.startsWith("A:")) {
        setMotorA(cmd.substring(2).toInt());
      }
      else if (cmd.startsWith("B:")) {
        setMotorB(cmd.substring(2).toInt());
      }
      break;
    }

    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  ledcAttach(IN1, PWM_FREQ, PWM_RES);
  ledcAttach(IN2, PWM_FREQ, PWM_RES);
  ledcAttach(IN3, PWM_FREQ, PWM_RES);
  ledcAttach(IN4, PWM_FREQ, PWM_RES);
  stopMotors();

  // ---------------- Camera init ----------------
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (psramFound()) {
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  // ---------------- WiFi ----------------
  // IMPORTANT: disable power-save so incoming packets (WS handshake + motor
  // commands) aren't delayed by radio sleep cycles.
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("Camera stream ready: http://");
  Serial.println(WiFi.localIP());

  startCameraServer();

  // ---------------- WebSocket ----------------
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WS READY on port 81");
}

void loop() {
  // MUST be called every pass with no blocking delay before/after it,
  // or the WebSocket handshake will time out on the client side.
  webSocket.loop();
}

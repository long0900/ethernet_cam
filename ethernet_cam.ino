
#include "soc/soc.h" //disable brownout problems
#include "soc/rtc_cntl_reg.h"  //disable brownout problems


#include "esp_camera.h"
#include "cam_pins.h"
#include "SPI.h"
#include "Ethernet.h"
#include <ArduinoOTA.h>
#include <utility\w5100.h>
#include "String.h"
#include "index_html.h"

#define BOUNDARY "0123456789876543210"

byte socketStat[MAX_SOCK_NUM];

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
IPAddress ip(192, 168, 1, 100);
//IPAddress gateway(192, 168, 1, 1);
//IPAddress subnet(255, 255, 255, 0);

EthernetServer server(80);
EthernetServer streamServer(81);

uint32_t t1_cnt;

void setup() 
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
 
  Serial.begin(115200);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
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

  config.frame_size = FRAMESIZE_SVGA;
  config.pixel_format = PIXFORMAT_JPEG; 
  config.jpeg_quality = 8;
  config.fb_count = 1;

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) 
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
  // SCLK, MISO, MOSI, CS
  SPI.begin(14, 12, 13, 15);
  Ethernet.init(15);
  Ethernet.begin(mac, ip);
  while(Ethernet.hardwareStatus() == EthernetNoHardware)
  {
    Serial.println("Ethernet hardware wasn't found.");
    delay(1000);
  }
  Serial.println("Found Ethernet hardware.");
  while(Ethernet.linkStatus() != LinkON)
  {
    Serial.println("Ethernet cable is not connnected.");
    delay(1000);
  }
  Serial.println("Ethernet cable is connnected.");

  ledcSetup(5, 5000, 8);
  ledcAttachPin(4, 5);
  ledcWrite(5, 1);

  ArduinoOTA.begin(Ethernet.localIP(), "Arduino", "password", InternalStorage);
  streamServer.begin();
}

void loop() 
{
  ArduinoOTA.poll();

  EthernetClient client = server.available();
  if(client)
  {
    bool currentLineIsBlank = true;
    if(client.connected()) 
    {
      while(client.available())
      {
        char c = client.read();
        Serial.write(c);
        if (c == '\n' && currentLineIsBlank) 
        {
          String s = "HTTP/1.1 200 OK\r\n";
          s += "Content-Length:";
          s += sizeof(INDEX_HTML);
          s += "\r\n";
          client.print(s);
          s = "Connection: close\r\n";
          s += "Content-Type: text/html; charset=UTF-8\r\n";
          s += "Content-Encoding: deflate\r\n\r\n";
          client.print(s);

          client.print(INDEX_HTML);
          client.print("\r\n\r\n");
        }
        if (c == '\n')
        {
          // you're starting a new line
          currentLineIsBlank = true;
        } 
        else if (c != '\r') 
        {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();
  }
  
  uint32_t current_ms = millis();
  if( current_ms - t1_cnt >= 100 )
  {
    t1_cnt = current_ms;
    //showSockStatus();
    EthernetClient streamClient = streamServer.available();
    if(streamClient) 
    {
      // an http request ends with a blank line
      bool currentLineIsBlank = true;
      if(streamClient.connected()) 
      {
        String s;
        camera_fb_t *fb = NULL;
        
        while(streamClient.available())
        {
          char c = client.read();
          //Serial.write(c);
          if (c == '\n' && currentLineIsBlank) 
          {
            s = "HTTP/1.1 200 OK\r\n";
            s += "Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY "\r\n\r\n";
            streamClient.print(s);
          }
          if (c == '\n')
          {
            // you're starting a new line
            currentLineIsBlank = true;
          } 
          else if (c != '\r') 
          {
            // you've gotten a character on the current line
            currentLineIsBlank = false;
          }
        }
        
        fb = esp_camera_fb_get();
        if (!fb) 
        {
          Serial.println("Camera capture failed");
        }
        size_t jpeg_len = fb->len;
        Serial.println(jpeg_len);
        
        s = "--" BOUNDARY "\r\n";
        s += "Content-Type: image/jpeg\r\n";
        s += "Content-Length:";
        s += String(jpeg_len);
        s += "\r\n\r\n";
        
        streamClient.print(s);

        if(jpeg_len <= 2048)
        {
          streamClient.write(fb->buf, jpeg_len);
        }
        else
        {
          size_t offset = 0;
          for(;offset <= jpeg_len; offset += 2048)
          {
            streamClient.write(fb->buf + offset, 2048);
          }
          streamClient.write(fb->buf + offset, jpeg_len - offset);
        }
        streamClient.print("\r\n\r\n");
      }
      
      // give the web browser time to receive the data
      delay(1);
      // close the connection:
      streamClient.stop();
    }
  }
}

void showSockStatus(void)
{
  for (int i = 0; i < MAX_SOCK_NUM; i++) 
  {
    Serial.print("Socket#");
    Serial.print(i);
    uint8_t s = W5100.readSnSR(i);
    socketStat[i] = s;
    Serial.print(":0x");
    Serial.print(s,16);
    Serial.print(" ");
    Serial.print(W5100.readSnPORT(i));
    Serial.print(" D:");
    uint8_t dip[4];
    W5100.readSnDIPR(i, dip);
    for (int j=0; j<4; j++) 
    {
      Serial.print(dip[j],10);
      if (j<3) Serial.print(".");
    }
    Serial.print("(");
    Serial.print(W5100.readSnDPORT(i));
    Serial.println(")");
  }
}

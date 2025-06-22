#include <Arduino.h>

#define FASTLED_RMT_BUILTIN_DRIVER 0
#define FASTLED_EXPERIMENTAL_ESP32_RGBW_ENABLED 0
#define FASTLED_EXPERIMENTAL_ESP32_RGBW_MODE kRGBWExactColors

#include <FastLED.h>

#define GH_INCLUDE_PORTAL
//#define GH_FILE_PORTAL
#include <GyverHub.h>
#include <PairsFile.h>
#include <LittleFS.h>
#include <WiFiConnector.h>

#include "ColorConverterLib.h"

#define VERSION "0.1.1"

GyverHub hub;
PairsFile data(&LittleFS, "/data.dat", 3000);

// How many leds in your strip?
#define NUM_LEDS 161

// For led chips like WS2812, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
// Clock pin only needed for SPI based chipsets when not using hardware SPI
#define DATA_PIN D1

// Define the array of leds
CRGB leds[NUM_LEDS];

// Time scaling factors for each component
#define TIME_FACTOR_HUE 60
#define TIME_FACTOR_SAT 100
#define TIME_FACTOR_VAL 100

enum ELightModes {
  LightMode_HueWave = 0,
  LightMode_Solid,
  LightMode_Count,
};

ELightModes g_LightMode = LightMode_HueWave;

bool bHueWaveEnable = true;
bool bSolidColorEnable = true;
gh::Flags fEnabledAnimations;

struct SolidModulation {
  bool Enabled;
  float HueDepth;
  uint16_t HueRate;
  float SatDepth;
  uint16_t SatRate;
  float ValDepth;
  uint16_t ValRate;
};

gh::Flag SendDebug;

int solid_r = 128, solid_g = 0, solid_b = 0;
SolidModulation solidModulationSettings;
gh::Flag SolidModulationChanged;

PGM_P BrightnessStr PROGMEM = "Brightness";
PGM_P EnabledAnimationStr PROGMEM  = "EnabledAnimation";
PGM_P SwitchIntervalStr PROGMEM  = "SwitchInterval";
PGM_P DeviceNameStr PROGMEM  = "DeviceName";
PGM_P SolidModulationStr PROGMEM  = "SolidModulation";

gh::Flags FlagsFromString(const String& s) {
  gh::Flags result;
  int last = min(16u, s.length());
  for (int i = 0; i < last; i++) result.write(i, s[i] != '0');
  return result;
}

void onBrightnessChanged(gh::Build& b) {
  char buf[128];
  int val = b.value.toInt();
  sprintf_P(buf, PSTR("Set brigthness to %d"), val);
  Serial.println(buf);
  hub.sendCLI(buf);
  FastLED.setBrightness(val);
}

void onConnectClicked(gh::Build& b) {
  String ssid = data.get("ssid").toString();
  String pass = data.get("pass").toString();
  WiFiConnector.closeAP(true);
  WiFiConnector.connect(ssid, pass);

  char buf[128];
  sprintf_P(buf, PSTR("Connect to %s"), ssid.c_str());
  Serial.println(buf);
  hub.sendCLI(buf);
}

// билдер
void build(gh::Builder& b) {
  b.Menu("Calibrate");

  b.Title(F("Common"));
  b.Slider_(BrightnessStr, &data).label(FPSTR(BrightnessStr)).attach(onBrightnessChanged).range(0, 255, 1);
  if (b.Flags_(FPSTR(EnabledAnimationStr), &fEnabledAnimations).label(F("Enabled animation")).text(F("Hue Wave;Solid Color")).click()) {
    Serial.print(fEnabledAnimations.toString());
    data[FPSTR(EnabledAnimationStr)] = fEnabledAnimations.toString();
  };

  b.Slider_(FPSTR(SwitchIntervalStr), &data).label(F("Switch interval")).range(1, 60, 0.5).suffix("sec");

  // b.Title("Hue Wave");
  // b.Switch(&bHueWaveEnable);

  b.Title(F("Solid Color"));
  bool solidSliderClicked = (b.Slider_("SolidR", &data).range(0, 255, 1).label("R").click())
                            || (b.Slider_("SolidG", &data).range(0, 255, 1).label("G").click())
                            || (b.Slider_("SolidB", &data).range(0, 255, 1).label("B").click());

  b.Color_("SolidColor").value(((solid_r & 0xff) << 16) | ((solid_g & 0xff) << 8) | (solid_b & 0xff));
  if (solidSliderClicked) {
    solid_r = data.get("SolidR");
    solid_g = data.get("SolidG");
    solid_b = data.get("SolidB");
    hub.update("SolidColor").value(((solid_r & 0xff) << 16) | ((solid_g & 0xff) << 8) | (solid_b & 0xff));
  }

  bool &modEnabled = solidModulationSettings.Enabled;
  b.Switch(&solidModulationSettings.Enabled).label(F("Modulate")).attach(&SolidModulationChanged);
  b.show(modEnabled);
  b.Slider(&solidModulationSettings.HueDepth).label(F("Hue Depth")).range(0, 1.0, 0.001, 3).attach(&SolidModulationChanged);
  b.Slider(&solidModulationSettings.HueRate).label(F("Hue Rate")).range(0, 1000, 10).attach(&SolidModulationChanged);
  b.Slider(&solidModulationSettings.SatDepth).label(F("Saturation Depth")).range(0, 1.0, 0.001, 3).attach(&SolidModulationChanged);
  b.Slider(&solidModulationSettings.SatRate).label(F("Saturation Rate")).range(0, 1000, 10).attach(&SolidModulationChanged);
  b.Slider(&solidModulationSettings.ValDepth).label(F("Value Depth")).range(0, 1.0, 0.001, 3).attach(&SolidModulationChanged);
  b.Slider(&solidModulationSettings.ValRate).label(F("Value Rate")).range(0, 1000, 10).attach(&SolidModulationChanged);
  b.Button().label(F("Debug")).attach(&SendDebug);
  b.show(true);

  b.Title(F("Device"));
  b.Input_(FPSTR(DeviceNameStr), &data).label(F("Device name"));
  b.Title("Network");
  b.Input_("ssid", &data);
  b.Pass_("pass", &data);
  b.Button().label("Connect").attach(&onConnectClicked);
}

void setup() {

#ifdef GH_ESP_BUILD

  hub.mqtt.config("test.mosquitto.org", 1883);  // + MQTT

  // ИЛИ

  // режим точки доступа
  // WiFi.mode(WIFI_AP);
  // WiFi.softAP("My Hub");
  // Serial.println(WiFi.softAPIP());
#endif
  LittleFS.begin();
  data.begin();
  if (!data.get(FPSTR(BrightnessStr)).valid()) {
    data[FPSTR(BrightnessStr)] = 128;
  }

  if (!data.get(FPSTR(EnabledAnimationStr)).valid()) {

    data[FPSTR(EnabledAnimationStr)] = gh::Flags(0xffff).toString();
  }
  if (!data.get(FPSTR(SwitchIntervalStr)).valid()) {
    data[FPSTR(SwitchIntervalStr)] = 5.0;
  }
  if (!data.get("SolidR").valid()) {
    data["SolidR"] = 255;
  }
  if (!data.get("SolidG").valid()) {
    data["SolidG"] = 83;
  }
  if (!data.get("SolidB").valid()) {
    data["SolidB"] = 6;
  }

  solid_r = data.get("SolidR");
  solid_g = data.get("SolidG");
  solid_b = data.get("SolidB");
  
  if (!data[FPSTR(SolidModulationStr)].decodeB64(&solidModulationSettings, sizeof(solidModulationSettings))) {
    memset(&solidModulationSettings, 0, sizeof(solidModulationSettings));
    data[FPSTR(SolidModulationStr)] = pairs::Value(&solidModulationSettings, sizeof(solidModulationSettings));
  }

  String deviceName = data.get(FPSTR(DeviceNameStr));

  const __FlashStringHelper* backlight = F("Backlight");

  hub.config(F("MyDevices"), deviceName.length() == 0 ? backlight : String(backlight) + "-" + deviceName, "f2bd");  // user-circle icon
  hub.setVersion(F("piroxiljin/mirror-backlight@" VERSION));
  hub.onBuild(build);
  hub.begin();


  while (!Serial && millis() < 3000) {};
  Serial.begin(115200);
  Serial.println(F("Ready version " VERSION));

  String ssid = data.get("ssid").toString();
  String pass = data.get("pass").toString();

  WiFiConnector.onConnect([]() {
    Serial.print("Connected. Local IP: ");
    Serial.println(WiFi.localIP());
  });
  WiFiConnector.onError([]() {
    Serial.print("WiFi error. AP IP: ");
    Serial.println(WiFi.softAPIP());
  });

  WiFiConnector.connect(ssid, pass);

  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);  // GRB ordering is assumed
  uint8_t bright = data.get(FPSTR(BrightnessStr));
  FastLED.setBrightness(bright);

  fEnabledAnimations = FlagsFromString(data.get(FPSTR(EnabledAnimationStr)));
}

void solidColorAnimation() {
  uint32_t ms = millis();
  
	double hue, saturation, value;
  RGBConverter::RgbToHsv(solid_r, solid_g, solid_b, hue, saturation, value);
  hue *= 255;
  saturation *= 255;
  value *= 255;

  if (solidModulationSettings.Enabled) {
    hue += ((inoise16(ms * solidModulationSettings.HueRate, 0, 0) >> 8) - 128) * solidModulationSettings.HueDepth;
    saturation += ((inoise16(ms * solidModulationSettings.SatRate, 1000, 1000) >> 8) - 128) * solidModulationSettings.SatDepth;
    value += ((inoise16(ms * solidModulationSettings.ValRate, 2000, 2000) >> 8) - 128) * solidModulationSettings.ValDepth;
  }

  if (SendDebug) {
    char buf[128];
    sprintf_P(buf, PSTR("RGB: %d, %d, %d"), solid_r, solid_g, solid_b);
    Serial.println(buf);
    hub.sendCLI(buf);
    sprintf_P(buf, PSTR("HSV: %f, %f, %f"), hue, saturation, value);
    Serial.println(buf);
    hub.sendCLI(buf);
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(hue, saturation, value);
  }
}

void hueWaveAnimation() {
  uint32_t ms = millis();

  for (int i = 0; i < NUM_LEDS; i++) {
    // Use different noise functions for each LED and each color component
    uint8_t hue = inoise16(ms * TIME_FACTOR_HUE, i * 1000, 0) >> 8;
    uint8_t sat = inoise16(ms * TIME_FACTOR_SAT, i * 2000, 1000) >> 8;
    uint8_t val = inoise16(ms * TIME_FACTOR_VAL, i * 3000, 2000) >> 8;

    // Map the noise to full range for saturation and value
    sat = map(sat, 0, 255, 30, 255);
    val = map(val, 0, 255, 100, 255);

    leds[i] = CHSV(hue, sat, val);
  }
}

void loop() {
  hub.tick();
  WiFiConnector.tick();
  data.tick();

  if (SolidModulationChanged) {
    data[FPSTR(SolidModulationStr)] = pairs::Value(&solidModulationSettings, sizeof(solidModulationSettings));
  }

  static int switchInterval = data.get(FPSTR(SwitchIntervalStr)).toInt() * 1000;
  static gh::Timer modeTimer(switchInterval);

  if (modeTimer) {
    int nextMode = (g_LightMode + 1) % LightMode_Count;
    while (!fEnabledAnimations.get(nextMode) && nextMode != g_LightMode) {
      nextMode = (nextMode + 1) % LightMode_Count;
    }
    g_LightMode = ELightModes(nextMode);

    switchInterval = data.get(FPSTR(SwitchIntervalStr)).toInt() * 1000;
    modeTimer.startInterval(switchInterval);
  }

  switch (g_LightMode) {
    case LightMode_HueWave:
      hueWaveAnimation();
      break;
    case LightMode_Solid:
      solidColorAnimation();
      break;
  }

  FastLED.show();
}
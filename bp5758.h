#include "esphome.h"

#define KHZ 100
static const char *const TAG = "tuya-globe";
#define get_bp5758(id) (*((MyCustomLightOutput *)id))

enum BP5758_COLOR_IDX : uint8_t {
  BLUE = 0,
  GREEN = 1,
  RED = 2,
  WARM_WHITE = 3,
  COLD_WHITE = 4,
};

enum BP5758_ADDRESS : uint8_t {
  OUTPUT_SLEEP = 0b10000110,
  OUTPUT_1_TO_5_ENABLEMENT = 0b10010000,
  OUTPUT_1_RANGE_SETTING = 0b10010001,
  OUTPUT_2_RANGE_SETTING = 0b10010010,
  OUTPUT_3_RANGE_SETTING = 0b10010011,
  OUTPUT_4_RANGE_SETTING = 0b10010100,
  OUTPUT_5_RANGE_SETTING = 0b10010101,
  OUTPUT_1_GRAYSCALE_SETTING = 0b10010110,
  OUTPUT_2_GRAYSCALE_SETTING = 0b10011000,
  OUTPUT_3_GRAYSCALE_SETTING = 0b10011010,
  OUTPUT_4_GRAYSCALE_SETTING = 0b10011100,
  OUTPUT_5_GRAYSCALE_SETTING = 0b10011110,
};

void set_channel(uint8_t data[], BP5758_COLOR_IDX byte_idx, float brightness) {
  uint16_t word = brightness * 1023;        // 10 bits
  if (word == 0 && brightness > 0) word++;  // so that 1% is still on
  data[byte_idx * 2 + 7] = word & 0b11111;
  data[byte_idx * 2 + 8] = word >> 5;
}

class MyCustomLightOutput : public Component, public LightOutput {
 protected:
  uint8_t sda;
  uint8_t scl;
  bool constant_brightness = true;
  bool color_interlock = true;

 public:
  bool use_dynamic_range = true;
  MyCustomLightOutput(uint8_t sda_, uint8_t scl_) {
    this->sda = sda_;
    this->scl = scl_;
  }
  void set_constant_brightness(bool constant_brightness_) {
    this->constant_brightness = constant_brightness_;
  }
  void set_color_interlock(bool color_interlock_) {
    this->color_interlock = color_interlock_;
  }
  void setup() override {
    pinMode(this->sda, OUTPUT);
    pinMode(this->scl, OUTPUT);
    end_i2c();
  }

  LightTraits get_traits() override {
    auto traits = LightTraits();
    if (this->color_interlock)
      traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::COLD_WARM_WHITE});
    else
      traits.set_supported_color_modes({light::ColorMode::RGB_COLD_WARM_WHITE});
    traits.set_min_mireds(153);
    traits.set_max_mireds(500);

    return traits;
  }

  void dynamic_range(float &bright_percent, uint8_t &mA_max) {
    if (!this->use_dynamic_range) return;
    if (bright_percent == 0) {
      // by setting the range to zero now, flicker is avoided when the light is turned on 
      mA_max = 0;
    } else {
      float bright_mA = bright_percent * mA_max;
      float brightness_target = 0.5; // set the range so the grayscale is half of max
      int mA_target = bright_mA / brightness_target;
      mA_target = constrain(mA_target, 1, mA_max);
      bright_percent = bright_mA / mA_target;
      if (mA_max>30){
        // the maximum is 90mA. Also b[7] (128) will be ignored by the chip (see Byte2).
        mA_max = constrain(mA_max, 1, 90);
        // b[6] is 30mA instead of 64mA, see Byte2 in datasheet
        mA_max -= 30;
        mA_max |= 0b01000000;
      }
      mA_max = mA_target;
      // ESP_LOGW(TAG, "mA_max:%d\tbright_percent:%f", mA_max, bright_percent);
    }
  }
  void write_state(LightState *state) override {
    float red, green, blue, cold_white, warm_white;
    state->current_values_as_rgbww(&red, &green, &blue, &cold_white, &warm_white, this->constant_brightness);
    // ESP_LOGW(TAG, "red:%f\tgreen:%f\tblue:%f\tcold:%f\twarm:%f",red, green, blue, cold_white, warm_white );

    uint8_t red_max = 0b00010000, green_max = 0b00010000, blue_max = 0b00010000, cold_white_max = 0b00011010, warm_white_max = 0b00011010;

    dynamic_range(red, red_max);
    dynamic_range(green, green_max);
    dynamic_range(blue, blue_max);
    dynamic_range(cold_white, cold_white_max);
    dynamic_range(warm_white, warm_white_max);

    uint8_t data_size = 17;
    uint8_t data[data_size] = {BP5758_ADDRESS::OUTPUT_1_TO_5_ENABLEMENT, 0b00011111, blue_max, green_max, red_max, warm_white_max, cold_white_max};
    set_channel(data, BP5758_COLOR_IDX::RED, red);
    set_channel(data, BP5758_COLOR_IDX::GREEN, green);
    set_channel(data, BP5758_COLOR_IDX::BLUE, blue);
    set_channel(data, BP5758_COLOR_IDX::COLD_WHITE, cold_white);
    set_channel(data, BP5758_COLOR_IDX::WARM_WHITE, warm_white);
    // ESP_LOGW(TAG, "warm_white:%f\twarm_white:%u\twarm_white_max:%u",warm_white,data[BP5758_COLOR_IDX::WARM_WHITE+1]*32+data[BP5758_COLOR_IDX::WARM_WHITE], warm_white_max );

    this->send(data, data_size);

    bool isOff = red == 0 && green == 0 && blue == 0 && cold_white == 0 && warm_white == 0;
    if (isOff) {
      this->send_sleep();
    }
  }

 protected:
  void send_sleep() {
    uint8_t data_size = 1;
    uint8_t data[data_size] = {BP5758_ADDRESS::OUTPUT_SLEEP};
    this->send(data, data_size);
  }

  void wait() {
    // delayMicroseconds(1000 / KHZ / 2);
    delayMicroseconds(2);
  }
  void start_i2c() {
    digitalWrite(this->sda, LOW);
    this->wait();
    digitalWrite(this->scl, LOW);
    this->wait();
  }
  void end_i2c() {
    digitalWrite(this->scl, HIGH);
    this->wait();
    digitalWrite(this->sda, HIGH);
    this->wait();
  }
  void wait_for_ack() {
    pinMode(this->sda, INPUT);
    digitalWrite(this->scl, HIGH);
    this->wait();
    digitalWrite(this->scl, LOW);
    this->wait();
    pinMode(this->sda, OUTPUT);
  }
  void send(uint8_t data[], uint8_t size) {
    this->start_i2c();
    for (uint8_t i = 0; i < size; i++) {
      uint8_t the_byte = data[i];
      for (int bit_idx = 7; bit_idx >= 0; bit_idx--) {
        bool bit = bitRead(the_byte, bit_idx);
        digitalWrite(this->sda, bit);
        this->wait();
        digitalWrite(this->scl, HIGH);
        this->wait();
        digitalWrite(this->scl, LOW);
        this->wait();
      }
      this->wait_for_ack();
    }
    this->end_i2c();
  }
};

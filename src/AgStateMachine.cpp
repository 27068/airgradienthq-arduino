#include "AgStateMachine.h"
#include "AgOledDisplay.h"

#define LED_TEST_BLINK_DELAY 50   /** ms */
#define LED_FAST_BLINK_DELAY 250  /** ms */
#define LED_SLOW_BLINK_DELAY 1000 /** ms */
#define LED_SHORT_BLINK_DELAY 500 /** ms */
#define LED_LONG_BLINK_DELAY 2000 /** ms */

#define SENSOR_CO2_CALIB_COUNTDOWN_MAX 5 /** sec */

bool StateMachine::RGB::operator==(const RGB &other) const {
  return r == other.r && g == other.g && b == other.b;
}

bool StateMachine::RGB::operator!=(const RGB &other) const {
  return !(*this == other);
}

const StateMachine::RGB StateMachine::RGB_COLOR_B = {0, 0, 255}; /** Blue */
const StateMachine::RGB StateMachine::RGB_COLOR_G = {0, 255, 0};   /** Green */
const StateMachine::RGB StateMachine::RGB_COLOR_Y = {255, 255, 0}; /** Yellow */
const StateMachine::RGB StateMachine::RGB_COLOR_O = {255, 128, 0}; /** Orange */
const StateMachine::RGB StateMachine::RGB_COLOR_R = {255, 0, 0};   /** Red */
const StateMachine::RGB StateMachine::RGB_COLOR_P = {180, 0, 255}; /** Purple */
const StateMachine::RGB StateMachine::RGB_COLOR_CLEAR = {0, 0, 0}; /** No color */
const StateMachine::RGB StateMachine::RGB_COLOR_W = {255, 255, 255}; /** White */

// CO2 and PM2.5 are calculated to be spaced evenly on an exponential scale.
const std::vector<StateMachine::ColumnSetting> StateMachine::LED_BAR_COMBO_COLUMNS = {
    {Measurements::Temperature, 1, false, true, {15, 26, 30, 34, 38}},
    {Measurements::Humidity, 1, false, true, {30, 60, 70, 80, 90}},
    {static_cast<Measurements::MeasurementType>(-1), 1, false, false, {}},
    {Measurements::CO2, 2, false, false, {526, 654, 813, 1010, 1256, 1561, 1941, 2413, 3000}},
    {static_cast<Measurements::MeasurementType>(-1), 1, false, false, {}},
    {Measurements::PM25, 2, true, false, {6, 10, 16, 25, 40, 63, 100, 158, 250}},
    {static_cast<Measurements::MeasurementType>(-1), 1, false, false, {}},
    {Measurements::TVOC, 1, true, false, {100, 200, 300, 400}},
    {Measurements::NOx, 1, true, false, {100, 200, 300, 400}},
};

// CO2 level 3 is repeated so we can skip the pattern for that level
const std::map<Measurements::MeasurementType, std::vector<int>> StateMachine::LED_BAR_LEVELS_LEGACY = {
    {Measurements::CO2, {600, 800, 1000, 1000, 1250, 1500, 1750, 2000, 3000}},
    {Measurements::PM25, {5, 9, 20, 35, 45, 55, 100, 125, 225}}};

/**
 * @brief Animation LED bar with color
 *
 * @param r
 * @param g
 * @param b
 */
void StateMachine::ledBarSingleLedAnimation(uint8_t r, uint8_t g, uint8_t b) {
  if (ledBarAnimationCount < 0) {
    ledBarAnimationCount = 0;
    ag->ledBar.setColor(r, g, b, ledBarAnimationCount);
  } else {
    ledBarAnimationCount++;
    if (ledBarAnimationCount >= ag->ledBar.getNumberOfLeds()) {
      ledBarAnimationCount = 0;
    }
    ag->ledBar.setColor(r, g, b, ledBarAnimationCount);
  }
}

/**
 * @brief LED status blink with delay
 *
 * @param ms Miliseconds
 */
void StateMachine::ledStatusBlinkDelay(uint32_t ms) {
  ag->statusLed.setOn();
  delay(ms);
  ag->statusLed.setOff();
  delay(ms);
}

/**
 * @brief Led bar show PM or CO2 led color status
 *
 * @param statusLedColor
 */
void StateMachine::sensorhandleLeds(RGB statusLedColor) {
  switch (config.getLedBarMode()) {
  case LedBarMode::LedBarModeCO2:
    handleLedsLegacy(statusLedColor, Measurements::CO2, [&]() {
      return round(value.getAverage(Measurements::CO2));
    });
    break;
  case LedBarMode::LedBarModePm:
    handleLedsLegacy(statusLedColor, Measurements::PM25, [&]() {
      const auto average = config.hasSensorSHT && config.isPMCorrectionEnabled() ? value.getCorrectedPM25(true) : value.getAverage(Measurements::PM25);
      return round(average);
    });
    break;
  case LedBarMode::LedBarModeCombo:
    comboHandleLeds(statusLedColor, [&](Measurements::MeasurementType type) {
      return value.getAverage(type);
    });
    break;
  default:
    ag->ledBar.clear();
    break;
  }
}

void StateMachine::comboHandleLeds(RGB statusLedColor, std::function<float(Measurements::MeasurementType)> getValueFunc) {
  uint offset = 0;
  for (const auto &column : LED_BAR_COMBO_COLUMNS) {
    // Override first group with status LED otherwise there's not enough LEDs
    if (offset == 0 && statusLedColor != RGB_COLOR_CLEAR) {
      setColorWithPadding(offset, column.length, statusLedColor, 1, false);
    }
    else if (column.measurementType == -1) {
      // Empty column
      setColor(offset, RGB_COLOR_CLEAR, column.length);
    }
    else {
      fillColumn(offset, column, getValueFunc(column.measurementType));
    }

    offset += column.length;
  }
}

/**
 * @brief Fills a column with color based on the measurement value.
 *
 * @param offset The starting index of the column
 * @param column The column settings including measurement type, length, alignment, extended colors, and cutoffs
 * @param value The measurement value
 *
 * @details
 * The LED strip is divided into columns, each representing a measurement type.
 * The column starts at the given offset and has a length of `column.length`.
 * The measurement value is compared against the cutoffs to determine the
 * level, which then determines the color pattern. As the level increases, for
 * each color it will fill one LED, then the next, until the column is filled,
 * after which it will continue to the next color in the sequence.
 * E.g. G, GG, GGG, Y, YY, YYY.
 *
 * It will also set the empty LEDs to clear any color that was previously set.
 *
 * Extended colors means it will start at blue rather than green, for values
 * that go below the normal range.
 */
void StateMachine::fillColumn(uint offset, ColumnSetting column, float value) {
  static const std::vector<RGB> colorSequence = {RGB_COLOR_B, RGB_COLOR_G, RGB_COLOR_Y, RGB_COLOR_O, RGB_COLOR_R, RGB_COLOR_P};

  const uint level = std::distance(column.cutoffs.begin(), std::upper_bound(column.cutoffs.begin(), column.cutoffs.end(), value));
  const auto colorIndex = (column.extendedColors ? 0 : 1) + level / column.length;
  const auto color = colorSequence.at(min(colorIndex, colorSequence.size() - 1));
  const auto numLeds = (level % column.length) + 1;
  setColorWithPadding(offset, column.length, color, numLeds, column.alignRight);
}

void StateMachine::handleLedsLegacy(RGB statusLedColor, Measurements::MeasurementType measurementType,
  std::function<int()> getValueFunc) {
  static const std::vector<RGB> colorSequence = {RGB_COLOR_G, RGB_COLOR_Y, RGB_COLOR_O, RGB_COLOR_R, RGB_COLOR_P};

  const auto value = getValueFunc();

  // Calculate level
  const auto levels = LED_BAR_LEVELS_LEGACY.at(measurementType);
  uint level = std::distance(levels.begin() , std::lower_bound(levels.begin(), levels.end(), value));
  const uint maxLevel = colorSequence.size() * 2 - 1;
  // This should never happen but limit it for safety
  level = std::min(level, maxLevel);

  // Skip status LED and unused LED
  const uint LED_OFFSET = 2;
  // Offset should not exceed the number of LEDs but limit it just in case
  const auto columnSize = std::max(0u, ag->ledBar.getNumberOfLeds() - LED_OFFSET);

  // The last level is special, fill with purple and red
  if (level == maxLevel) {
    for (auto i = 0; i < columnSize; i++) {
      setColor(LED_OFFSET + i, i % 2 ? RGB_COLOR_R : RGB_COLOR_P);
    }
  } else {
    // CO2 skips a level
    const auto numLeds = measurementType == Measurements::CO2 && level >= 3 ? level : level + 1;
    setColorWithPadding(LED_OFFSET, columnSize, colorSequence.at(level / 2), numLeds, true);
  }

  // Set status LED and unused LED
  setColor(0, statusLedColor);
  setColor(1, RGB_COLOR_CLEAR);
}

/**
 * @brief Sets the color of a range of LEDs with padding to the size of the column.
 *
 * @param offset The starting index of the range
 * @param columnSize The total size of the column
 * @param color The color to set the LEDs to
 * @param length The number of LEDs to set the color for
 * @param alignRight If true, the color will be aligned to the right of the column
 */
void StateMachine::setColorWithPadding(uint offset, uint columnSize, RGB color, uint length, bool alignRight) {
  uint paddingOffset;
  uint colorOffset;
  auto paddingLength = columnSize - length;

  if (alignRight) {
    paddingOffset = 0;
    colorOffset = paddingLength;
  }
  else {
    colorOffset = 0;
     paddingOffset = length;
  }

  setColor(offset + paddingOffset, RGB_COLOR_CLEAR, paddingLength);
  setColor(offset + colorOffset, color, length);
}

/**
 * @brief Sets the color of a range of LEDs.
 *
 * @param offset The starting index of the range
 * @param color The color to set the LEDs to
 * @param length The number of LEDs to set the color for
 */
void StateMachine::setColor(uint offset, RGB color, uint length) {
  for (auto i = 0; i < length; i++) {
    setColor(offset + i, color);
  }
}

/**
 * @brief Sets the color of a LED.
 *
 * @param ledNum The LED number to set the color for
 * @param color The color to set the LED to
 *
 * @note This is a convenience wrapper to allow the RGB struct to be used instead of separate r, g, b parameters.
 */
void StateMachine::setColor(int ledNum, RGB color)
{
  ag->ledBar.setColor(color.r, color.g, color.b, ledNum);
}

void StateMachine::co2Calibration(void) {
  if (config.isCo2CalibrationRequested() && config.hasSensorS8) {
    logInfo("CO2 Calibration");

    /** Count down to 0 then start */
    for (int i = 0; i < SENSOR_CO2_CALIB_COUNTDOWN_MAX; i++) {
      if (ag->isOne() || (ag->isPro4_2()) || ag->isPro3_3()) {
        String str =
            "after " + String(SENSOR_CO2_CALIB_COUNTDOWN_MAX - i) + " sec";
        disp.setText("Start CO2 calib", str.c_str(), "");
      } else if (ag->isBasic()) {
        String str = String(SENSOR_CO2_CALIB_COUNTDOWN_MAX - i) + " sec";
        disp.setText("CO2 Calib", "after", str.c_str());
      } else {
        logInfo("Start CO2 calib after " +
                String(SENSOR_CO2_CALIB_COUNTDOWN_MAX - i) + " sec");
      }
      delay(1000);
    }

    if (ag->s8.setBaselineCalibration()) {
      if (ag->isOne() || (ag->isPro4_2()) || ag->isPro3_3()) {
        disp.setText("Calibration", "success", "");
      } else if (ag->isBasic()) {
        disp.setText("CO2 Calib", "success", "");
      } else {
        logInfo("CO2 Calibration: success");
      }
      delay(1000);
      if (ag->isOne() || (ag->isPro4_2()) || ag->isPro3_3() || ag->isBasic()) {
        disp.setText("Wait for", "calib done", "...");
      } else {
        logInfo("CO2 Calibration: Wait for calibration finish...");
      }

      /** Count down wait for finish */
      int count = 0;
      while (ag->s8.isBaseLineCalibrationDone() == false) {
        delay(1000);
        count++;
      }
      if (ag->isOne() || (ag->isPro4_2()) || ag->isPro3_3() || ag->isBasic()) {
        String str = "after " + String(count);
        disp.setText("Calib done", str.c_str(), "sec");
      } else {
        logInfo("CO2 Calibration: finish after " + String(count) + " sec");
      }
      delay(2000);
    } else {
      if (ag->isOne() || (ag->isPro4_2()) || ag->isPro3_3()) {
        disp.setText("Calibration", "failure!!!", "");
      } else if (ag->isBasic()) {
        disp.setText("CO2 calib", "failure!!!", "");
      } else {
        logInfo("CO2 Calibration: failure!!!");
      }
      delay(2000);
    }
  }

  if (config.getCO2CalibrationAbcDays() >= 0 && config.hasSensorS8) {
    int newHour = config.getCO2CalibrationAbcDays() * 24;
    int curHour = ag->s8.getAbcPeriod();
    if (curHour != newHour) {
      String resultStr = "failure";
      if (ag->s8.setAbcPeriod(config.getCO2CalibrationAbcDays() * 24)) {
        resultStr = "successful";
      }
      String fromStr = String(curHour / 24) + " days";
      if (curHour == 0) {
        fromStr = "off";
      }
      String toStr = String(config.getCO2CalibrationAbcDays()) + " days";
      if (config.getCO2CalibrationAbcDays() == 0) {
        toStr = "off";
      }
      String msg =
          "Setting S8 from " + fromStr + " to " + toStr + " " + resultStr;
      logInfo(msg);
    }
  } else {
    logWarning("CO2 S8 not available, set 'abcDays' ignored");
  }
}

void StateMachine::ledBarTest(void) {
  if (config.isLedBarTestRequested()) {
    if (ag->isOne()) {
      ag->ledBar.clear();
      if (config.getCountry() == "TH") {
        uint32_t tstart = millis();
        logInfo("Start run LED test for 2 min");
        while (1) {
          ledBarRunTest();
          uint32_t ms = (uint32_t)(millis() - tstart);
          if (ms >= (60 * 1000 * 2)) {
            logInfo("LED test after 2 min finish");
            break;
          }
        }
      } else {
        ledBarRunTest();
      }
    } else if (ag->isOpenAir()) {
      ledBarRunTest();
    }
  }
}

void StateMachine::ledBarPowerUpTest(void) {
  if (ag->isOne()) {
    ag->ledBar.clear();
  }
  ledBarRunTest();
}

void StateMachine::ledBarRunTest(void) {
  if (ag->isOne()) {
    disp.setText("LED Test", "running", ".....");
    runLedTest(RGB_COLOR_R);
    ag->ledBar.show();
    delay(1000);
    runLedTest(RGB_COLOR_G);
    ag->ledBar.show();
    delay(1000);
    runLedTest(RGB_COLOR_B);
    ag->ledBar.show();
    delay(1000);
    runLedTest(RGB_COLOR_W);
    ag->ledBar.show();
    delay(1000);
    runLedTest(RGB_COLOR_CLEAR);
    ag->ledBar.show();
    delay(1000);
    testLegacyLeds();
    testComboLeds();
  } else if (ag->isOpenAir()) {
    for (int i = 0; i < 100; i++) {
      ag->statusLed.setOn();
      delay(LED_TEST_BLINK_DELAY);
      ag->statusLed.setOff();
      delay(LED_TEST_BLINK_DELAY);
    }
  }
}

void StateMachine::testComboLeds(void) {
  for (const auto &column : LED_BAR_COMBO_COLUMNS)
  {
    // Test a value lower than the first cutoff
    testLedColumn(column.measurementType, 0);
    ag->ledBar.show();
    delay(1000);

    for (const auto value : column.cutoffs) {
      testLedColumn(column.measurementType, value);
      ag->ledBar.show();
      delay(1000);
    }
  }
}

void StateMachine::testLedColumn(Measurements::MeasurementType type, float value) {
  comboHandleLeds(RGB_COLOR_W, [&](Measurements::MeasurementType innerType) {
     return innerType == type ? value : 0;
  });
}

void StateMachine::testLegacyLeds(void) {
  for (const auto &x : LED_BAR_LEVELS_LEGACY) {
    for (const auto value : x.second) {
      testLegacyLedType(x.first, value);
      ag->ledBar.show();
      delay(1000);
    }

    // Test a value higher than the last cutoff
    testLegacyLedType(x.first, INT_MAX);
    delay(1000);
  }
}

void StateMachine::testLegacyLedType(Measurements::MeasurementType type, int value) {
  handleLedsLegacy(RGB_COLOR_W, type, [&]() {
    return value;
  });
}

void StateMachine::runLedTest(RGB color) {
  setColor(0, color, ag->ledBar.getNumberOfLeds());
}

/**
 * @brief Construct a new Ag State Machine:: Ag State Machine object
 *
 * @param disp OledDisplay
 * @param log Serial Stream
 * @param value Measurements
 * @param config Configuration
 */
StateMachine::StateMachine(OledDisplay &disp, Stream &log, Measurements &value,
                           Configuration &config)
    : PrintLog(log, "StateMachine"), disp(disp), value(value), config(config) {}

StateMachine::~StateMachine() {}

/**
 * @brief OLED display show content from state value
 *
 * @param state
 */
void StateMachine::displayHandle(AgStateMachineState state) {
  // Ignore handle if not support display
  if (!(ag->isOne() || (ag->isPro4_2()) || ag->isPro3_3() || ag->isBasic())) {
    if (state == AgStateMachineCo2Calibration) {
      co2Calibration();
    }
    return;
  }

  if (state > AgStateMachineNormal) {
    logError("displayHandle: State invalid");
    return;
  }

  dispState = state;

  switch (state) {
  case AgStateMachineWiFiManagerMode:
  case AgStateMachineWiFiManagerPortalActive: {
    if (wifiConnectCountDown >= 0) {
      if (ag->isBasic()) {
        String ssid = "\"airgradient-" + ag->deviceId() + "\"    " +
                      String(wifiConnectCountDown) + String("s");
        disp.setText("Connect tohotspot:", ssid.c_str(), "");
      } else {
        String line1 = String(wifiConnectCountDown) + "s to connect";
        String line2 = "to WiFi hotspot:";
        String line3 = "\"airgradient-";
        String line4 = ag->deviceId() + "\"";
        disp.setText(line1, line2, line3, line4);
      }
      wifiConnectCountDown--;
    }
    break;
  }
  case AgStateMachineWiFiManagerStaConnecting: {
    disp.setText("Trying to", "connect to WiFi", "...");
    break;
  }
  case AgStateMachineWiFiManagerStaConnected: {
    disp.setText("WiFi connection", "successful", "");
    break;
  }
  case AgStateMachineWiFiOkServerConnecting: {
    if (ag->isBasic()) {
      disp.setText("Connecting", "to", "Server...");
    } else {
      disp.setText("Connecting to", "Server", "...");
    }

    break;
  }
  case AgStateMachineWiFiOkServerConnected: {
    disp.setText("Server", "connection", "successful");
    break;
  }
  case AgStateMachineWiFiManagerConnectFailed: {
    disp.setText("WiFi not", "connected", "");
    break;
  }
  case AgStateMachineWiFiOkServerConnectFailed: {
    // displayShowText("Server not", "reachable", "");
    break;
  }
  case AgStateMachineWiFiOkServerOkSensorConfigFailed: {
    if (ag->isBasic()) {
      disp.setText("Monitor", "not on", "dashboard");
    } else {
      disp.setText("Monitor not", "setup on", "dashboard");
    }
    break;
  }
  case AgStateMachineWiFiLost: {
    disp.showDashboard(OledDisplay::DashBoardStatusWiFiIssue);
    break;
  }
  case AgStateMachineServerLost: {
    disp.showDashboard(OledDisplay::DashBoardStatusServerIssue);
    break;
  }
  case AgStateMachineSensorConfigFailed: {
    if (addToDashBoard) {
      uint32_t ms = (uint32_t)(millis() - addToDashboardTime);
      if (ms >= 5000) {
        addToDashboardTime = millis();
        if (addToDashBoardToggle) {
          disp.showDashboard(OledDisplay::DashBoardStatusAddToDashboard);
        } else {
          disp.showDashboard(OledDisplay::DashBoardStatusDeviceId);
        }
        addToDashBoardToggle = !addToDashBoardToggle;
      }
    } else {
      disp.showDashboard();
    }
    break;
  }
  case AgStateMachineNormal: {
    if (config.isOfflineMode()) {
      disp.showDashboard(
          OledDisplay::DashBoardStatusOfflineMode);
    } else {
      disp.showDashboard();
    }
    break;
  }
  case AgStateMachineCo2Calibration:
    co2Calibration();
    break;
  default:
    break;
  }
}

/**
 * @brief OLED display show content as previous state updated
 *
 */
void StateMachine::displayHandle(void) { displayHandle(dispState); }

/**
 * @brief Update status add to dashboard
 *
 */
void StateMachine::displaySetAddToDashBoard(void) {
  if (addToDashBoard == false) {
    addToDashboardTime = 0;
    addToDashBoardToggle = true;
  }
  addToDashBoard = true;
}

void StateMachine::displayClearAddToDashBoard(void) { addToDashBoard = false; }

/**
 * @brief Set WiFi connection coundown on dashboard
 *
 * @param count Seconds
 */
void StateMachine::displayWiFiConnectCountDown(int count) {
  wifiConnectCountDown = count;
}

/**
 * @brief Init before start LED bar animation
 *
 */
void StateMachine::ledAnimationInit(void) { ledBarAnimationCount = -1; }

/**
 * @brief Handle LED from state, only handle LED if board type is: One Indoor or
 * Open Air
 *
 * @param state
 */
void StateMachine::handleLeds(AgStateMachineState state) {
  /** Ignore if board type if not ONE_INDOOR or OPEN_AIR_OUTDOOR */
  if ((ag->getBoardType() != BoardType::ONE_INDOOR) &&
      (ag->getBoardType() != BoardType::OPEN_AIR_OUTDOOR)) {
    return;
  }

  if (state > AgStateMachineNormal) {
    logError("ledHandle: state invalid");
    return;
  }

  ledState = state;
  switch (state) {
  case AgStateMachineWiFiManagerMode: {
    /** In WiFi Manager Mode */
    /** Turn LED OFF */
    /** Turn middle LED Color */
    if (ag->isOne()) {
      ag->ledBar.clear();
      ag->ledBar.setColor(0, 0, 255, ag->ledBar.getNumberOfLeds() / 2);
    } else {
      ag->statusLed.setToggle();
    }
    break;
  }
  case AgStateMachineWiFiManagerPortalActive: {
    /** WiFi Manager has connected to mobile phone */
    if (ag->isOne()) {
      ag->ledBar.clear();
      ag->ledBar.setColor(0, 0, 255);
    } else {
      ag->statusLed.setOn();
    }
    break;
  }
  case AgStateMachineWiFiManagerStaConnecting: {
    /** after SSID and PW entered and OK clicked, connection to WiFI network is
     * attempted */
    if (ag->isOne()) {
      ag->ledBar.clear();
      ledBarSingleLedAnimation(255, 255, 255);
    } else {
      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineWiFiManagerStaConnected: {
    /** Connecting to WiFi worked */
    if (ag->isOne()) {
      ag->ledBar.clear();
      ag->ledBar.setColor(255, 255, 255);
    } else {
      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineWiFiOkServerConnecting: {
    /** once connected to WiFi an attempt to reach the server is performed */
    if (ag->isOne()) {
      ag->ledBar.clear();
      ledBarSingleLedAnimation(0, 255, 0);
    } else {
      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineWiFiOkServerConnected: {
    /** Server is reachable, all ﬁne */
    if (ag->isOne()) {
      ag->ledBar.clear();
      ag->ledBar.setColor(0, 255, 0);
    } else {
      ag->statusLed.setOff();

      /** two time slow blink, then off */
      for (int i = 0; i < 2; i++) {
        ledStatusBlinkDelay(LED_SLOW_BLINK_DELAY);
      }

      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineWiFiManagerConnectFailed: {
    /** Cannot connect to WiFi (e.g. wrong password, WPA Enterprise etc.) */
    if (ag->isOne()) {
      ag->ledBar.clear();
      ag->ledBar.setColor(255, 0, 0);
    } else {
      ag->statusLed.setOff();

      for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
          ledStatusBlinkDelay(LED_FAST_BLINK_DELAY);
        }
        delay(2000);
      }
      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineWiFiOkServerConnectFailed: {
    /** Connected to WiFi but server not reachable, e.g. firewall block/
     * whitelisting needed etc. */
    if (ag->isOne()) {
      ag->ledBar.clear();
      ag->ledBar.setColor(233, 183, 54); /** orange */
    } else {
      ag->statusLed.setOff();
      for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 4; i++) {
          ledStatusBlinkDelay(LED_FAST_BLINK_DELAY);
        }
        delay(2000);
      }
      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineWiFiOkServerOkSensorConfigFailed: {
    /** Server reachable but sensor not configured correctly */
    if (ag->isOne()) {
      ag->ledBar.clear();
      ag->ledBar.setColor(139, 24, 248); /** violet */
    } else {
      ag->statusLed.setOff();
      for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 5; i++) {
          ledStatusBlinkDelay(LED_FAST_BLINK_DELAY);
        }
        delay(2000);
      }
      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineWiFiLost: {
    /** Connection to WiFi network failed credentials incorrect encryption not
     * supported etc. */
    if (ag->isOne()) {
      sensorhandleLeds(RGB_COLOR_R);
    } else {
      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineServerLost: {
    /** Connected to WiFi network but the server cannot be reached through the
     * internet, e.g. blocked by firewall */
    if (ag->isOne()) {
      sensorhandleLeds((const RGB){233, 183, 54});
    } else {
      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineSensorConfigFailed: {
    /** Server is reachable but there is some configuration issue to be fixed on
     * the server side */
    if (ag->isOne()) {
      sensorhandleLeds((const RGB){139, 24, 248});
    } else {
      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineNormal: {
    if (ag->isOne()) {
      sensorhandleLeds(RGB_COLOR_CLEAR);
    } else {
      ag->statusLed.setOff();
    }
    break;
  }
  case AgStateMachineLedBarTest:
    ledBarTest();
    break;
  case AgStateMachineLedBarPowerUpTest:
    ledBarPowerUpTest();
  default:
    break;
  }

  // Show LED bar color
  if (ag->isOne()) {
    ag->ledBar.show();
  }
}

/**
 * @brief Handle LED as previous state updated
 *
 */
void StateMachine::handleLeds(void) { handleLeds(ledState); }

/**
 * @brief Set display state
 *
 * @param state
 */
void StateMachine::setDisplayState(AgStateMachineState state) {
  dispState = state;
}

/**
 * @brief Get current display state
 *
 * @return AgStateMachineState
 */
AgStateMachineState StateMachine::getDisplayState(void) { return dispState; }

/**
 * @brief Set AirGradient instance
 *
 * @param ag Point to AirGradient instance
 */
void StateMachine::setAirGradient(AirGradient *ag) { this->ag = ag; }

/**
 * @brief Get current LED state
 *
 * @return AgStateMachineState
 */
AgStateMachineState StateMachine::getLedState(void) { return ledState; }

void StateMachine::executeCo2Calibration(void) {
  displayHandle(AgStateMachineCo2Calibration);
}

void StateMachine::executeLedBarTest(void) {
  handleLeds(AgStateMachineLedBarTest);
}

void StateMachine::executeLedBarPowerUpTest(void) {
  handleLeds(AgStateMachineLedBarPowerUpTest);
}

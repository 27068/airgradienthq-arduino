#ifndef _AG_STATE_MACHINE_H_
#define _AG_STATE_MACHINE_H_

#include "AgOledDisplay.h"
#include "AgValue.h"
#include "AgConfigure.h"
#include "Main/PrintLog.h"
#include "App/AppDef.h"
#include <map>
#include <functional>

class StateMachine : public PrintLog {
private:
  struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    bool operator==(const RGB &other) const;
    bool operator!=(const RGB &other) const;
  };

  struct ColumnSetting {
    Measurements::MeasurementType measurementType;
    int length;
    bool alignRight;
    bool extendedColors;
    std::vector<int> cutoffs;
  };

  static const RGB RGB_COLOR_B;
  static const RGB RGB_COLOR_G;
  static const RGB RGB_COLOR_Y;
  static const RGB RGB_COLOR_O;
  static const RGB RGB_COLOR_R;
  static const RGB RGB_COLOR_P;
  static const RGB RGB_COLOR_CLEAR;
  static const RGB RGB_COLOR_W;
  static const std::vector<ColumnSetting> LED_BAR_COMBO_COLUMNS;
  static const std::map<Measurements::MeasurementType, std::vector<int>> LED_BAR_LEVELS_LEGACY;

  // AgStateMachineState state;
  AgStateMachineState ledState;
  AgStateMachineState dispState;
  AirGradient *ag;
  OledDisplay &disp;
  Measurements &value;
  Configuration &config;
  bool addToDashBoard = false;
  bool addToDashBoardToggle = false;
  uint32_t addToDashboardTime;
  int wifiConnectCountDown;
  int ledBarAnimationCount;

  void ledBarSingleLedAnimation(uint8_t r, uint8_t g, uint8_t b);
  void ledStatusBlinkDelay(uint32_t delay);
  void sensorhandleLeds(RGB statusLedColor);
  void comboHandleLeds(RGB statusLedColor, std::function<float(Measurements::MeasurementType)> getValueFunc);
  void fillColumn(uint offset, ColumnSetting column, float value);
  void handleLedsLegacy(RGB statusLedColor, Measurements::MeasurementType measurementType,
     std::function<int()> getValueFunc);
  void setColorWithPadding(uint offset, uint columnSize, RGB color, uint length, bool alignRight);
  void setColor(uint offset, RGB color, uint length);
  void setColor(int ledNum, RGB color);
  void co2Calibration(void);
  void ledBarTest(void);
  void ledBarPowerUpTest(void);
  void ledBarRunTest(void);
  void testComboLeds(void);
  void testLedColumn(Measurements::MeasurementType type, float value);
  void testLegacyLeds(void);
  void testLegacyLedType(Measurements::MeasurementType type, int value);
  void runLedTest(RGB color);

public:
  StateMachine(OledDisplay &disp, Stream &log,
                 Measurements &value, Configuration& config);
  ~StateMachine();
  void setAirGradient(AirGradient* ag);
  void displayHandle(AgStateMachineState state);
  void displayHandle(void);
  void displaySetAddToDashBoard(void);
  void displayClearAddToDashBoard(void);
  void displayWiFiConnectCountDown(int count);
  void ledAnimationInit(void);
  void handleLeds(AgStateMachineState state);
  void handleLeds(void);
  void setDisplayState(AgStateMachineState state);
  AgStateMachineState getDisplayState(void);
  AgStateMachineState getLedState(void);
  void executeCo2Calibration(void);
  void executeLedBarTest(void);
  void executeLedBarPowerUpTest(void);
};

#endif /** _AG_STATE_MACHINE_H_ */

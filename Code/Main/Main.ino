#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "webpage.h"

// ====================== CONFIGURATION ======================

// Wi-Fi
const char* ssid = "Car_Controller";
const char* password = "TechBros";
const IPAddress local_ip(192, 168, 4, 1);
const IPAddress gateway(192, 168, 4, 1);
const IPAddress subnet(255, 255, 255, 0);

// Motor Pins
#define ENA 25
#define IN1 26
#define IN2 27
#define ENB 14
#define IN3 12
#define IN4 13

// Ultrasonic Pins
#define SENSOR_A_TRIG 18   // left
#define SENSOR_A_ECHO 19
#define SENSOR_B_TRIG 33   // right
#define SENSOR_B_ECHO 32
#define SENSOR_C_TRIG 4    // center
#define SENSOR_C_ECHO 5

// PWM
#define PWM_FREQ 5000
#define PWM_RES  8
#define PWM_CHANNEL_LEFT  0
#define PWM_CHANNEL_RIGHT 1

// Sensor Constants 
#define SENSOR_TIMEOUT 12000
#define OBSTACLE_CRITICAL 20   // fully stop
#define OBSTACLE_MINIMUM 10    // emergency 
#define SLOW_ZONE 80           // Not used now

// Motor Command IDs
#define MOTOR_STOP 0
#define MOTOR_FORWARD 1
#define MOTOR_BACKWARD 2
#define MOTOR_LEFT 3
#define MOTOR_RIGHT 4
#define MOTOR_SPIN_LEFT 5
#define MOTOR_SPIN_RIGHT 6

// Speed Presets
#define SPEED_FULL 200
#define SPEED_HIGH 170
#define SPEED_MEDIUM 130
#define SPEED_LOW 80
#define SPEED_CREEP 40

// Ramp Configuration
#define RAMP_STEP 10
#define START_BOOST 130

// ====================== DATA STRUCTURES ======================

struct SensorData {
  float distanceA;        // left
  float distanceB;        // right
  float distanceC;        // center 
  float distanceCenter;   // مرادف لـ distanceC للتوافق
  float obstacleAngle;
};

struct MotorState {
  uint8_t leftDirection;
  uint8_t rightDirection;
  uint8_t leftSpeed;
  uint8_t rightSpeed;
  uint8_t leftTargetSpeed;
  uint8_t rightTargetSpeed;
};

struct CarState {
  uint8_t currentMode;
  uint8_t currentMotorCmd;
  uint8_t currentSpeed;
  bool emergencyStop;
  uint32_t lastSensorReadTime;
  uint32_t lastSpeedRampTime;
  uint8_t requestedCmd;    // جديد: لحفظ أمر الحركة القادم من الزر
  uint8_t requestedSpeed;  // جديد: لحفظ السرعة المطلوبة
};

enum ControlMode {
  MODE_IDLE = 0,
  MODE_MANUAL = 1,
  MODE_ASSISTED = 2,
  MODE_AUTONOMOUS = 3,
  MODE_STUNT = 4,
  MODE_FOLLOW_ME = 5
};

// Global Variables
SensorData sensors = {0, 0, 0, 0, 0};
MotorState motorState = {0, 0, 0, 0, 0, 0};
CarState carState = {0, MOTOR_STOP, SPEED_FULL, false, 0, 0, MOTOR_STOP, SPEED_FULL};
uint8_t lastTurnDirection = 0;
uint32_t stuntStartTime = 0;
uint8_t stuntPhase = 0;
AsyncWebServer server(80);

float filterA[3] = {0}, filterB[3] = {0}, filterC[3] = {0};
uint8_t filterIndex = 0;

// ====================== FUNCTION PROTOTYPES ======================
void initializeSensors();
void initializeMotorPins();
void initializeWiFi();
void setupWebServer();
float readUltrasonicSensor(uint8_t trig, uint8_t echo);
void fuseSensorData();
void setMotorDirections(uint8_t leftDir, uint8_t rightDir);
void setMotorSpeeds(uint8_t leftSpeed, uint8_t rightSpeed);
void rampMotorSpeed();
void stopMotorsImmediate();
void executeCommand(uint8_t cmd, uint8_t speed);
void updateAssistedMode(uint8_t userCmd, uint8_t userSpeed);
void updateAutonomousMode();
void updateFollowMode();
void updateStuntMode();
void checkSafety();
String getStatusJSON();

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nCAR CONTROLLER STARTING...");
  
  initializeSensors();
  initializeMotorPins();
  initializeWiFi();
  setupWebServer();
  
  carState.lastSensorReadTime = millis();
  carState.lastSpeedRampTime = millis();
  Serial.println("Ready.");
}

// ====================== LOOP ======================
void loop() {
  uint32_t now = millis();

  if (now - carState.lastSensorReadTime >= 30) {
    carState.lastSensorReadTime = now;
    
    float rawA = readUltrasonicSensor(SENSOR_A_TRIG, SENSOR_A_ECHO);
    delay(5); 
    float rawB = readUltrasonicSensor(SENSOR_B_TRIG, SENSOR_B_ECHO);
    delay(5);
    float rawC = readUltrasonicSensor(SENSOR_C_TRIG, SENSOR_C_ECHO);
    
    // إلغاء الفلتر البطيء ونقل القيم مباشرة لضمان التفاف وتوقف فوري ودقيق
    sensors.distanceA = rawA;
    sensors.distanceB = rawB;
    sensors.distanceC = rawC;
    sensors.distanceCenter = rawC;
    
    fuseSensorData();
  }

  if (now - carState.lastSpeedRampTime >= 10) {
    carState.lastSpeedRampTime = now;
    rampMotorSpeed();
  }

  checkSafety();
  switch (carState.currentMode) {
    case MODE_MANUAL: break;
    case MODE_ASSISTED: updateAssistedMode(carState.requestedCmd, carState.requestedSpeed); break;
    case MODE_AUTONOMOUS: updateAutonomousMode(); break;
    case MODE_STUNT: updateStuntMode(); break;
    case MODE_FOLLOW_ME: updateFollowMode(); break;
    default: executeCommand(MOTOR_STOP, 0); break;
  }

  delay(10);
}

// ====================== INITIALIZATION ======================
void initializeSensors() {
  pinMode(SENSOR_A_TRIG, OUTPUT); pinMode(SENSOR_A_ECHO, INPUT);
  pinMode(SENSOR_B_TRIG, OUTPUT); pinMode(SENSOR_B_ECHO, INPUT);
  pinMode(SENSOR_C_TRIG, OUTPUT); pinMode(SENSOR_C_ECHO, INPUT);
  digitalWrite(SENSOR_A_TRIG, LOW); digitalWrite(SENSOR_B_TRIG, LOW); digitalWrite(SENSOR_C_TRIG, LOW);
}

void initializeMotorPins() {
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);
  
  ledcSetup(PWM_CHANNEL_LEFT, PWM_FREQ, PWM_RES);
  ledcAttachPin(ENA, PWM_CHANNEL_LEFT);
  ledcSetup(PWM_CHANNEL_RIGHT, PWM_FREQ, PWM_RES);
  ledcAttachPin(ENB, PWM_CHANNEL_RIGHT);
  
  stopMotorsImmediate();
}

void initializeWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPdisconnect(true);
  delay(100);
  WiFi.softAP(ssid, password, 6, 0, 1, false);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
}

// ====================== SENSOR FUNCTIONS ======================
float readUltrasonicSensor(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  unsigned long duration = pulseIn(echoPin, HIGH, SENSOR_TIMEOUT);
  if (duration == 0) return 150.0; // إذا ضاع الصوت بسبب زاوية الميلان، اعتبره 150 بدلاً من 400
  
  float distance = duration / 58.2;
  if (distance < 2.0) distance = 2.0;
  if (distance > 150.0) distance = 150.0;
  return distance;
}

void fuseSensorData() {
  // المركز هو الحساس الثالث مباشرة
  sensors.distanceCenter = sensors.distanceC;
  float diff = sensors.distanceB - sensors.distanceA;
  sensors.obstacleAngle = constrain(diff * 2, -90, 90);
}

// ====================== MOTOR CONTROL ======================
void setMotorDirections(uint8_t leftDir, uint8_t rightDir) {
  if (leftDir == 1) { digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW); }
  else if (leftDir == 2) { digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); }
  else { digitalWrite(IN1, LOW); digitalWrite(IN2, LOW); }
  
  if (rightDir == 1) { digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); }
  else if (rightDir == 2) { digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH); }
  else { digitalWrite(IN3, LOW); digitalWrite(IN4, LOW); }
  
  motorState.leftDirection = leftDir;
  motorState.rightDirection = rightDir;
}

void setMotorSpeeds(uint8_t leftSpeed, uint8_t rightSpeed) {
  motorState.leftSpeed = leftSpeed;
  motorState.rightSpeed = rightSpeed;
  ledcWrite(PWM_CHANNEL_LEFT, leftSpeed);
  ledcWrite(PWM_CHANNEL_RIGHT, rightSpeed);
}

void rampMotorSpeed() {
  int16_t newLeft = motorState.leftSpeed;
  int16_t newRight = motorState.rightSpeed;
  
  if (motorState.leftTargetSpeed > 0 && motorState.leftSpeed == 0) newLeft = START_BOOST;
  if (motorState.rightTargetSpeed > 0 && motorState.rightSpeed == 0) newRight = START_BOOST;
  
  if (newLeft < motorState.leftTargetSpeed) {
    newLeft += RAMP_STEP;
    if (newLeft > motorState.leftTargetSpeed) newLeft = motorState.leftTargetSpeed;
  } else if (newLeft > motorState.leftTargetSpeed) {
    newLeft -= RAMP_STEP;
    if (newLeft < motorState.leftTargetSpeed) newLeft = motorState.leftTargetSpeed;
  }
  
  if (newRight < motorState.rightTargetSpeed) {
    newRight += RAMP_STEP;
    if (newRight > motorState.rightTargetSpeed) newRight = motorState.rightTargetSpeed;
  } else if (newRight > motorState.rightTargetSpeed) {
    newRight -= RAMP_STEP;
    if (newRight < motorState.rightTargetSpeed) newRight = motorState.rightTargetSpeed;
  }
  
  setMotorSpeeds((uint8_t)newLeft, (uint8_t)newRight);
}

void stopMotorsImmediate() {
  motorState.leftDirection = 0;
  motorState.rightDirection = 0;
  // 1. جعل جميع أقطاب الاتجاه LOW 
  setMotorDirections(0, 0);
  
  motorState.leftTargetSpeed = 0;
  motorState.rightTargetSpeed = 0;
  
  // 2. تفعيل الفرملة الإلكترونية: 
  // إعطاء أقصى طاقة (255) بينما الأقطاب LOW يصنع "دائرة قصر" تفرمل المحركات بقوة شديدة ولحظية.
  setMotorSpeeds(255, 255); 
  
  // ملاحظة: دالة rampMotorSpeed ستقوم بتخفيض هذه الفرملة من 255 إلى 0 
  // خلال حوالي ربع ثانية، مما يعمل كنظام ABS يمنع انزلاق العجلات ويوقفها بنعومة وأمان.
}

void executeCommand(uint8_t cmd, uint8_t speed) {
// إضافة هذا الشرط لمنع تكرار التوقف والسماح للفرملة بأخذ وقتها
  if (cmd == MOTOR_STOP && carState.currentMotorCmd == MOTOR_STOP) {
    return;
  }

  if (speed > 255) speed = 255;
  if (speed < 40 && speed > 0) speed = 40;
  
  carState.currentMotorCmd = cmd;
  carState.currentSpeed = speed;

  switch (cmd) {
    case MOTOR_STOP:
      stopMotorsImmediate();
      break;
    case MOTOR_FORWARD:
      setMotorDirections(1, 1);
      motorState.leftTargetSpeed = speed;
      motorState.rightTargetSpeed = speed;
      break;
    case MOTOR_BACKWARD:
      setMotorDirections(2, 2);
      motorState.leftTargetSpeed = speed;
      motorState.rightTargetSpeed = speed;
      break;
    case MOTOR_LEFT:
      setMotorDirections(2, 1);
      motorState.leftTargetSpeed = speed * 0.7;
      motorState.rightTargetSpeed = speed;
      break;
    case MOTOR_RIGHT:
      setMotorDirections(1, 2);
      motorState.leftTargetSpeed = speed;
      motorState.rightTargetSpeed = speed * 0.7;
      break;
    case MOTOR_SPIN_LEFT:
      setMotorDirections(2, 1);
      motorState.leftTargetSpeed = speed;
      motorState.rightTargetSpeed = speed;
      break;
    case MOTOR_SPIN_RIGHT:
      setMotorDirections(1, 2);
      motorState.leftTargetSpeed = speed;
      motorState.rightTargetSpeed = speed;
      break;
    default:
      stopMotorsImmediate();
  }
}

// ====================== MODE LOGIC ======================

// Assisted: توقف كامل عند وجود عائق في اتجاه الحركة
// Assisted: توقف فوري ولحظي عند وجود عائق في اتجاه الحركة على مسافة 10 سم
// Assisted: توقف فوري ولحظي وقوي عند مسافة 10 سم
void updateAssistedMode(uint8_t userCmd, uint8_t userSpeed) {

  float criticalDist = 15.0; 
  
  bool obstacleForward = (userCmd == MOTOR_FORWARD && sensors.distanceC <= criticalDist);
  bool obstacleLeft    = (userCmd == MOTOR_LEFT && sensors.distanceA <= criticalDist);
  bool obstacleRight   = (userCmd == MOTOR_RIGHT && sensors.distanceB <= criticalDist);
  
  if (obstacleForward || obstacleLeft || obstacleRight) {
    if (carState.currentMotorCmd != MOTOR_STOP) {
      executeCommand(MOTOR_STOP, 0); // تفعيل الفرملة فوراً
    }
    return;
  }
  
  if (carState.currentMotorCmd != userCmd || carState.currentSpeed != userSpeed) {
    executeCommand(userCmd, userSpeed);
  }
}
// Autonomous: تجنب العوائق بذكاء مع نظام المناورة المتسلسلة (رجوع ثم التفاف)
void updateAutonomousMode() {
  // متغيرات ثابتة (Static) لحفظ حالة المناورة بين كل استدعاء للدالة
  static uint32_t maneuverTimer = 0;
  static uint8_t maneuverState = 0; // 0: طبيعي، 1: يرجع للخلف، 2: يلتف
  static uint8_t escapeTurnCmd = MOTOR_SPIN_RIGHT;

  uint32_t now = millis();

  // 1. تنفيذ المناورة الحالية إن وجدت (يمنع ارتعاش السيارة ويجعلها تكمل حركتها للنهاية)
  if (maneuverState > 0) {
    if (now < maneuverTimer) {
      return; // استمر في الحركة الحالية (الرجوع أو الالتفاف) حتى ينتهي الوقت
    } else {
      // الانتقال للمرحلة التالية من المناورة
      if (maneuverState == 1) {
        // انتهى وقت الرجوع للخلف، نبدأ الآن بالدوران
        executeCommand(escapeTurnCmd, SPEED_HIGH);
        maneuverState = 2;
        maneuverTimer = now + 350; // وقت الدوران (350 ملي ثانية - يمكنك تعديله)
        return;
      } else if (maneuverState == 2) {
        // انتهت المناورة بالكامل
        maneuverState = 0;
        executeCommand(MOTOR_STOP, 0); // توقف لحظي للثبات
        return;
      }
    }
  }

  // 2. قراءة المسافات في الوضع الطبيعي
  float front = sensors.distanceC;
  float left = sensors.distanceA;
  float right = sensors.distanceB;
  
  float criticalDist = 15.0; // تم رفع مسافة الأمان قليلاً لتحسين دقة القرار
  
  bool obsFront = (front <= criticalDist);
  bool obsLeft = (left <= criticalDist);
  bool obsRight = (right <= criticalDist);
  
  // طوارئ قصوى: الاصطدام وشيك جداً من أي اتجاه (أقل من 6 سم)
  if (front <= 6.0 || left <= 6.0 || right <= 6.0) {
    escapeTurnCmd = (left > right) ? MOTOR_SPIN_LEFT : MOTOR_SPIN_RIGHT;
    executeCommand(MOTOR_BACKWARD, SPEED_HIGH);
    maneuverState = 1;
    maneuverTimer = now + 500; // رجوع للخلف بقوة لمدة نصف ثانية لإنقاذ السيارة
    return;
  }
  
  // عائق أمامي (نبدأ مناورة منظمة: رجوع ثم دوران)
  if (obsFront) {
    escapeTurnCmd = (left > right) ? MOTOR_SPIN_LEFT : MOTOR_SPIN_RIGHT;
    executeCommand(MOTOR_BACKWARD, SPEED_MEDIUM);
    maneuverState = 1;
    maneuverTimer = now + 300; // رجوع للخلف بهدوء لمدة 300 ملي ثانية قبل اللف
    return;
  }
  
  // عائق جانبي أيسر (دوران لليمين لتعديل المسار بدون توقف)
  if (obsLeft && !obsRight) {
    executeCommand(MOTOR_SPIN_RIGHT, SPEED_MEDIUM);
    return;
  }
  
  // عائق جانبي أيمن (دوران لليسار لتعديل المسار بدون توقف)
  if (obsRight && !obsLeft) {
    executeCommand(MOTOR_SPIN_LEFT, SPEED_MEDIUM);
    return;
  }
  
  // الطريق سالك تماماً
  if (!obsFront && !obsLeft && !obsRight) {
    // استخدمنا SPEED_MEDIUM في التلقائي لزيادة دقة الملاحظة وتقليل الحوادث
    executeCommand(MOTOR_FORWARD, SPEED_MEDIUM); 
    return;
  }
  
  // محاصر من الجانبين ولكن الأمام سالك (مثل الممرات الضيقة)
  if (obsLeft && obsRight && !obsFront) {
    executeCommand(MOTOR_FORWARD, SPEED_LOW); 
    return;
  }
}
// Follow Me: تتبع الهدف باستخدام جميع الحساسات
void updateFollowMode() {
  const float TARGET = 30.0;    // المسافة المستهدفة
  const float HYST = 5.0;       // منطقة التوقف
  float front = sensors.distanceC;
  float left = sensors.distanceA;
  float right = sensors.distanceB;
  
  // إذا كان الهدف بعيداً جداً (>120 سم) توقف
  if (front > 120.0 && left > 120.0 && right > 120.0) {
    executeCommand(MOTOR_STOP, 0);
    return;
  }
  
  // تحديد أقرب جسم بين الحساسات (الهدف)
  float minDist = min(front, min(left, right));
  
  // إذا كان أقرب جسم هو الأمامي
  if (minDist == front) {
    if (front < TARGET - HYST) {
      executeCommand(MOTOR_BACKWARD, SPEED_LOW);
    } else if (front > TARGET + HYST) {
      executeCommand(MOTOR_FORWARD, SPEED_MEDIUM);
    } else {
      executeCommand(MOTOR_STOP, 0);
    }
    return;
  }
  
  // إذا كان الهدف على اليسار
  if (minDist == left && left < right) {
    if (left < OBSTACLE_MINIMUM) {
      executeCommand(MOTOR_STOP, 0);
    } else {
      executeCommand(MOTOR_LEFT, SPEED_LOW);
    }
    return;
  }
  
  // إذا كان الهدف على اليمين
  if (minDist == right && right < left) {
    if (right < OBSTACLE_MINIMUM) {
      executeCommand(MOTOR_STOP, 0);
    } else {
      executeCommand(MOTOR_RIGHT, SPEED_LOW);
    }
    return;
  }
  
  // في حال عدم التحديد، استخدم الأمامي
  if (front > TARGET + HYST) {
    executeCommand(MOTOR_FORWARD, SPEED_MEDIUM);
  } else if (front < TARGET - HYST) {
    executeCommand(MOTOR_BACKWARD, SPEED_LOW);
  } else {
    executeCommand(MOTOR_STOP, 0);
  }
}

void updateStuntMode() {
  uint32_t elapsed = millis() - stuntStartTime;
  switch (stuntPhase) {
    case 1:
      if (elapsed < 3000) executeCommand(MOTOR_SPIN_RIGHT, SPEED_HIGH);
      else { carState.currentMode = MODE_IDLE; executeCommand(MOTOR_STOP, 0); }
      break;
    case 2:
      if (elapsed < 6000) {
        if (((elapsed / 500) % 4) < 2) executeCommand(MOTOR_LEFT, SPEED_HIGH);
        else executeCommand(MOTOR_RIGHT, SPEED_HIGH);
      } else { carState.currentMode = MODE_IDLE; executeCommand(MOTOR_STOP, 0); }
      break;
    case 3:
      if (elapsed < 8000) {
        if (elapsed < 4000) executeCommand(MOTOR_SPIN_LEFT, SPEED_MEDIUM);
        else executeCommand(MOTOR_SPIN_RIGHT, SPEED_MEDIUM);
      } else { carState.currentMode = MODE_IDLE; executeCommand(MOTOR_STOP, 0); }
      break;
    case 4:
      if (elapsed < 2000) executeCommand(MOTOR_FORWARD, SPEED_FULL);
      else if (elapsed < 2500) executeCommand(MOTOR_STOP, 0);
      else if (elapsed < 4500) executeCommand(MOTOR_BACKWARD, SPEED_FULL);
      else { carState.currentMode = MODE_IDLE; executeCommand(MOTOR_STOP, 0); }
      break;
  }
}

void checkSafety() {
  if (carState.emergencyStop) {
    executeCommand(MOTOR_STOP, 0);
    if (millis() - stuntStartTime > 5000) carState.emergencyStop = false;
  }
}

String getStatusJSON() {
  char buf[512];
  snprintf(buf, sizeof(buf),
    "{\"sensorA\":%.1f,\"sensorB\":%.1f,\"sensorC\":%.1f,\"angle\":%.0f,"
    "\"mode\":%u,\"motorCmd\":%u,\"motorSpeed\":%u,\"emergencyStop\":%s}",
    sensors.distanceA, sensors.distanceB, sensors.distanceC, sensors.obstacleAngle,
    carState.currentMode, carState.currentMotorCmd, carState.currentSpeed,
    carState.emergencyStop ? "true" : "false"
  );
  return String(buf);
}

// ====================== WEB SERVER ======================
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = index_html; 
    request->send(200, "text/html", html);
  });

  server.on("/api/setMode", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("mode")) {
      uint8_t mode = atoi(request->getParam("mode")->value().c_str());
      if (mode <= 5) {
        carState.currentMode = mode;
        request->send(200, "application/json", "{\"status\":\"ok\"}");
      } else {
        request->send(400, "application/json", "{\"error\":\"Invalid mode\"}");
      }
    } else {
      request->send(400, "application/json", "{\"error\":\"Missing mode\"}");
    }
  });

  server.on("/api/motorCmd", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("cmd") && request->hasParam("speed")) {
      uint8_t cmd = atoi(request->getParam("cmd")->value().c_str());
      uint8_t speed = atoi(request->getParam("speed")->value().c_str());
      
      // حفظ الأمر المطلوب بدلاً من تنفيذه إجبارياً
      carState.requestedCmd = cmd;
      carState.requestedSpeed = speed;
      
      // ننفذ فوراً فقط إذا كنا في الوضع اليدوي أو المتوقف
      if (carState.currentMode == MODE_MANUAL || carState.currentMode == MODE_IDLE) {
        executeCommand(cmd, speed);
      }
      
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
    }
  });

  server.on("/api/stunt", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("id")) {
      uint8_t id = atoi(request->getParam("id")->value().c_str());
      carState.currentMode = MODE_STUNT;
      stuntPhase = id;
      stuntStartTime = millis();
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      request->send(400, "application/json", "{\"error\":\"Missing id\"}");
    }
  });

  server.on("/api/emergencyStop", HTTP_GET, [](AsyncWebServerRequest *request) {
    carState.emergencyStop = true;
    executeCommand(MOTOR_STOP, 0);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", getStatusJSON());
  });

  server.begin();
  Serial.println("Web server started on http://192.168.4.1");
}
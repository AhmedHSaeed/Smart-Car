#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ====================== CONFIGURATION ======================

// Wi-Fi
const char* ssid = "Robot_Controller";
const char* password = "12345678";
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
#define SENSOR_C_TRIG 4    // center (المعتمد للكشف الأمامي)
#define SENSOR_C_ECHO 5

// PWM
#define PWM_FREQ 5000
#define PWM_RES  8
#define PWM_CHANNEL_LEFT  0
#define PWM_CHANNEL_RIGHT 1

// Sensor Constants (تم رفع المدى)
#define SENSOR_TIMEOUT 12000
#define OBSTACLE_CRITICAL 20   // التوقف الكامل عند 10 سم
#define OBSTACLE_MINIMUM 10    // طوارئ قصوى
#define SLOW_ZONE 80           // غير مستخدم حالياً

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
  float distanceC;        // center (primary)
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

// Filter buffers (تم التعديل إلى 3 لزيادة السرعة والحد من التأخير)
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

  // تم تقليل وقت التحديث إلى 30 ملي ثانية لزيادة سرعة الملاحظة
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
  // مسافة 30 سم ممتازة للسرعات العالية، الفرامل ستوقفها قبل العائق بـ 15-20 سم تقريباً
  float criticalDist = 30.0; 
  
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
// Autonomous: تجنب العوائق بذكاء باستخدام الحساسات الثلاثة
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
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>CarBot Control Panel</title>
    <link href="https://fonts.googleapis.com/css2?family=Space+Mono:wght@400;700&family=Poppins:wght@300;400;600;700&display=swap" rel="stylesheet">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        :root {
            --bg-dark: #0a0e27; --bg-secondary: #141829; --bg-tertiary: #1a1f3a;
            --cyan-primary: #00d9ff; --orange-accent: #ff6b35; --orange-dark: #d84315;
            --text-primary: #ffffff; --text-secondary: #b0b8d4;
            --border-color: rgba(0, 217, 255, 0.2);
        }
        html { background: linear-gradient(135deg, var(--bg-dark) 0%, #0f1535 100%); background-attachment: fixed; }
        body {
            font-family: 'Poppins', sans-serif; background: transparent; min-height: 100vh;
            padding: 20px; color: var(--text-primary); overflow-x: hidden;
        }
        body::before {
            content: ''; position: fixed; top: 0; left: 0; width: 100%; height: 100vh;
            background-image: linear-gradient(rgba(0, 217, 255, 0.03) 1px, transparent 1px),
                              linear-gradient(90deg, rgba(0, 217, 255, 0.03) 1px, transparent 1px);
            background-size: 50px 50px; pointer-events: none; z-index: -1;
        }
        .container { max-width: 1200px; margin: 0 auto; animation: fadeInUp 0.8s ease-out; }
        @keyframes fadeInUp { from { opacity: 0; transform: translateY(30px); } to { opacity: 1; transform: translateY(0); } }
        .header { text-align: center; margin-bottom: 40px; padding-bottom: 30px; border-bottom: 2px solid var(--border-color); }
        h1 {
            font-family: 'Space Mono', monospace; font-size: clamp(2rem, 5vw, 3.5rem); font-weight: 700;
            margin-bottom: 10px; background: linear-gradient(135deg, var(--cyan-primary) 0%, #00a8cc 100%);
            -webkit-background-clip: text; -webkit-text-fill-color: transparent;
            text-shadow: 0 0 30px rgba(0, 217, 255, 0.3); letter-spacing: 1px;
        }
        .subtitle { font-size: 0.95rem; color: var(--text-secondary); font-weight: 300; letter-spacing: 2px; text-transform: uppercase; }
        .emergency-container { margin-bottom: 30px; }
        .emergency-stop {
            width: 100%; padding: 18px 30px; background: linear-gradient(135deg, var(--orange-accent) 0%, var(--orange-dark) 100%);
            color: white; border: 2px solid var(--orange-accent); border-radius: 12px; font-size: 1.1rem;
            font-weight: 700; font-family: 'Space Mono', monospace; cursor: pointer; transition: all 0.3s;
            letter-spacing: 1px; box-shadow: 0 0 40px rgba(255, 107, 53, 0.3);
            text-transform: uppercase;
        }
        .emergency-stop:hover { transform: translateY(-3px); box-shadow: 0 0 60px rgba(255, 107, 53, 0.5); filter: brightness(1.1); }
        .emergency-stop:active { transform: translateY(-1px); }
        .main-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 25px; margin-bottom: 30px; }
        .control-section {
            background: var(--bg-secondary); border: 2px solid var(--border-color); border-radius: 16px;
            padding: 25px; backdrop-filter: blur(10px); transition: all 0.3s ease;
        }
        .control-section:hover { border-color: rgba(0, 217, 255, 0.4); box-shadow: 0 0 30px rgba(0, 217, 255, 0.1); }
        .section-title {
            font-family: 'Space Mono', monospace; font-size: 0.85rem; font-weight: 700;
            text-transform: uppercase; letter-spacing: 2px; color: var(--cyan-primary);
            margin-bottom: 20px; padding-bottom: 12px; border-bottom: 2px solid var(--border-color);
        }
        .mode-selector { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap: 12px; }
        .mode-btn {
            padding: 14px 16px; background: var(--bg-tertiary); color: var(--text-secondary);
            border: 2px solid var(--border-color); border-radius: 10px; font-family: 'Poppins', sans-serif;
            font-size: 0.9rem; font-weight: 600; cursor: pointer; transition: all 0.3s;
            text-transform: capitalize; letter-spacing: 0.5px;
        }
        .mode-btn:hover { border-color: var(--cyan-primary); color: var(--cyan-primary); box-shadow: 0 0 20px rgba(0, 217, 255, 0.2); transform: translateY(-2px); }
        .mode-btn.active { background: linear-gradient(135deg, rgba(0, 217, 255, 0.2) 0%, rgba(0, 217, 255, 0.1) 100%); color: var(--cyan-primary); border-color: var(--cyan-primary); box-shadow: 0 0 30px rgba(0, 217, 255, 0.3); }
        .manual-controls { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; margin-bottom: 20px; }
        .control-btn {
            padding: 16px; background: var(--bg-tertiary); color: var(--cyan-primary);
            border: 2px solid var(--border-color); border-radius: 12px; font-family: 'Poppins', sans-serif;
            font-size: 0.95rem; font-weight: 600; cursor: pointer; transition: all 0.3s;
            text-transform: uppercase; letter-spacing: 0.5px; position: relative; overflow: hidden;
        }
        .control-btn::before {
            content: ''; position: absolute; top: 50%; left: 50%; width: 0; height: 0;
            background: radial-gradient(circle, rgba(0, 217, 255, 0.3), transparent);
            border-radius: 50%; transform: translate(-50%, -50%); transition: width 0.6s, height 0.6s;
        }
        .control-btn:hover::before { width: 200px; height: 200px; }
        .control-btn:hover { border-color: var(--cyan-primary); box-shadow: 0 0 25px rgba(0, 217, 255, 0.3); transform: translateY(-2px); }
        .control-btn:active { transform: scale(0.95); }
        .speed-control { margin-top: 20px; padding-top: 20px; border-top: 2px solid var(--border-color); }
        .speed-control label { display: block; font-size: 0.85rem; font-weight: 600; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 1px; margin-bottom: 12px; }
        .speed-input-group { display: flex; gap: 15px; align-items: center; }
        .speed-slider { flex: 1; height: 6px; background: var(--bg-tertiary); border: 1px solid var(--border-color); border-radius: 3px; cursor: pointer; -webkit-appearance: none; }
        .speed-slider::-webkit-slider-thumb { -webkit-appearance: none; width: 16px; height: 16px; border-radius: 50%; background: linear-gradient(135deg, var(--cyan-primary) 0%, #00a8cc 100%); cursor: pointer; box-shadow: 0 0 15px rgba(0, 217, 255, 0.6); }
        #speedValue { display: inline-block; min-width: 45px; text-align: center; font-family: 'Space Mono', monospace; font-size: 1.1rem; font-weight: 700; color: var(--cyan-primary); }
        .stunt-buttons { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 12px; }
        .stunt-btn {
            padding: 16px; background: linear-gradient(135deg, var(--orange-accent) 0%, rgba(255, 107, 53, 0.8) 100%);
            color: white; border: 2px solid var(--orange-accent); border-radius: 12px;
            font-family: 'Poppins', sans-serif; font-size: 0.9rem; font-weight: 600; cursor: pointer;
            transition: all 0.3s; text-transform: uppercase; letter-spacing: 0.5px;
            box-shadow: 0 0 20px rgba(255, 107, 53, 0.2);
        }
        .stunt-btn:hover { border-color: #ff8c5a; box-shadow: 0 0 35px rgba(255, 107, 53, 0.4); transform: translateY(-2px); filter: brightness(1.1); }
        .stunt-btn:active { transform: scale(0.95); }
        .sensor-display { background: var(--bg-secondary); border: 2px solid var(--border-color); border-radius: 16px; padding: 25px; }
        .sensor-row { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 20px; margin-bottom: 25px; }
        .sensor-row:last-child { margin-bottom: 0; }
        .sensor-item { text-align: center; }
        .sensor-label { font-size: 0.75rem; font-weight: 700; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px; display: block; }
        .sensor-value { font-family: 'Space Mono', monospace; font-size: 1.8rem; font-weight: 700; color: var(--cyan-primary); margin: 8px 0; text-shadow: 0 0 15px rgba(0, 217, 255, 0.4); }
        .sensor-bar { width: 100%; height: 8px; background: var(--bg-tertiary); border: 1px solid var(--border-color); border-radius: 4px; overflow: hidden; }
        .sensor-bar-fill { height: 100%; background: linear-gradient(90deg, var(--cyan-primary) 0%, #00a8cc 100%); transition: width 0.3s; box-shadow: 0 0 15px rgba(0, 217, 255, 0.6); }
        .status-panel { background: var(--bg-secondary); border: 2px solid var(--border-color); border-radius: 16px; padding: 25px; }
        .status-item { display: flex; justify-content: space-between; align-items: center; padding: 12px 0; border-bottom: 1px solid rgba(0, 217, 255, 0.1); }
        .status-item:last-child { border-bottom: none; }
        .status-label { color: var(--text-secondary); font-weight: 600; font-size: 0.9rem; text-transform: uppercase; letter-spacing: 0.5px; }
        .status-value { font-family: 'Space Mono', monospace; color: var(--cyan-primary); font-weight: 700; }
        @media (max-width: 768px) {
            .container { padding: 10px; }
            h1 { font-size: 1.8rem; }
            .main-grid { grid-template-columns: 1fr; }
            .manual-controls { grid-template-columns: repeat(3, 1fr); }
            .mode-selector { grid-template-columns: repeat(2, 1fr); }
            .sensor-row { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>CARBOT</h1>
            <p class="subtitle">Autonomous Vehicle Control System</p>
        </div>
        <div class="emergency-container">
            <button class="emergency-stop" onclick="emergencyStop()">EMERGENCY STOP</button>
        </div>
        <div class="main-grid">
            <div class="control-section">
                <div class="section-title">Control Modes</div>
                <div class="mode-selector">
                    <button class="mode-btn" onclick="selectMode(1)">Manual</button>
                    <button class="mode-btn" onclick="selectMode(2)">Assisted</button>
                    <button class="mode-btn" onclick="selectMode(3)">Autonomous</button>
                    <button class="mode-btn" onclick="selectMode(4)">Stunt</button>
                    <button class="mode-btn" onclick="selectMode(5)">Follow Me</button>
                </div>
            </div>
            <div class="sensor-display">
                <div class="section-title">Sensor Data</div>
                <div class="sensor-row">
                    <div class="sensor-item">
                        <span class="sensor-label">Left (A)</span>
                        <div class="sensor-value" id="sensorA">--</div>
                        <div class="sensor-bar"><div class="sensor-bar-fill" id="barA" style="width: 0%"></div></div>
                    </div>
                    <div class="sensor-item">
                        <span class="sensor-label">Center</span>
                        <div class="sensor-value" id="sensorC">--</div>
                        <div class="sensor-bar"><div class="sensor-bar-fill" id="barC" style="width: 0%"></div></div>
                    </div>
                    <div class="sensor-item">
                        <span class="sensor-label">Right (B)</span>
                        <div class="sensor-value" id="sensorB">--</div>
                        <div class="sensor-bar"><div class="sensor-bar-fill" id="barB" style="width: 0%"></div></div>
                    </div>
                </div>
                <div class="sensor-row">
                    <div class="sensor-item">
                        <span class="sensor-label">Obstacle Angle</span>
                        <div class="sensor-value" id="angle">--</div>
                    </div>
                </div>
            </div>
            <div class="status-panel">
                <div class="section-title">System Status</div>
                <div class="status-item"><span class="status-label">Current Mode:</span><span class="status-value" id="currentMode">Idle</span></div>
                <div class="status-item"><span class="status-label">Motor Command:</span><span class="status-value" id="motorCmd">Stop</span></div>
                <div class="status-item"><span class="status-label">Speed:</span><span class="status-value" id="motorSpeed">0</span></div>
                <div class="status-item"><span class="status-label">Emergency Stop:</span><span class="status-value" id="estopStatus">Normal</span></div>
            </div>
        </div>
        <div id="manualSection" class="control-section" style="display:none;">
            <div class="section-title">Manual Control</div>
            <div class="manual-controls">
                <div></div>
                <button class="control-btn" onmousedown="startMove(1, currentSpeed)" onmouseup="stopMove()" ontouchstart="startMove(1, currentSpeed)" ontouchend="stopMove()">Forward</button>
                <div></div>
                <button class="control-btn" onmousedown="startMove(3, currentSpeed)" onmouseup="stopMove()" ontouchstart="startMove(3, currentSpeed)" ontouchend="stopMove()">Left</button>
                <button class="control-btn" onclick="motorCmd(0, 0)">Stop</button>
                <button class="control-btn" onmousedown="startMove(4, currentSpeed)" onmouseup="stopMove()" ontouchstart="startMove(4, currentSpeed)" ontouchend="stopMove()">Right</button>
                <div></div>
                <button class="control-btn" onmousedown="startMove(2, currentSpeed)" onmouseup="stopMove()" ontouchstart="startMove(2, currentSpeed)" ontouchend="stopMove()">Backward</button>
                <div></div>
            </div>
            <div class="speed-control">
                <label>Speed Control (0-255)</label>
                <div class="speed-input-group">
                    <input type="range" id="speedSlider" min="40" max="255" value="200" class="speed-slider" oninput="updateSpeed(this.value)">
                    <span id="speedValue">200</span>
                </div>
            </div>
        </div>
        <div id="stuntSection" class="control-section" style="display:none;">
            <div class="section-title">Stunt Sequences</div>
            <div class="stunt-buttons">
                <button class="stunt-btn" onclick="startStunt(1)">Spin 360</button>
                <button class="stunt-btn" onclick="startStunt(2)">Zigzag</button>
                <button class="stunt-btn" onclick="startStunt(3)">Figure-8</button>
                <button class="stunt-btn" onclick="startStunt(4)">Brake & Reverse</button>
            </div>
        </div>
    </div>
    <script>
        let currentMode = 0;
        let currentSpeed = 200;
        const modeNames = {0:'Idle',1:'Manual',2:'Assisted',3:'Autonomous',4:'Stunt',5:'Follow Me'};
        const motorCmdNames = {0:'Stop',1:'Forward',2:'Backward',3:'Left',4:'Right',5:'Spin Left',6:'Spin Right'};
        
        function selectMode(mode) {
            currentMode = mode;
            document.querySelectorAll('.mode-btn').forEach(btn => btn.classList.remove('active'));
            event.target.classList.add('active');
            document.getElementById('manualSection').style.display = (mode === 1 || mode === 2) ? 'block' : 'none';
            document.getElementById('stuntSection').style.display = (mode === 4) ? 'block' : 'none';
            fetch('/api/setMode?mode=' + mode).catch(e => console.error(e));
        }
        
        function motorCmd(cmd, speed) {
            fetch('/api/motorCmd?cmd=' + cmd + '&speed=' + speed).catch(e => console.error(e));
        }
        
        function startMove(cmd, speed) {
            if (currentMode !== 1 && currentMode !== 2) { alert('Switch to Manual or Assisted mode first!'); return; }
            motorCmd(cmd, speed);
        }
        
        function stopMove() {
            motorCmd(0, 0);
        }
        
        function updateSpeed(val) {
            currentSpeed = parseInt(val);
            document.getElementById('speedValue').textContent = currentSpeed;
        }
        
        function startStunt(stuntId) { fetch('/api/stunt?id=' + stuntId).catch(e => console.error(e)); }
        function emergencyStop() { fetch('/api/emergencyStop').catch(e => console.error(e)); alert('Emergency Stop Activated!'); }
        
        function updateStatus() {
            fetch('/api/status')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('sensorA').textContent = data.sensorA.toFixed(1) + ' cm';
                    document.getElementById('sensorB').textContent = data.sensorB.toFixed(1) + ' cm';
                    document.getElementById('sensorC').textContent = data.sensorC.toFixed(1) + ' cm';
                    document.getElementById('angle').textContent = data.angle.toFixed(0) + String.fromCharCode(176);
                    const maxDist = 150;
                    document.getElementById('barA').style.width = Math.min(100, (data.sensorA / maxDist) * 100) + '%';
                    document.getElementById('barB').style.width = Math.min(100, (data.sensorB / maxDist) * 100) + '%';
                    document.getElementById('barC').style.width = Math.min(100, (data.sensorC / maxDist) * 100) + '%';
                    document.getElementById('currentMode').textContent = modeNames[data.mode] || 'Unknown';
                    document.getElementById('motorCmd').textContent = motorCmdNames[data.motorCmd] || 'Unknown';
                    document.getElementById('motorSpeed').textContent = data.motorSpeed;
                    document.getElementById('estopStatus').textContent = data.emergencyStop ? 'ACTIVE' : 'Normal';
                }).catch(e => console.error(e));
        }
        
        setInterval(updateStatus, 500);
        updateStatus();
    </script>
</body>
</html>
)rawliteral";
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
#include "sensor.h"
#include "webserver.h"   // saveConfig()

#include <Wire.h>
#include <SparkFun_BNO08x_Arduino_Library.h>

// Sensor handling for the Adafruit BNO085 breakout.
// Based on the level sensor by klehnst
// (https://github.com/klehnst/ESP32-Adrafruit-BNO085-HomeAssistant-Caravan-Levelsensor-MQTT),
// reduced to motorhome levelling with four wedges.
//
// Sign convention (same as the original project, so displays and HA cards stay compatible):
//   pitch > 0  = front is higher than the rear
//   roll  > 0  = left side is higher than the right side

static BNO08x imu;
static unsigned long lastReport = 0;
static unsigned long lastRestart = 0;
static bool haveFiltered = false;
static float fPitch = 0.0f, fRoll = 0.0f;

#define DEADBAND        0.1f    // degrees shown as 0
#define STUCK_MS        3000    // no report for this long -> re-enable the report
#define RESTART_GAP_MS  5000    // at most one recovery attempt per 5 s

static const char* MOUNT_NAMES[MOUNT_COUNT] = {
    "Flach USB vorne", "Flach USB hinten", "Flach USB links", "Flach USB rechts",
    "Wand vorne", "Wand hinten", "Wand links", "Wand rechts"
};

const char* mountName(MountOrientation m) {
    return (m >= 0 && m < MOUNT_COUNT) ? MOUNT_NAMES[m] : MOUNT_NAMES[0];
}

bool mountFromName(const String& name, MountOrientation& out) {
    for (int i = 0; i < MOUNT_COUNT; i++) {
        if (name.equalsIgnoreCase(MOUNT_NAMES[i])) { out = (MountOrientation)i; return true; }
    }
    return false;
}

// Remap of the raw angles depending on how the board is mounted
static void applyMounting(float rawPitch, float rawRoll, float& pitch, float& roll) {
    switch (config.mount) {
        case MOUNT_USB_FRONT:  pitch =  rawPitch; roll =  rawRoll;  break;
        case MOUNT_USB_BACK:   pitch = -rawPitch; roll = -rawRoll;  break;
        case MOUNT_USB_LEFT:   pitch =  rawRoll;  roll = -rawPitch; break;
        case MOUNT_USB_RIGHT:  pitch = -rawRoll;  roll =  rawPitch; break;
        case MOUNT_WALL_FRONT: pitch =  rawRoll;  roll =  rawPitch; break;
        case MOUNT_WALL_BACK:  pitch = -rawRoll;  roll = -rawPitch; break;
        case MOUNT_WALL_LEFT:  pitch =  rawPitch; roll = -rawRoll;  break;
        case MOUNT_WALL_RIGHT: pitch = -rawPitch; roll =  rawRoll;  break;
        default:               pitch =  rawPitch; roll =  rawRoll;  break;
    }
}

// Wedge heights. Each wheel's height relative to the vehicle centre:
//   h = +-lat +-lon  with  lat = tan(roll) * track/2,  lon = tan(pitch) * wheelbase/2
// The high wheels cannot be lowered, so every wheel is raised to the HIGHEST one:
//   wedge = max(h) - h,  max(h) = |lat| + |lon|
// (The original raised the low wheels only to the centre height, i.e. half of what is needed.)
static void calculateWedges() {
    float lat = tanf(sensorData.roll * DEG_TO_RAD) * config.track_width / 2.0f;
    float lon = tanf(sensorData.pitch * DEG_TO_RAD) * config.wheelbase / 2.0f;
    float top = fabsf(lat) + fabsf(lon);

    float hFL =  lat + lon;   // left higher with roll > 0, front higher with pitch > 0
    float hFR = -lat + lon;
    float hRL =  lat - lon;
    float hRR = -lat - lon;

    sensorData.wedge_fl = top - hFL;
    sensorData.wedge_fr = top - hFR;
    sensorData.wedge_rl = top - hRL;
    sensorData.wedge_rr = top - hRR;

    sensorData.is_level = fabsf(sensorData.pitch) <= config.tolerance &&
                          fabsf(sensorData.roll)  <= config.tolerance;
}

void sensorRecalc() {
    calculateWedges();
}

static bool enableReport() {
    // Game Rotation Vector = accelerometer + gyro fusion without magnetometer:
    // not disturbed by cables or the metal body, gyro smooths vibrations.
    return imu.enableGameRotationVector(SENSOR_UPDATE_MS);
}

bool sensorInit() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Serial.printf("[Sensor] I2C SDA=%d SCL=%d, BNO085 at 0x%02X\n", PIN_I2C_SDA, PIN_I2C_SCL, BNO08X_I2C_ADDR);
    delay(200);

    if (!imu.begin(BNO08X_I2C_ADDR, Wire)) {
        Serial.println("[Sensor] BNO085 not found - check wiring, swap SDA/SCL in config_local.h");
        sensorData.sensor_ok = false;
        return false;
    }
    delay(100);
    sensorData.sensor_ok = enableReport();
    lastReport = millis();
    Serial.printf("[Sensor] BNO085 %s\n", sensorData.sensor_ok ? "OK" : "report could not be enabled");
    return sensorData.sensor_ok;
}

static void handleReport() {
    float qr = imu.getQuatReal();
    float qi = imu.getQuatI();
    float qj = imu.getQuatJ();
    float qk = imu.getQuatK();

    // Quaternion -> roll / pitch
    float rawRoll = atan2f(2.0f * (qr * qi + qj * qk), 1.0f - 2.0f * (qi * qi + qj * qj)) * RAD_TO_DEG;
    float sinp = 2.0f * (qr * qj - qk * qi);
    float rawPitch = (fabsf(sinp) >= 1.0f) ? copysignf(90.0f, sinp) : asinf(sinp) * RAD_TO_DEG;

    float p, r;
    applyMounting(rawPitch, rawRoll, p, r);
    sensorData.raw_pitch = p;
    sensorData.raw_roll = r;

    if (config.tare_active) {
        p -= config.tare_pitch;
        r -= config.tare_roll;
    }

    // Optional EMA smoothing (the original had a switch for it, but never applied a filter)
    if (config.filter_active) {
        if (!haveFiltered) { fPitch = p; fRoll = r; haveFiltered = true; }
        fPitch += FILTER_ALPHA * (p - fPitch);
        fRoll  += FILTER_ALPHA * (r - fRoll);
        p = fPitch;
        r = fRoll;
    } else {
        haveFiltered = false;
    }

    // Deadband and 0.1 degree resolution
    sensorData.pitch = (fabsf(p) < DEADBAND) ? 0.0f : roundf(p * 10.0f) / 10.0f;
    sensorData.roll  = (fabsf(r) < DEADBAND) ? 0.0f : roundf(r * 10.0f) / 10.0f;

    calculateWedges();
    sensorData.updates++;
}

void sensorLoop() {
    if (!sensorData.sensor_ok) return;

    if (imu.wasReset()) {
        Serial.println("[Sensor] BNO085 was reset - enabling the report again");
        enableReport();
    }

    if (imu.getSensorEvent() && imu.getSensorEventID() == SENSOR_REPORTID_GAME_ROTATION_VECTOR) {
        lastReport = millis();
        handleReport();
        return;
    }

    // No data for a while: try to bring the sensor back (without blocking the loop)
    if (millis() - lastReport > STUCK_MS && millis() - lastRestart > RESTART_GAP_MS) {
        lastRestart = millis();
        Serial.println("[Sensor] No data - re-enabling the report");
        enableReport();
    }
}

void sensorSetTare() {
    config.tare_pitch = sensorData.raw_pitch;
    config.tare_roll  = sensorData.raw_roll;
    config.tare_active = true;
    haveFiltered = false;
    saveConfig();
    Serial.printf("[Sensor] Tare set: pitch=%.2f roll=%.2f\n", config.tare_pitch, config.tare_roll);
}

void sensorResetTare() {
    config.tare_pitch = 0.0f;
    config.tare_roll  = 0.0f;
    config.tare_active = false;
    haveFiltered = false;
    saveConfig();
    Serial.println("[Sensor] Tare reset");
}

void sensorSetTareValues(float pitch, float roll) {
    config.tare_pitch = constrain(pitch, -MAX_TARE_DEG, MAX_TARE_DEG);
    config.tare_roll  = constrain(roll,  -MAX_TARE_DEG, MAX_TARE_DEG);
    config.tare_active = true;
    haveFiltered = false;
    saveConfig();
    Serial.printf("[Sensor] Tare values: pitch=%.2f roll=%.2f\n", config.tare_pitch, config.tare_roll);
}

void sensorRestart() {
    Serial.println("[Sensor] Soft reset of the BNO085");
    imu.softReset();
    delay(300);
    sensorData.sensor_ok = enableReport();
    lastReport = millis();
}

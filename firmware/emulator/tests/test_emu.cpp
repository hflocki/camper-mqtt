// Host test of emu.cpp: payloads of every emulated device, commands, input checks and
// scenarios, without an ESP32. Run with ./run.sh.
//   test_emu            run the checks
//   test_emu --schema   print the schema and state JSON (used for the web UI mockup)
#include "Arduino.h"
#include "emu.h"
#include <map>

SerialShim Serial;
static uint32_t nowMs = 1000;
uint32_t millis() { return nowMs; }

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::map<std::string, std::string> out;
static void emit(const String& t, const String& p, void*) { out[t.s] = p.s; }
static void build(int d) { out.clear(); emuBuild(d, emit, nullptr); }
static std::string get(int d, const char* t) { build(d); return out.count(t) ? out[t] : std::string("<none>"); }
static void dump(int d) { build(d); for (auto& kv : out) printf("  %-52s %s\n", kv.first.c_str(), kv.second.c_str()); }
static void cmd(const char* t, const char* p) { emuCommand(t, p, strlen(p)); }
static void run(int seconds) { for (int i = 0; i < seconds; i++) { nowMs += 1000; emuLoop(); } }

static std::vector<std::string> sentOnce;
static void sender(const String& t, const String& p) { sentOnce.push_back(t.s + "=" + p.s); }

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    emuInit();
    emuSetSender(sender);
    for (int d = 0; d < D_COUNT; d++) emuSetNoise(d, false);

    if (argc > 1 && !strcmp(argv[1], "--schema")) {
        JsonDocument s, st;
        emuSchemaJson(s);
        emuStateJson(st.to<JsonObject>());
        String a, b;
        serializeJson(s, a);
        serializeJson(st, b);
        printf("{\"schema\":%s,\"state\":%s}\n", a.c_str(), b.c_str());
        return 0;
    }

    bool verbose = argc > 1 && !strcmp(argv[1], "-v");
    if (verbose) for (int d = 0; d < D_COUNT; d++) { printf("== %s\n", EMU_DEVS[d].id); dump(d); }

    // ---- example data in the real formats ----
    CHECK(get(D_TANK, "camper/tanklevel/water/percent") == "62");
    CHECK(get(D_TANK, "camper/tanklevel/gas/content") == "7.81");
    CHECK(get(D_LEVEL, "camper/level/is_level") == "ON");
    CHECK(get(D_HEATER, "camper/heater/control_status/hvac_action") == "heating");
    CHECK(get(D_TEMP, "camper/temp/sensors") == "[{\"id\":\"fridge\",\"name\":\"Kühlschrank\",\"t\":5,\"alarm\":0},{\"id\":\"freezer\",\"name\":\"Gefrierfach\",\"t\":-17,\"alarm\":0}]");
    CHECK(get(D_BATTERY, "camper/battery/batteries") == "[{\"id\":\"house\",\"name\":\"Bordbatterie\",\"ok\":true,\"t\":2,\"soc\":78,\"v\":13.25,\"a\":-4.2,\"w\":-56,\"rt\":1114}]");
    CHECK(get(D_ROUTER, "camper/door") == "{\"Door\":{\"name\":\"din2\",\"type\":\"Digital In\",\"state\":\"close\",\"custom_name\":\"Door\"}}");
    CHECK(get(D_ROUTER, "camper/router/mobile") == "{\"rsrp\":-92,\"rssi\":-65,\"conntype\":\"LTE\"}");
    CHECK(get(D_ROUTER, "camper/rut").find("\"fix_status\":\"1\"") != std::string::npos);
    CHECK(get(D_ZIGBEE, "camper/zigbee/data/00158d0000000011/1/0402/0000") == "{\"data\":{\"val\":5.50,\"type_sm\":\"report\"}}");
    CHECK(get(D_ZIGBEE, "camper/zigbee/data/00158d0000000003/1/0006/0000") == "<none>");   // contact 3 is off

    // ---- commands from the display ----
    cmd("camper/heater/set/target_temp_room", "23");
    CHECK(get(D_HEATER, "camper/heater/control_status/room_setpoint") == "23.0");
    cmd("camper/heater/set/hvac_mode", "off");
    CHECK(get(D_HEATER, "camper/heater/control_status/hvac_mode") == "off");
    CHECK(get(D_HEATER, "camper/heater/control_status/target_temp_room") == "0");
    cmd("camper/heater/set/heating_mode", "high");
    CHECK(get(D_HEATER, "camper/heater/control_status/fan_mode") == "high");
    CHECK(get(D_HEATER, "camper/heater/control_status/hvac_mode") == "heat");
    cmd("camper/heater/set/water_mode", "eco");
    CHECK(get(D_HEATER, "camper/heater/control_status/target_temp_water") == "40");
    cmd("camper/maxxfan/cmd/p1/led/set", "TOGGLE");
    CHECK(get(D_FAN, "camper/maxxfan/p1/led") == "ON");
    cmd("camper/maxxfan/cmd/p1/press", "auto");
    CHECK(get(D_FAN, "camper/maxxfan/p1/auto_hold") == "ON");
    cmd("camper/level/cmd/tare/set", "PRESS");
    CHECK(get(D_LEVEL, "camper/level/pitch") == "0.0");
    CHECK(get(D_LEVEL, "camper/level/tare_active") == "ON");
    cmd("camper/tanklevel/cmd/water/filter/set", "OFF");
    CHECK(get(D_TANK, "camper/tanklevel/water/filter_active") == "OFF");
    cmd("camper/zigbee/cmd/a4c1380000000021/1/0006", "ON");
    CHECK(get(D_ZIGBEE, "camper/zigbee/data/a4c1380000000021/1/0006/0000") == "{\"data\":{\"val\":\"ON\",\"type_sm\":\"report\"}}");
    cmd("camper/zigbee/cmd/a4c1380000000021/1/0008/0000", "77");
    CHECK(get(D_ZIGBEE, "camper/zigbee/data/a4c1380000000021/1/0008/0000") == "{\"data\":{\"val\":77,\"type_sm\":\"report\"}}");

    // ---- input checks from the web UI ----
    String err;
    CHECK(!emuSetValue("w_pct", "130", err));
    CHECK(emuSetValue("w_pct", "12,7", err));
    CHECK(get(D_TANK, "camper/tanklevel/water/percent") == "13");
    CHECK(!emuSetValue("t1_id", "a/b", err));
    CHECK(!emuSetValue("tk_pre", "test/#", err));
    CHECK(emuSetValue("tk_pre", "test/tanks", err));
    CHECK(get(D_TANK, "test/tanks/water/percent") == "13");
    CHECK(emuSetValue("t1_t", "9.4", err));
    CHECK(get(D_TEMP, "camper/temp/alarm_text") == "Kühlschrank zu warm: 9,4 °C (Grenze 8,0 °C)");
    CHECK(get(D_TEMP, "camper/temp/fridge/alarm") == "ON");
    CHECK(emuSetValue("t1_hi", "", err));                           // no limit
    CHECK(get(D_TEMP, "camper/temp/alarm") == "OFF");
    CHECK(emuSetValue("b1_a", "5", err));
    CHECK(get(D_BATTERY, "camper/battery/house/charging") == "ON");
    CHECK(get(D_BATTERY, "camper/battery/house/runtime") == "None");
    CHECK(emuSetValue("b1_type", "3", err));                        // BM2: no current
    CHECK(get(D_BATTERY, "camper/battery/house/current") == "<none>");
    CHECK(get(D_BATTERY, "camper/battery/house/state") == "Normal");
    CHECK(emuSetValue("pitch", "2", err) && emuSetValue("roll", "1", err));
    CHECK(get(D_LEVEL, "camper/level/wedge_rr") == "15.2");          // rear right is lowest
    CHECK(get(D_LEVEL, "camper/level/wedge_fl") == "0.0");           // front left is highest
    CHECK(emuSetValue("gas_state", "Kein Signal", err));
    CHECK(get(D_TANK, "test/tanks/gas/level") == "<none>");
    CHECK(!emuSetValue("gas_state", "kaputt", err));

    // ---- scenarios ----
    CHECK(emuStartScenario("depart", 0, err));
    run(5);
    CHECK(get(D_ROUTER, "camper/door").find("open") != std::string::npos);
    run(30);
    CHECK(get(D_ROUTER, "camper/rut").find("\"speed\":\"30.0\"") != std::string::npos);
    run(60);
    CHECK(get(D_ROUTER, "camper/door").find("close") != std::string::npos);
    run(60);
    CHECK(get(D_ROUTER, "camper/rut").find("\"speed\":\"0.0\"") != std::string::npos);
    CHECK(sentOnce.size() >= 2 && sentOnce[0] == "camper/display/cmd/awning=ON" && sentOnce[1] == "camper/display/cmd/awning=OFF");
    emuStopScenario(true);

    CHECK(emuStartScenario("offline", D_HEATER, err));
    run(40);
    CHECK(emuMode(D_HEATER) == M_OFFLINE);
    run(120);
    CHECK(emuMode(D_HEATER) == M_ON);
    emuStopScenario(true);

    CHECK(emuStartScenario("heat", 0, err));
    run(60);
    float room = atof(get(D_HEATER, "camper/heater/control_status/current_temp_room").c_str());
    CHECK(room > 13 && room < 22);
    emuStopScenario(true);
    CHECK(get(D_HEATER, "camper/heater/control_status/room_setpoint") == "23.0");   // restored

    CHECK(emuStartScenario("empty", 0, err));
    run(200);
    CHECK(get(D_TANK, "test/tanks/water/percent") == "5");
    emuStopScenario(false);                                         // keep the end state
    CHECK(get(D_TANK, "test/tanks/water/percent") == "5");

    emuResetValues(-1);
    CHECK(get(D_TANK, "camper/tanklevel/water/percent") == "62");

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}

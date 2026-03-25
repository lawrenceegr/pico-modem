# AquaGate Optimization Tasks

Self-contained optimization plan. Each task has the exact file, line numbers,
the problem, and the concrete fix so work can resume from any session.

## Status Key
- [x] done
- [x] done

---

## T1: read_buffer() always burns full timeout — ROOT CAUSE of blocking
**File:** `driver/net/modem/modem.cpp:202-221`

**Problem:** `read_buffer(timeout)` busy-loops for the *entire* timeout
duration even if the modem responded in 20ms. Every AT command pays the full
timeout as latency. A `send_at_response(cmd, 2000)` always takes 2 full
seconds regardless of actual response time.

**Current code:**
```cpp
void SimA7670E::read_buffer(uint32_t timeout)
{
    clear_buffer();
    rx_ptr = (uint8_t *)&uart_buffer[0];
    uint32_t start = time_us_32();
    while ((time_us_32() - start < timeout * 1000) &&
           (rx_ptr - &uart_buffer[0] < UART_BUFFER_SIZE))
    {
        if (uart_is_readable(MODEM_UART))
        {
            uart_read_blocking(MODEM_UART, rx_ptr, 1);
            rx_ptr++;
        }
    }
}
```

**Fix:** Add early termination. As bytes arrive, check if the accumulated
buffer ends with a known terminator: `OK\r\n`, `ERROR\r\n`, `> `, or a
URC result code like `+CM...`. Return as soon as a complete response is
detected. Keep the timeout as a ceiling only.

Approach: after each byte is appended, check the last few bytes for
terminator patterns. Use a small idle-gap detector too — if no byte arrives
for e.g. 50ms after we already have data, assume the response is complete
(handles non-standard responses).

**Impact:** Collapses a single publish() from ~14s worst-case to ~200-500ms
typical. This is the single biggest fix and the foundation for T2-T6.

- [x] done

---

## T2: Unnecessary sleep_ms() in MQTT publish path
**File:** `driver/net/mqtt/mqtt_client.cpp:80-139`

**Problem:** Two `sleep_ms(500)` calls (lines 103, 118) after `send_raw()`,
plus `read_buffer(2000)` after each. The sleeps were workarounds because
`read_buffer` couldn't detect the actual OK — it would start reading too
early or miss the response. With T1 fixed (early termination on OK),
the modem's response will be captured properly without the artificial delay.

**Fix:** Remove both `sleep_ms(500)` at lines 103 and 118. The
`read_buffer()` call after `send_raw()` will wait for OK and return early.

**Impact:** Saves ~1s per publish call.

- [x] done

---

## T3: Unnecessary sleep_ms() in MQTT connect path
**File:** `driver/net/mqtt/mqtt_client.cpp:18-65`

**Problem:** `sleep_ms(500)` at line 40 (after CMQTTSTART) and line 49
(after CMQTTACCQ). Same workaround pattern as T2.

**Fix:** Remove both. The subsequent `send_at_response()` calls will
properly wait for the modem with T1's early termination.

**Impact:** Saves 1s on every MQTT connect/reconnect.

- [x] done

---

## T4: Unnecessary sleep_ms() in MQTT subscribe path
**File:** `driver/net/mqtt/mqtt_client.cpp:142-176`

**Problem:** `sleep_ms(500)` at line 158 after `send_raw(topic)`.

**Fix:** Remove it. `read_buffer()` with early termination handles timing.

- [x] done

---

## T5: Unnecessary sleep_ms() in activate_pdp()
**File:** `driver/net/modem/modem.cpp:153-178`

**Problem:** `sleep_ms(1000)` between PDP deactivate and activate (line 158).
`sleep_ms(2000)` after activate before checking address (line 165).
The deactivate already waits up to 5s for OK. The activate waits 10s.
The address query has its own 2s timeout. These sleeps are redundant.

**Fix:** Remove `sleep_ms(1000)` at line 158. Remove `sleep_ms(2000)` at
line 165. The AT command timeouts themselves are sufficient.

- [x] done

---

## T6: URC data lost during AT command exchanges
**Files:** `driver/net/modem/modem.cpp`, `driver/net/modem/modem.h`,
`driver/net/mqtt/mqtt_client.cpp`

**Problem:** When an MQTT message URC (`+CMQTTRXSTART...+CMQTTRXEND`)
arrives while `publish()` is mid-AT-exchange, the URC bytes land in the
UART read buffer mixed with the AT response. `send_at_response()` returns
the whole buffer as the command response, and the URC is discarded.
The next `poll_message()` call never sees it — the inbound command is
silently lost.

This is the *mechanism* behind the reported "commands ignored while
something is ongoing" bug.

**Fix:**
1. Add a URC staging buffer to `SimA7670E` (e.g. `char urc_buf[512]`
   with a length counter).
2. In the improved `read_buffer()` (T1), after collecting the full
   response, scan for `+CMQTTRXSTART` through `+CMQTTRXEND` substrings.
   If found, extract and move them to `urc_buf`, leaving only the AT
   response in the main buffer.
3. Add a `bool has_pending_urc()` and `std::string get_pending_urc()`
   method to SimA7670E.
4. In `MqttClient::poll_message()`, check `modem.has_pending_urc()`
   *before* calling `modem.poll_urc()`. If a buffered URC exists,
   process it immediately instead of polling UART.

**Impact:** Commands received during publish/heartbeat are no longer
silently dropped.

- [x] done

---

## T7: Remove TEMP debug code in poll_message()
**File:** `driver/net/mqtt/mqtt_client.cpp:192-200`

**Problem:** Raw URC hex dump block left from debugging:
```cpp
// TEMP: dump raw URC to see exact format
printf("=== RAW URC ===\n");
for (size_t i = 0; i < urc.size(); i++) { ... }
printf("=== END URC ===\n");
// END TEMP
```
Runs on every received message. Unnecessary printf overhead in production.

**Fix:** Delete the entire block (lines 192-200, the `// TEMP` through
`// END TEMP` section).

- [x] done

---

## T8: CMQTTSTART double-attempt logic
**File:** `driver/net/mqtt/mqtt_client.cpp:30-39`

**Problem:** Sends `AT+CMQTTSTART` via `send_at()`, and if that fails,
immediately resends via `send_at_response()` with a different error check.
The intent is to handle the case where MQTT service is already started
(modem returns `+CMQTTSTART: 0` instead of plain `OK`), but the
two-attempt approach is convoluted.

**Current code:**
```cpp
if (!modem.send_at("AT+CMQTTSTART", "OK", 3000))
{
    string r = modem.send_at_response("AT+CMQTTSTART", 3000);
    if (r.find("ERROR") != string::npos &&
        r.find("+CMQTTSTART: 0") == string::npos)
    {
        LOG_ERR("MQTT service start failed");
        return false;
    }
}
```

**Fix:** Single attempt:
```cpp
string r = modem.send_at_response("AT+CMQTTSTART", 3000);
if (r.find("OK") == string::npos &&
    r.find("+CMQTTSTART: 0") == string::npos)
{
    LOG_ERR("MQTT service start failed");
    return false;
}
```
Accept either `OK` (fresh start) or `+CMQTTSTART: 0` (already running).

- [x] done

---

## T9: Duplicate stdio_init_all()
**Files:** `driver/actuators/actuators.c:32`, `main.cpp:194`

**Problem:** `stdio_init_all()` called in both `actuators_init()` and
`main()`. Double-init. It belongs in `main()` only — actuators shouldn't
own stdio initialization.

**Fix:** Remove `stdio_init_all();` from `actuators_init()` (line 32 of
`driver/actuators/actuators.c`).

- [x] done

---

## T10: send_at / send_at_response pass std::string by value
**Files:** `driver/net/modem/modem.h:21-22`, `driver/net/modem/modem.cpp:180-194`

**Problem:**
```cpp
bool send_at(std::string cmd, std::string expected = "OK", uint32_t timeout = 3000);
std::string send_at_response(std::string cmd, uint32_t timeout = 3000);
```
Every call copies the string. On RP2040 with 264KB RAM, unnecessary
heap churn.

**Fix:** Change signatures to `const std::string &`:
```cpp
bool send_at(const std::string &cmd, const std::string &expected = "OK", uint32_t timeout = 3000);
std::string send_at_response(const std::string &cmd, uint32_t timeout = 3000);
```
Update both `.h` and `.cpp`.

- [x] done

---

## Execution Order

```
T1  ← foundation (early-termination read_buffer)
 ├── T2  (remove publish sleeps)
 ├── T3  (remove connect sleeps)
 ├── T4  (remove subscribe sleeps)
 ├── T5  (remove PDP sleeps)
 └── T6  (URC buffering, builds on T1's buffer scanning)

T7-T10  ← independent cleanups, any order
```

T1 must land first. T2-T5 can then be done together. T6 builds on T1.
T7-T10 are standalone and can be done at any point.

---
title: "Registering a SIM Card to a Cellular Network with AT Commands"
date: 2026-01-02
author: Lawrence
tags: [modem, cellular, iot, embedded, at-commands, sim7080g, cat-m, nb-iot, lte-m]
image:
    path: "/images/blog/rp2040-modem.jpg"
    alt: rp2040-modem-setup
---

# Registering a SIM Card to a Cellular Network with AT Commands

Getting a SIM card to register on a cellular network requires understanding the sequence of steps from power-on to network attachment. This post walks through the practical process using AT commands with Cat-M and NB-IoT modems, revealing what actually happens beneath framework abstractions and helping you diagnose issues when things go wrong.

The goal is simple: insert a SIM card, power on a modem, and get it registered to the network. But the steps to achieve this are often unclear when working with cellular modems for the first time.

## Why Work with AT Commands Directly?

High-level frameworks handle network registration automatically, which works well until something fails. When registration doesn't happen, framework error messages rarely explain why. Did the SIM fail to initialize? Is signal strength too weak? Is the APN wrong? Working directly with AT commands makes each step visible and debuggable.

While there are alternative approaches to modem communication - such as using PPP (Point-to-Point Protocol) to establish a direct network interface - AT commands remain the most transparent method for understanding and controlling the registration process. PPP abstracts away the registration details, which can be convenient for production but limits visibility during development and troubleshooting.

This isn't about avoiding frameworks entirely - it's about understanding what they're doing so you can troubleshoot effectively and make informed decisions about when abstractions help versus when they hide critical details.

## The Hayes AT Command Protocol

Cellular modems communicate using AT commands - a text-based protocol that's been standard since 1981. Commands follow a simple pattern:

```
AT+COMMAND[=parameters]
```

Responses include the command result and a status code:
```
+COMMAND: response_data
OK
```

Or on failure:
```
ERROR
```

Most cellular modems support a core set of standardized 3GPP commands for SIM operations, network registration, and signal quality checks. Vendor-specific commands extend functionality but the basics remain consistent across manufacturers.

## Step 1: Power On and Modem Initialization

Before any AT commands work, the modem must complete its boot sequence. This isn't instant - it requires powering the module correctly and waiting for initialization.

### Power Toggle Sequence

Most cellular modems don't power on simply by applying voltage. They require a power enable pin to be pulsed:

1. Pull power enable pin HIGH
2. Hold for 1-2 seconds (consult datasheet)
3. Pull power enable pin LOW
4. Wait for modem boot

For the SIM7080G, this pulse is 1.5 seconds.

### Boot Timing

Documentation often understates actual boot time. The SIM7080G datasheet suggests ~30 seconds, but reliable operation requires at least 35 seconds before the first AT command attempt.

Testing this programmatically:
```
1. Toggle power enable pin
2. Wait 35 seconds
3. Send AT command
4. If no response, wait 4 seconds and retry
5. Repeat up to 20 times
```

Measured boot times ranged from 35.1 to 37.8 seconds across multiple power cycles, confirming the need for conservative timing.

### First Command Test

The simplest AT command verifies the modem responds:

```
AT
```

Expected response:
```
OK
```

If you get no response, the modem hasn't completed boot. If you get garbled characters, check baud rate (typically 115200).

Enable command echo for easier debugging:
```
ATE1
```

This echoes back every command you send, making it clear whether the modem received your input correctly.

## Step 2: SIM Card Detection

Once the modem responds to basic commands, verify SIM card presence and status.

### Checking SIM Status

```
AT+CPIN?
```

Possible responses:
- `+CPIN: READY` - SIM unlocked and ready
- `+CPIN: SIM PIN` - PIN code required
- `+CPIN: SIM PUK` - PUK required (PIN entered incorrectly 3 times)
- `+CME ERROR: 10` - SIM not inserted

### Will Consumer SIM Cards Work?

A common question: do you need special IoT SIM cards, or will standard prepaid cards work?

Testing with a Safaricom consumer prepaid SIM card confirmed it works perfectly with Cat-M modems. Requirements:
- Active data plan on the SIM
- Network supports Cat-M/LTE-M in your region
- PIN lock disabled (or unlock via `AT+CPIN=<pin>`)
- Correct APN configured

This matters because IoT-specific SIMs often cost significantly more than standard prepaid cards. For development and low-volume deployments, consumer SIMs are viable.

### Enable Verbose Errors

Before proceeding, enable detailed error reporting:

```
AT+CMEE=2
```

This changes generic `ERROR` responses to specific error codes like `+CME ERROR: 30` (no network service), making diagnosis much easier.

## Step 3: Network Mode Configuration

Before registration can succeed, the modem needs to know which network technology to use. This is vendor-specific configuration.

### For SIM7080G (Cat-M/NB-IoT Modem):

**Set LTE-only mode:**
```
AT+CNMP=38
```

**Select network mode with AT+CMNB:**
```
AT+CMNB=1  # Cat-M only
AT+CMNB=2  # NB-IoT only
AT+CMNB=3  # Both Cat-M and NB-IoT (automatic selection)
```

These commands tell the modem which LPWA technology to use:
- **Cat-M (LTE-M)**: Higher bandwidth (~375 kbps downlink), better for moderate data, voice support, mobility. Good for asset tracking, wearables.
- **NB-IoT**: Lower power consumption, better coverage indoors/underground, slower speeds (~60 kbps). Good for static sensors, smart meters.
- **Both (AT+CMNB=3)**: Automatic selection. Recommended for development as it provides most reliable connectivity by allowing the module to choose the best available network.

If your carrier doesn't support the configured mode, or if you configure the wrong mode, registration will fail even with good signal. Check your carrier's network support before deployment.

### For Standard GSM/GPRS/3G Modems:

Modems that support traditional cellular networks (2G/3G/4G) typically use different configuration commands. For example:

**Set network mode with AT+CNMP (varies by vendor):**
```
AT+CNMP=2   # Automatic (GSM/GPRS/LTE)
AT+CNMP=13  # GSM only
AT+CNMP=14  # WCDMA only
AT+CNMP=38  # LTE only
```

**Or using AT+CMODE:**
```
AT+CMODE=0  # Automatic mode selection
AT+CMODE=2  # GSM only
```

The specific commands and parameters vary by manufacturer. Consult your modem's AT command manual - the principle remains the same: specify which radio technology to use.

## Step 4: Access Point Name (APN) Configuration

The APN is the gateway between the cellular network and the internet. Think of it as a router configuration that tells the network where to route your data traffic.

### Configure the APN

```
AT+CGDCONT=1,"IP","<apn>"
```

Where:
- `1` is the context ID (usually 1)
- `"IP"` specifies IPv4 (can also be "IPV6" or "IPV4V6")
- `"<apn>"` is your carrier's access point name

### Finding the Correct APN

Carrier APNs are usually documented on their websites. Common examples:
- **Safaricom (Kenya)**: `safaricom`
- **Airtel**: `internet`

For the tested Safaricom SIM:
```
AT+CGDCONT=1,"IP","safaricom"
```

**Common mistake:** Using an IoT/M2M APN with a consumer SIM (or vice versa) causes registration success but data connection failure. The network mode, SIM type, and APN must all align.

## Step 5: Network Registration

With configuration complete, initiate network registration.

### Automatic Operator Selection

```
AT+COPS=0
```

This tells the modem to automatically select and register to an available network. Manual selection is possible but rarely necessary.

### Monitoring Registration Status

```
AT+CREG?
```

Response format:
```
+CREG: <n>,<stat>
```

Where `<stat>` indicates:
- `0` - Not registered, not searching
- `1` - Registered, home network
- `2` - Not registered, searching
- `3` - Registration denied
- `4` - Unknown (out of coverage)
- `5` - Registered, roaming

**Important:** Registration is not instant. Even with good signal, expect 30-120 seconds for `<stat>` to reach `1` or `5`.

### Checking Signal Strength

While waiting for registration, verify signal quality:

```
AT+CSQ
```

Response:
```
+CSQ: <rssi>,<ber>
```

The `<rssi>` value indicates signal strength on a 0-31 scale:
- `0-9`: Poor signal (may prevent registration)
- `10-14`: Adequate signal
- `15-19`: Good signal
- `20-31`: Excellent signal
- `99`: No signal or unknown

The second parameter (`<ber>`) is typically `99` meaning "not known" on LTE modems.

If signal is below 10, registration may fail or be unreliable. Check antenna connection and placement.

### Verifying Network Operator

Once registered, confirm the network:

```
AT+COPS?
```

Response:
```
+COPS: 0,0,"Safaricom",7
```

The `7` indicates E-UTRAN (LTE), confirming the modem registered on an LTE network.

## Step 6: Data Context Activation

Registration means the modem has attached to the network, but data connectivity requires activating a PDP (Packet Data Protocol) context.

### Check Context Status

```
AT+CGACT?
```

Response:
```
+CGACT: 1,1
```

Format: `+CGACT: <cid>,<state>`
- `<cid>`: Context ID (1 in this case)
- `<state>`: 0 = inactive, 1 = active

If the context shows `0` (inactive) despite successful registration, the APN is likely incorrect.

## Common Registration Failures and Diagnosis

### SIM Not Detected

**Symptom:**
```
AT+CPIN?
+CME ERROR: 10
```

**Causes:**
- SIM not physically inserted
- SIM inserted incorrectly (check orientation)
- Dirty or damaged SIM contacts
- Defective SIM card

**Diagnosis:** Try the SIM in a phone to verify it's functional.

### No Signal

**Symptom:**
```
AT+CSQ
+CSQ: 99,99
```

**Causes:**
- No antenna connected
- Antenna poorly positioned (indoors, metal shielding)
- Out of carrier coverage area

**Diagnosis:** Move the device near a window or outdoors. Check antenna connection.

### Registration Fails - Searching

**Symptom:**
```
AT+CREG?
+CREG: 0,2
```

Remains in state `2` (searching) indefinitely.

**Causes:**
- Weak signal (check with `AT+CSQ`)
- Wrong network mode (Cat-M vs NB-IoT mismatch with carrier support)
- Carrier doesn't support selected technology in your region

**Diagnosis:** Verify signal strength is adequate. Confirm carrier supports your selected mode (Cat-M or NB-IoT) in your region. Try `AT+CMNB=3` to allow automatic selection between both technologies.

### Registration Denied

**Symptom:**
```
AT+CREG?
+CREG: 0,3
```

**Causes:**
- SIM not activated or data plan expired
- Network mode incompatible with carrier
- SIM locked to different carrier (if used SIM)

**Diagnosis:** Verify SIM has active service. Try the SIM in a phone to confirm it can access data.

### Registered But No Data

**Symptom:**
```
AT+CREG?
+CREG: 0,1

AT+CGACT?
+CGACT: 1,0
```

Registered successfully but PDP context inactive.

**Causes:**
- Incorrect APN configured
- SIM has no data plan
- APN type mismatch (consumer APN with IoT SIM)

**Diagnosis:** Double-check APN spelling and verify data plan is active.

## Practical Implementation Example

The code implementing this registration sequence is available at:
https://github.com/learnqtkenya/pico-modem

The implementation demonstrates:
- Power toggle timing (1.5s pulse)
- Boot delay (35s) with retry logic
- SIM status verification
- Network registration monitoring
- Visual feedback via LED patterns

Key functions:
- `boot_modem()`: Handles power toggle and boot timing
```c
bool Sim7080G::boot_modem() {
    bool powered = false;
    uint32_t attempts = 0;
    uint32_t start_time = time_us_32();

    while (attempts < 20) {
        if (send_at("ATE1")) {
            // #ifdef DEBUG
            printf("Modem ready after %i ms\n", (time_us_32() - start_time) / 1000);
            // #endif

            return true;
        }
        if (!powered) {
            printf("Toggling power...\n");
            toggle_module_power();
            powered = true;
            // Wait 35s for modem to boot before first AT command
            printf("Waiting 35s for modem boot...\n");
            sleep_ms(35000);
        }
        
        sleep_ms(4000);
        attempts++;
    }

    return false;
}
```
- `check_sim()`: Verifies SIM presence with `AT+CPIN?`
```c
bool Sim7080G::check_sim() {
    printf("\n===== Checking SIM =====\n");

    string response = send_at_response("AT+CPIN?", 1000);

    if (response.find("READY") != string::npos) {
        printf("SIM: READY\n");
        return true;
    } else if (response.find("SIM PIN") != string::npos) {
        printf("SIM: PIN REQUIRED\n");
    } else if (response.find("SIM PUK") != string::npos) {
        printf("SIM: PUK REQUIRED\n");
    } else {
        printf("SIM: NOT DETECTED\n");
    }
    return false;
}
```
- `config_modem()`: Sets network mode and APN
```c

#define NETWORK_APN   "safaricom"

void Sim7080G::config_modem() {
    // Set error reporting to 2, set modem to text mode, delete left-over SMS,
    // select LTE-only mode, select Cat-M only mode (use CMNB=2 for NB-IoT, CMNB=3 for both), set the APN

    printf("\n===== Configuring Modem =====\n");

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CMEE=2;+CMGF=1;+CMGD=,4;+CNMP=38;+CMNB=1;+CGDCONT=1,\"IP\",\"%s\"", NETWORK_APN);
    send_at(cmd);

    // Set SST version, set SSL no verify, set header config
    send_at("AT+CSSLCFG=\"sslversion\",1,3;+SHSSL=1,\"\";+SHCONF=\"BODYLEN\",1024;+SHCONF=\"HEADERLEN\",350");

    printf("Modem configured for Cat-M and the APN set to \"%s\"\n", NETWORK_APN);
}
```
- `get_sim_info()`: Displays ICCID, IMSI, operator, signal, registration status
```c
void Sim7080G::get_sim_info() {
    printf("\n===== SIM Info =====\n");

    printf("\nICCID:\n");
    send_at_response("AT+CCID", 1000);

    printf("\nIMSI:\n");
    send_at_response("AT+CIMI", 1000);

    printf("\nPhone Number:\n");
    send_at_response("AT+CNUM", 1000);

    printf("\nOperator:\n");
    send_at_response("AT+COPS?", 2000);

    printf("\nSignal:\n");
    send_at_response("AT+CSQ", 1000);

    printf("\nRegistration:\n");
    send_at_response("AT+CREG?", 1000);
}
```
The code prioritizes clarity over optimization, making it suitable for learning and adaptation to other platforms.

## Key Takeaways

**Boot timing matters.** Documentation often understates actual requirements. The SIM7080G needs 35+ seconds, not the documented 30. Always add margin.

**Consumer SIMs work.** Special IoT SIM cards aren't always necessary. Standard prepaid cards function with proper APN configuration and active data plans.

**APN is critical.** Wrong APN causes registration success but data failure - a confusing symptom to debug without AT command visibility.

**Registration takes time.** Even with perfect configuration and strong signal, expect 30-120 seconds for network attachment. Impatience leads to false failure diagnoses.

**Signal strength matters.** RSSI below 10 causes intermittent issues. Below 5 usually prevents registration entirely. Check antenna connection first.

**Verbose errors help.** Enable `AT+CMEE=2` immediately. Generic `ERROR` responses hide the actual problem.

Working directly with AT commands strips away abstraction convenience, but reveals exactly what's happening at each step. This knowledge applies regardless of hardware platform or framework - the fundamentals of cellular registration remain constant. When frameworks fail, understanding AT commands transforms debugging from guesswork into systematic diagnosis.

## References and Further Reading

### Official Documentation
- [SIM7070/SIM7080/SIM7090 Series AT Command Manual V1.04](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/unit/sim7080g/en/SIM7070_SIM7080_SIM7090%20Series_AT%20Command%20Manual_V1.04.pdf) - Complete AT command reference
- [SIM7080G Hardware Design Manual](https://www.texim-europe.com/Cmsfile/SMM-SIM7080G-Hardware-Design-V1.04-DS-200525-TE.pdf) - Power sequence and electrical specifications
- [SIM7080 Series Specification](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/unit/sim7080g/en/SIM7080_Series_SPEC_20200427.pdf) - Module specifications

### AT Command References
- [AT+CMNB – Preferred Selection between CAT-M and NB-IoT](https://m2msupport.net/m2msupport/atcmnb-preferred-selection-between-cat-m-and-nb-iot/) - Network mode configuration
- [AT+CSQ – Signal Quality](https://m2msupport.net/m2msupport/atcsq-signal-quality/) - RSSI interpretation and signal strength
- [Signal Quality Reference](https://m2msupport.net/m2msupport/signal-quality/) - Understanding cellular signal metrics

### Hardware Resources
- [Waveshare SIM7080G Cat-M/NB-IoT HAT Wiki](https://www.waveshare.com/wiki/SIM7080G_Cat-M/NB-IoT_HAT) - Hardware implementation examples
- [Waveshare Pico SIM7080G Wiki](https://www.waveshare.com/wiki/Pico-SIM7080G-Cat-M/NB-IoT) - Raspberry Pi Pico integration

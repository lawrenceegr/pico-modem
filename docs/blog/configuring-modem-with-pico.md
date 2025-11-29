---
title: "Understanding Cellular Modem Communication: A Journey from Abstraction to AT Commands"
date: 2025-11-29
author: Lawrence
tags: [modem, cellular, iot, embedded, at-commands]
image:
    path: "/images/blog/rp2040-modem.jpg"
    alt: rp2040-modem-setup
---

# Understanding Cellular Modem Communication: A Journey from Abstraction to AT Commands

![RP2040 Modem Setup](../images/rp2040-modem.jpg)

When adding cellular connectivity to embedded systems, developers face a choice: use high-level frameworks with convenient abstractions, or work directly with AT commands to understand exactly what's happening under the hood. This post explores the latter approachnot because it's always necessary, but because understanding the fundamentals of modem communication reveals critical insights often hidden by abstraction layers.

## The Problem with Abstractions

I initially attempted to integrate a SIM7080G Cat-M modem using Zephyr RTOS. The framework provided excellent abstractions that handled most of the heavy liftingnetwork registration, data connectivity, and socket operations worked almost out of the box. However, when issues arose, I found myself unable to diagnose problems effectively. What was the modem actually doing? Why did registration fail intermittently? What commands were being sent?

This lack of visibility motivated a different approach: strip away the frameworks and communicate directly with the modem using AT commands over UART. The goal wasn't to reinvent working solutions, but to understand the fundamental principles of cellular modem operation that apply regardless of which hardware, SDK, or framework you use.

## Core Questions This Journey Answered

Several fundamental questions drove this exploration:

1. **Will standard consumer SIM cards work with industrial Cat-M modems?** Documentation often assumes carrier-specific IoT SIMs, but what about regular prepaid cards?

2. **What information can we extract directly from a modem?** Beyond connectivity, what device and network data is accessible?

3. **What are the actual timing requirements?** Abstractions hide critical delayshow long does a modem actually need to boot?

4. **How does network registration really work?** What's the sequence from power-on to data-ready?

5. **What happens when things go wrong?** How do you diagnose issues when you can't see the underlying commands?

## The Hayes AT Command Protocol

At the heart of modem communication lies the Hayes command seta text-based protocol dating back to 1981 but still ubiquitous in cellular modems. Understanding this protocol is key to working with any modem, regardless of manufacturer.

### Basic Structure

AT commands follow simple patterns:

```
AT+COMMAND[=parameters]
```

Responses typically include:
- Command echo (if echo enabled)
- Response data
- Final result code (`OK`, `ERROR`, or specific error)

### Universal Commands

Certain AT commands work across virtually all cellular modems:

| Command | Purpose | Response |
|---------|---------|----------|
| `AT` | Test connectivity | `OK` |
| `ATE0/ATE1` | Disable/enable echo | `OK` |
| `AT+CPIN?` | Check SIM status | `+CPIN: READY` or PIN status |
| `AT+CSQ` | Signal quality | `+CSQ: rssi,ber` |
| `AT+CREG?` | Registration status | Network state |
| `AT+CGMI` | Manufacturer | Vendor name |
| `AT+CGMM` | Model | Model identifier |
| `AT+CGSN` | IMEI | Device serial number |
| `AT+COPS?` | Operator selection | Current network |

These commands form the foundation of modem interaction across different platforms and hardware.

### Command Categories

AT commands generally fall into several categories:

**Basic Commands** (no + prefix):
- `AT`, `ATE0/1`, `ATI` - Basic control and information

**General Commands** (3GPP TS 27.007):
- `AT+CPIN`, `AT+CREG`, `AT+CSQ` - SIM, registration, signal
- Standardized across all 3GPP-compliant modems

**SMS Commands** (3GPP TS 27.005):
- `AT+CMGS`, `AT+CMGR` - SMS transmission and reception

**Data Commands** (3GPP TS 27.007):
- `AT+CGDCONT`, `AT+CGACT` - PDP context definition and activation

**Vendor-Specific Commands**:
- Vary by manufacturer (e.g., SIMCom uses `AT+C...`, Quectel uses `AT+Q...`)
- Provide access to proprietary features

Understanding this categorization helps when working with unfamiliar modems—standard commands should work identically, while vendor commands require consulting datasheets.

## The Critical Timing Challenge

One of the most significant discoveries was the importance of timing in modem initializationsomething often handled invisibly by frameworks.

### The Boot Sequence Mystery

Cellular modems don't instantly respond to commands after power application. They require a boot sequence:

1. **Power toggle**: A specific pulse width on the power enable pin (typically 1-2 seconds)
2. **Internal boot**: Firmware initialization, hardware checks
3. **AT command readiness**: The point where the modem responds to commands

For the SIM7080G, documentation suggested approximately 30 seconds. However, real-world testing revealed the actual requirement was **35 seconds**. That five-second difference caused intermittent failures that were difficult to diagnosesometimes the modem responded, sometimes it didn't, with no obvious pattern.

This illustrates a critical principle: **documented timing is often optimistic**. Real hardware requires safety margins, and abstractions that don't account for this can exhibit mysterious reliability issues.

### Measuring Boot Time

The actual boot time can be measured programmatically:

```
1. Record timestamp before power toggle
2. Toggle power enable pin
3. Wait initial delay (35s in this case)
4. Attempt AT command (e.g., ATE1)
5. If no response, wait additional interval (4s)
6. Retry with timeout (up to 20 attempts)
7. On success, calculate elapsed time from timestamp
```

This measurement approach revealed that boot times varied from 35.1 to 37.8 seconds across multiple power cyclesdemonstrating why fixed delays need generous margins.

### Why This Matters

Understanding boot timing is crucial when:
- Implementing watchdog timers (must account for full boot duration)
- Calculating battery life (boot current draw is significant)
- Designing error recovery (distinguish between boot delay and failure)
- Optimizing wake-from-sleep operations (different timing than cold boot)

Frameworks that hide this timing may silently incorporate delays you're unaware of, impacting your application's performance characteristics.

## Working with SIM Cards: Consumer vs. IoT

A key question was whether standard consumer SIM cards would work with Cat-M modems, or if special IoT SIMs were required.

### SIM Status Check

The fundamental SIM verification command works across all modems:

```
AT+CPIN?
```

Possible responses:
- `+CPIN: READY` - SIM unlocked and ready
- `+CPIN: SIM PIN` - PIN code required
- `+CPIN: SIM PUK` - PUK required (PIN entered incorrectly 3 times)
- `+CME ERROR: 10` - SIM not inserted

### The Verdict: Consumer SIMs Work

Testing with a standard Safaricom prepaid SIM revealed that consumer cards work perfectly with Cat-M modems, provided:

1. **Data plan active**: The SIM must have an active data subscription
2. **Network compatibility**: Carrier must support Cat-M/LTE-M in your region
3. **No PIN lock**: Either disabled, or you must send the PIN via `AT+CPIN=<pin>`
4. **Correct APN**: Must configure the carrier's access point name

This is significant because IoT-specific SIMs often carry premium pricing, while standard prepaid cards can be substantially cheaper for development and low-volume deployments.

### Understanding APN Configuration

The Access Point Name (APN) is critical but often misunderstood. It's essentially the "gateway" between the cellular network and the internet. Think of it as a router configuration:

```
[Your Device] <-> [Cellular Network] <-> [APN Gateway] <-> [Internet]
```

Each carrier has specific APNs for different service types:
- **Consumer mobile data**: `internet`, `web.gprs.mtnnigeria.net`, `safaricom`
- **IoT/M2M**: `iot.carrier.com`, `m2m.carrier.com`
- **Private networks**: Custom APNs for enterprise deployments

The APN configuration command structure:
```
AT+CGDCONT=<cid>,"<protocol>","<apn>"[,<address>]
```

Where:
- `<cid>`: Context identifier (usually 1)
- `<protocol>`: IP protocol type ("IP" for IPv4, "IPV6", "IPV4V6")
- `<apn>`: The access point name string
- `<address>`: Optional static IP (usually omitted for dynamic assignment)

**Common mistake**: Using the wrong APN results in successful network registration (`AT+CREG?` returns 0,1) but failed data connection (`AT+CGACT?` shows context inactive). The modem can attach to the network but cannot route data.

## Network Registration: The Hidden Complexity

Network registration is the process by which a modem connects to a cellular networka multi-step sequence that abstractions typically hide entirely.

### Registration States

The `AT+CREG?` command reveals registration status:

```
AT+CREG?
+CREG: <n>,<stat>
```

Where `<stat>` indicates:
- `0` - Not registered, not searching
- `1` - Registered, home network
- `2` - Not registered, searching
- `3` - Registration denied
- `4` - Unknown (e.g., out of coverage)
- `5` - Registered, roaming

### The Registration Sequence

Achieving registered status requires configuration before registration can succeed:

1. **Set network mode** (LTE-only vs. fallback to 2G/3G)
2. **Configure APN** (carrier-specific access point name)
3. **Select operator** (automatic or manual)
4. **Wait for registration** (can take 30-120 seconds)
5. **Verify signal quality** (ensure adequate signal strength)

For the SIM7080G example:

```
AT+CNMP=38          # Select LTE mode only
AT+CMNB=1           # Select Cat-M (not NB-IoT)
AT+CGDCONT=1,"IP","safaricom"  # Configure APN
AT+COPS=0           # Automatic operator selection
```

After configuration, registration isn't instantit can take 30 seconds to 2 minutes depending on signal conditions and network load.

### Why Registration Fails

Common failure modes discovered through direct AT interaction:

**Signal too weak:**
```
AT+CSQ
+CSQ: 99,99    # 99 indicates no signal
```
Signal strength below 10 (on 0-31 scale) often prevents registration.

**Wrong APN:**
```
AT+CGDCONT?
+CGDCONT: 1,"IP","wrong-apn"...
```
Incorrect APN configuration is invisible to the modem until data connection attempts fail.

**Network mode mismatch:**
If modem configured for Cat-M but carrier only supports NB-IoT (or vice versa), registration will fail with `+CREG: 0,3` (denied).

Understanding these failure modes allows targeted diagnostics impossible when abstractions simply report "connection failed."

### Signal Strength Interpretation

The `AT+CSQ` command returns signal strength, but interpreting the values requires understanding the scale:

```
AT+CSQ
+CSQ: 17,99
```

The first parameter is RSSI (Received Signal Strength Indicator):

| RSSI Value | Signal Strength | Interpretation |
|------------|-----------------|----------------|
| 0 | -113 dBm or less | No signal |
| 1 | -111 dBm | Very poor |
| 2-9 | -109 to -95 dBm | Poor |
| 10-14 | -93 to -85 dBm | OK |
| 15-19 | -83 to -75 dBm | Good |
| 20-30 | -73 to -53 dBm | Excellent |
| 31 | -51 dBm or greater | Excellent |
| 99 | Unknown/not detectable | Error condition |

The second parameter is BER (Bit Error Rate), typically 99 for "not known" on modern LTE modems.

**Practical guidance**: Values below 10 often cause connection instability. Values above 15 are generally reliable for data transmission. If you see persistent values below 10, investigate:
- Antenna connection
- Antenna placement (near windows, away from metal)
- Carrier coverage maps
- Indoor vs outdoor performance

## Extracting Information: What Modems Reveal

Beyond connectivity, modems expose substantial information about themselves and the networkdata valuable for diagnostics, asset tracking, and compliance.

### Device Information

These commands work across virtually all cellular modems:

**Manufacturer and Model:**
```
AT+CGMI   # Returns manufacturer (e.g., "SIMCOM INCORPORATED")
AT+CGMM   # Returns model (e.g., "SIMCOM_SIM7080G")
```

**Firmware Version:**
```
AT+CGMR   # Returns firmware revision
```
Critical for identifying firmware-specific bugs and determining update requirements.

**IMEI (International Mobile Equipment Identity):**
```
AT+CGSN   # Returns 15-digit IMEI
```
Unique device identifier, required for regulatory compliance and device tracking.

### SIM and Subscriber Information

**ICCID (SIM Card Number):**
```
AT+CCID   # Returns 19-20 digit SIM identifier
```

**IMSI (International Mobile Subscriber Identity):**
```
AT+CIMI   # Returns subscriber identifier
```
Identifies the subscription, not the SIM hardware. Useful for verifying correct SIM insertion.

**Phone Number:**
```
AT+CNUM   # Returns MSISDN if stored on SIM
```
Note: Not all carriers store the phone number on SIM; may return empty.

### Network Information

**Current Operator:**
```
AT+COPS?
+COPS: 0,0,"Safaricom",7
```
Shows selected network and access technology (7 = E-UTRAN/LTE).

**Signal Quality:**
```
AT+CSQ
+CSQ: 17,99
```
First number is RSSI (0-31 scale, 31 = best), second is bit error rate (99 = not known/not detectable).

**Cell Tower Information:**
```
AT+CEREG?   # Extended registration info with cell ID
```
Some modems provide cell tower ID, enabling rough location determination without GPS.

### Why Direct Access Matters

Frameworks often expose only a subset of this information, or require specific API calls that vary by platform. Understanding the underlying AT commands allows:

- Consistent data access across different hardware platforms
- Custom logging for diagnostics and compliance
- Verification of framework behavior
- Building lightweight alternatives when full frameworks are overkill

## Command Optimization: Efficiency Matters

An important discovery was that AT command efficiency directly impacts initialization time and power consumption.

### Sequential vs. Chained Commands

Initial implementation sent commands individually:

```
AT+CMEE=2    (wait for OK)
AT+CMGF=1    (wait for OK)
AT+CNMP=38   (wait for OK)
...
```

With typical command response times of 200-500ms, configuration took ~8 seconds.

### Command Chaining

Most modems support semicolon-based command chaining:

```
AT+CMEE=2;+CMGF=1;+CNMP=38;+CMNB=1;+CGDCONT=1,"IP","safaricom"
```

Same configuration completes in ~2 secondsa 75% reduction. For battery-powered devices that configure on each wake cycle, this difference is substantial.

**Important:** Not all commands can be chained. Commands that return multiple lines of data or take extended processing time may fail when chained. Test thoroughly.

## Power Management Considerations

Understanding power states and transitions is crucial for battery-powered applications.

### Power State Transitions

Modems typically support several power states:

- **Off**: No power, requires full boot sequence
- **Sleep/Low-power**: Main systems powered down, minimal current (~5-50�A)
- **Idle**: Registered to network, ready for data (~2-5mA)
- **Active**: Transmitting/receiving data (~200-400mA, peaks to 2A)

### Wake/Sleep Commands

Enable low-power mode:
```
AT+CSCLK=1   # Enable automatic sleep
```

Configure wake behavior:
```
AT+CPSMS=1,,,<T3412>,<T3324>   # Power-saving mode with timers
```

The actual implementation of sleep modes varies significantly by modem model, but these commands provide starting points for investigation.

### Why This Matters for Abstractions

Many high-level frameworks don't expose fine-grained power control, instead managing sleep automatically based on activity. This can be ideal for simplicity but problematic for:

- Applications with strict power budgets
- Predictable wake/transmit schedules
- Ultra-low-power intermittent operation

Understanding the underlying power commands allows evaluation of whether framework-managed power is suitable or custom control is necessary.

## Communication Fundamentals: UART Considerations

Regardless of hardware platform, certain UART configuration principles apply universally.

### Critical Settings

**Baud Rate:**
Most cellular modems default to 115200 baud, though many support automatic baud detection. Higher rates (230400, 460800) are possible but unnecessary for AT commands and increase error sensitivity.

**Flow Control:**
Hardware flow control (CTS/RTS) is typically optional for AT commands. Enable only if:
- High-speed data transfer planned
- Operating in electrically noisy environments
- Modem documentation specifically requires it

Many simple implementations work reliably without flow control at 115200 baud.

**Data Format:**
Standard configuration is 8N1 (8 data bits, no parity, 1 stop bit). Deviating from this is rarely necessary.

### Response Buffering and Parsing

AT responses vary in length from simple `OK` (4 bytes with CRLF) to multi-line output (e.g., cell tower lists can exceed 1KB). Buffer sizing requires consideration:

- **Minimum**: 256 bytes (sufficient for simple queries)
- **Typical**: 512-1024 bytes (handles most standard commands)
- **Large data operations**: 2048+ bytes (HTTP responses, SMS, etc.)

**Response format example**:
```
AT+COPS?<CR><LF>           # Command echo (if ATE1)
<CR><LF>                    # Blank line
+COPS: 0,0,"Safaricom",7<CR><LF>   # Response data
<CR><LF>                    # Blank line
OK<CR><LF>                  # Final result code
```

**Parsing considerations**:
1. **Line terminators**: Responses use `<CR><LF>` (carriage return + line feed), requiring both characters for proper line detection
2. **Empty lines**: Often appear before and after response data
3. **Echo handling**: If echo is enabled, the first line is the command itself
4. **Unsolicited messages**: Modems may inject status messages (`+CREG: 1`, `CONNECT`, etc.) at any time

**Efficient parsing strategy**:
```
1. Read until timeout or final result code (OK/ERROR)
2. Split response into lines by <CR><LF>
3. Filter out empty lines and echo
4. Extract data lines (those starting with +)
5. Parse result code
```

This approach handles the variability in modem responses across different manufacturers.

### Timeout Management

Different commands require different timeouts:

| Command Type | Typical Timeout | Reason |
|--------------|-----------------|---------|
| Simple queries (AT, ATE1) | 500ms | Immediate response |
| SIM operations (AT+CPIN?) | 1000ms | SIM card access |
| Network queries (AT+COPS?) | 2000-5000ms | Network interaction |
| Data operations | 10000-60000ms | Server communication |

Abstractions often use conservative (long) timeouts, increasing response latency. Direct implementation allows optimization based on actual requirements.

## Diagnostics: What Can Go Wrong

Direct AT command access reveals failure modes that abstractions may report only as generic errors.

### Common Issues and AT-Based Diagnosis

**No response to any AT command:**
- Check physical UART connections (TX/RX swapped is common)
- Verify baud rate match
- Confirm modem has power and completed boot sequence
- Test with simplest possible command: `AT` (not `AT+...`)

**Modem responds but SIM not detected:**
```
AT+CPIN?
+CME ERROR: 10   # SIM not inserted
```
- Verify physical SIM insertion and orientation
- Check SIM contacts (corrosion, debris)
- Try SIM in phone to verify it's not defective

**SIM detected but registration fails:**
```
AT+CREG?
+CREG: 0,2   # Not registered, searching
```
Check signal:
```
AT+CSQ
+CSQ: 99,99   # No signal
```
- Verify antenna connection
- Check carrier coverage in area
- Confirm SIM has active service
- Verify correct network mode (AT+CNMP) and APN configuration

**Registration succeeds but data fails:**
```
AT+CREG?
+CREG: 0,1   # Registered

AT+CGACT?
+CGACT: 1,0  # PDP context not active
```
Indicates registration without data connectionoften wrong APN or no data plan.

### Verbose Error Reporting

Enable detailed error messages:
```
AT+CMEE=2   # Verbose error reporting
```

Instead of generic `ERROR`, you'll receive specific codes:
```
+CME ERROR: 30   # No network service
+CME ERROR: 50   # Incorrect parameters
```

Consult modem documentation for error code meaningsthis level of detail is invaluable for diagnostics.

## When to Use Abstractions vs. Direct Commands

This exploration doesn't argue against using frameworksthey exist for good reasons. Instead, it clarifies when each approach is appropriate.

### Use High-Level Frameworks When:

- Rapid development is priority over understanding
- Standard use cases (simple data connectivity)
- Cross-platform portability needed
- Team unfamiliar with AT commands
- Well-supported modem/framework combination

### Use Direct AT Commands When:

- Debugging framework issues
- Non-standard modem features needed
- Minimal code footprint required
- Custom power management critical
- Learning how modems actually work
- Abstraction overhead unacceptable

### Hybrid Approach

Often optimal: use frameworks for standard operations but maintain ability to send raw AT commands for:
- Diagnostics
- Accessing vendor-specific features
- Power management
- Extracting extended information

## Example Implementation

For those interested in seeing these principles applied, a reference implementation using an RP2040 microcontroller and SIM7080G modem is available:

**GitHub:** https://github.com/lawrenceegr/pico-modem

This implementation demonstrates:
- Minimal UART configuration
- AT command handling with proper timeouts
- Network registration verification
- Information extraction (IMEI, ICCID, signal, etc.)
- Boot timing measurement
- Visual feedback via LED patterns

The code is intentionally straightforward, prioritizing clarity over optimization, making it suitable for learning and adaptation to other platforms.

## Key Takeaways

1. **Timing is critical**: Documented delays are often optimistic; measure actual requirements and add safety margins.

2. **Consumer SIM cards work**: Special IoT SIMs are not always necessary; standard prepaid cards function with appropriate configuration.

3. **AT commands are universal**: Mastering basic AT commands provides diagnostic capability across platforms and hardware.

4. **Registration is complex**: Network attachment requires proper configuration and can take significant time; failures have specific, diagnosable causes.

5. **Information is accessible**: Modems expose extensive data about devices, subscribers, and networks through standardized commands.

6. **Optimization matters**: Command chaining and efficient sequencing reduce initialization time and power consumption.

7. **Abstractions hide complexity**: Frameworks simplify development but can obscure critical details needed for debugging and optimization.

## Conclusion

Working directly with AT commands and UART communication strips away the convenience of high-level abstractions, but the insights gained are invaluable. Understanding actual modem behaviorboot timing, registration sequences, command responsesprovides diagnostic capabilities and optimization opportunities that remain hidden when relying solely on frameworks.

This knowledge applies broadly: whether using Zephyr, FreeRTOS, Arduino, or bare-metal implementations, the fundamental principles of cellular modem communication remain constant. The specific APIs and abstractions change, but AT commands persist as the universal language of modem interaction.

For production systems, use whatever tools and frameworks make sense for your requirements. But when things go wrong, or when you need capabilities beyond what abstractions provide, understanding what's actually happening at the AT command level transforms debugging from guesswork into systematic diagnosis.


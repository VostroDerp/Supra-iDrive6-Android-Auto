# Car GPS for Android Auto (upstream #73): design note

Desk groundwork, 2026-11-03 (task D-73). The car side is still open: runbook G1 answers it. Nothing here has been
built or car-tested yet.

**2026-11-04: mostly superseded.** Tom's upstream PR #77 (WIP, on #78) reads the fix from MOST instead: FBlock 0x52
inst 00 fkt 0x440 (the navigation's raw GNSS fix, ~1 Hz) and 0xCD/01 fkt 0xC00 (receiver status), and sends
LOCATION and GPS_SATELLITE. Sections 2 (wire format) and 6 (rate) still hold; sections 3-5 (SOME/IP) are the fallback
if the ID5 does not send those MOST messages. Runbook G1 now tests #77 on the ID5.

## 1. What happens today

- Every session the phone sends SensorStartRequest (channel 1, msg 0x8001) for types 1, 2, 3, 7, 8, 10, 13, 19,
  21 and 22 (runs 13–15). It asks because our service descriptor advertises them (`include/dhu_sdp.h`, channel 1
  sensor list: 1, 2, 3, 5, 6, 7, 8, 10, 13, 19, 20, 21, 22).
- `AASession` (upstream `src/aa_session.cpp`, the 0x8001 branch and `sendSensorEvent`) acks every request but only
  sends data for 10 (night mode) and 13 (driving status). Others are "ack only".
- So the phone sees a location sensor that never reports, and uses its own GPS.
- The descriptor sets `location_characterization = 256` (bytes `0x10 0x80 0x02`), which is RAW_GPS_ONLY.

## 2. Android Auto wire format (checked against opencardev/aasdk protos)

A sensor event is msg 0x8003 on channel 1: a `SensorBatch`. Its field number equals the sensor type, which is
why night mode is tag `0x52` (field 10) and driving status `0x6A` (field 13) in the current code. Location is
field 1 (tag `0x0A`), a repeated `LocationData`:

| Field | Tag | Type | Unit |
|---|---|---|---|
| timestamp | 1 | uint64 | deprecated, optional |
| latitude_e7 | 2 | int32 (required) | degrees × 10^7 |
| longitude_e7 | 3 | int32 (required) | degrees × 10^7 |
| accuracy_e3 | 4 | uint32 | metres × 10^3 |
| altitude_e2 | 5 | int32 | metres × 10^2 |
| speed_e3 | 6 | int32 | m/s × 10^3 |
| bearing_e6 | 7 | int32 | degrees × 10^6 |

The units come from the `_eN` names (the protos have no comments). int32 negatives encode as 10-byte varints in
proto2, so latitudes/longitudes south or west need that case in the encoder.

`LocationCharacterization` is a bitmask: PRIOR_LOCATIONS 1, GYROSCOPE_FUSION 2, ACCELEROMETER_FUSION 4,
COMPASS_FUSION 8, WHEEL_SPEED_FUSION 16, STEERING_ANGLE_FUSION 32, CAR_SPEED_FUSION 64, DEAD_RECKONED 128,
RAW_GPS_ONLY 256. If the car's source is the navigation's dead-reckoned position, the descriptor should say so (e.g.
128 | 16 | 2) instead of 256.

Related types the phone also asks for: 2 compass (`CompassData`), 3 speed (`SpeedData`), 21 GPS satellites
(`GpsSatelliteData`). Location alone is the first step; the others can follow.

## 3. What the car has (from the pulled `Connectivity`, no car time yet)

- **BMW already feeds car GPS to CarPlay phones.** Connectivity's iAP2 code (`DMMIAP2Client::onLocationInfoUpdate`,
  `requestSendGPSData`) sends GPGGA, GPRMC, GPGSV, GPHDT and PASCD sentences to the iPhone. PASCD (vehicle speed)
  is composed by Connectivity itself (`PASCD,%.3f,C,%c,0,%d%s`).
- Its input is the SOME/IP service `BMW_INFOTAINMENT_SatellitePositioning`: events `UpdatePosition`
  (`nmeaString`, `delayCounter`) and `UpdatePPS` (`pps`, `delayCounter`), plus a `BasicPearlChain` getter/notifier.
  The client uses `libsys_someip.cpp-ne.so.8` / `libsys_someip_mocca_base.cpp-ne.so.7`.
- Who serves it is not known yet. Candidates: `NavigationPositioning`
  (`/fs/sda0/opt/nav/eceusa/bin/NavigationPositioning`, fed by `dev-navsens` on `/dev/ser3`), or another ECU on
  the car network (the client is named `SomeIPATMClient_SatellitePositioning`).
- `dev-navsens` is started by starter.cfg with `-C/etc/gps_G6_V7_ver20.cfg -Gd=/dev/ser3,r=/dev/sysregs/GpsReset`.
  Never read its nodes (a second reader steals bytes).

## 4. Source options (G1 picks)

| | Source | For | Against |
|---|---|---|---|
| A | A readable output of NavigationPositioning (PPS object or resmgr node) | Simplest; possibly dead-reckoned (works in tunnels) | Unknown whether one exists: G1's PPS and `/dev` listing |
| B | Subscribe to SOME/IP SatellitePositioning ourselves | The exact feed BMW gives CarPlay; NMEA is easy to parse | SOME/IP service discovery and subscription to implement (or link BMW's lib); unknown if served without CarPlay coded |
| C | `/dev/ser3` or dev-navsens directly | Raw receiver | Ruled out: steals bytes from the car's own navigation |

## 5. NMEA → LocationData (options B, and A if it is NMEA)

- RMC: time and date (timestamp), status `A` (send) or `V` (skip), lat/lon (ddmm.mmmm → degrees × 10^7), speed in
  knots (× 0.514444 → m/s), course (bearing).
- GGA: fix quality (0 = skip), altitude (metres), HDOP. Accuracy is not in NMEA; HDOP × ~5 m is a common estimate
  (label it as one). GST, if the receiver sends it, has real error figures.
- Pair RMC and GGA by their time field; send once per fix.

## 6. Rate and timing

Forward each new fix when it arrives, at the source's own rate (typically 1 Hz for a GNSS receiver, possibly more
from dead reckoning), with its timestamp. Don't resend an old fix on a timer and don't interpolate; the phone fuses
it. Dedupe by fix time. If fixes stop (tunnel with raw GPS), send nothing rather than a stale position.

## 7. Implementation sketch (upstream style)

- `src/nmea_parse.hpp`: header-only RMC/GGA parser with checksum check. Host test with real sentences captured in G1
  (`tests/fixtures/`).
- `src/location_msg.hpp`: header-only `LocationData` → `SensorBatch` encoder (varints, negative int32). Host test
  against hand-encoded bytes.
- `AASession`: remember that sensor 1 was started (like `night_started_`), plus `sendLocation(const Fix&)` under
  the same mutex pattern.
- A location source thread (option A or B), started only when `NBT_CAR_GPS` is on (`nbt::envFlag`, default off
  until car-verified). It hands fixes to the current session.
- Descriptor: set `location_characterization` from the source (256 raw, or the fusion bits).
- `readme.md`: document `NBT_CAR_GPS`, marked "Not car-verified" until it is.

## 8. Open, for G1

1. Which process serves SatellitePositioning, and is it running on a car without CarPlay coded?
2. Is there a readable position output (PPS, `/dev/name/local`, `/dev`) from NavigationPositioning?
3. Update rate and fields of whatever we find; one capture of real sentences for the test fixture.
4. Pull `NavigationPositioning`, `/etc/gps_G6_V7_ver20.cfg` and the two `libsys_someip*` libraries for desk work.

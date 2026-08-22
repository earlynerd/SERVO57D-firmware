# Motor-Drive Operating Limits

Status: firmware 0.26.0 is the flashed, bench-qualified relative-position
baseline. Firmware 0.27.1 is the host/build-validated phase-prediction and
motion-policy candidate, includes firmware 0.26.1's independent encoder-
liveness guard, and permits velocity evaluation through 16 rev/s (960 RPM).
The velocity loop has
passed mirrored low-speed deadline/polarity, STOP, physical Right-button, and
hand-loaded saturation/recovery checks. Initial velocity qualification is
accepted; physical feedback-loss injection is indefinitely deferred on this
assembly while its fault/ZERO contract remains host/native tested. A validated
point is evidence, not automatically a request ceiling.

> **Evaluation warning:** the firmware deliberately accepts requests in
> unqualified operating zones. Acceptance means the request fits the software,
> estimator, actuator, and fault-containment contracts; it does not promise good
> tracking or safe continuous operation for a particular motor, supply, load,
> cooling arrangement, or mechanism. Saturation, stalls, faults, heating,
> vibration, and energetic motion are expected possible outcomes of boundary
> testing. Use explicit test bounds, telemetry, a current-limited supply, an
> appropriate fixture, and immediate physical STOP/supply-cutoff access.

## Limit classes

- **Hard constraint:** derived from silicon, topology, sensing range, timing, or
  another physical invariant. It may not be exceeded by configuration.
- **Validated envelope:** passed a documented bench procedure on a named board,
  motor, voltage, duration, and load condition.
- **Evaluation envelope:** deliberately enabled beyond the validated envelope so
  the next capability can be measured through the production authority path.
- **Motor/application configuration:** selected for the attached motor and load;
  it is not a universal property of the controller board.
- **Implementation constraint:** a real limitation of the present algorithm or
  schedule that must be engineered away to reach the performance target.

## Current firmware inventory

| Quantity | Firmware 0.27.1 value | Class and basis | Enforcement owner | Status / next evidence |
| --- | ---: | --- | --- | --- |
| Current scale | 6.059 mA/count nominal | Measured conversion on the tested board: 3.3 V ADC reference, 6.65 gain, 20 mΩ shunt | ADC conversion and host tools | Verified on one board; production tolerance and temperature remain open |
| Aligned q-current request | ±495 counts, ±2.999 A nominal | Evaluation envelope matching the attached motor's reported 3 A rating | `aligned_torque_controller` | 757.4 mA is validated; 1.503 A, 2.25 A, and 2.999 A are already permitted measurement points |
| Phase-current backend request | ±495 counts per winding | Evaluation envelope shared by torque, alignment, and the retained production diagnostic | `phase_current_loop` through `current_loop_backend` | Must advance with the q-current envelope; it is not the board's physical rating |
| Raw-current trip | ±600 counts, ±3.635 A nominal | Provisional protection threshold, more than 20% above maximum request | `phase_current_loop` in the 20 kHz ADC completion path | Immediate shutdown is tested; protection-grade tolerance, amplifier clipping, and temperature remain open |
| Phase-voltage command | 700 permille of bus | Timing constraint: all switching edges end by 70% of the carrier | `phase_current_loop` | Leaves at least 5 us before the 80%-carrier current sample; performance may expand only with a new measured sampling strategy |
| Active-leg duty | 800 permille maximum | Topology/timing constraint from the retained 200-permille duty margin | `phase_current_loop` and TIM3 backend | Independent of the 700-permille phase-voltage clamp; retain until switching/bootstrap measurements justify change |
| Current-reference slew | 10,000 counts/s, about 60.59 A/s | Evaluation envelope; reaches the 495-count maximum in 49.5 ms, about 7.6 times the measured 6.5 ms current-loop rise | `aligned_torque_controller` | Intentionally permits transient evaluation; use measured tracking/overshoot to select the next value |
| Aligned-actuator observed velocity | 20 rev/s, 1,200 RPM | Independent motion shutdown aligned with the current estimator plausibility boundary, not a command target | `aligned_torque_controller` | Leaves transient room above the 16 rev/s command range; raise with the estimator work required for 3,000+ RPM |
| Mechanical acceleration during open torque | 1,000 rev/s² observed | Evaluation ceiling chosen above expected filtered-estimator transients, not a motor command or performance rating | `aligned_torque_controller` | Prevents the initial 20 rev/s² candidate from masking current/speed boundaries; must become motor/application configuration with the closed velocity loop |
| Accepted feedback interval | 2,000 us maximum | Timing policy for the nominal 1 kHz encoder schedule, permitting one late interval | `aligned_torque_controller` | Active hardware observations were about 981-1,001 us; remeasure under outer-loop load |
| Fast electrical-phase prediction | Every 50 us; 2,000 us maximum observation age; 7 us nominal output lead; observations through 20 rev/s | Implementation timing contract derived from the 20 kHz ADC-completion/preload pipeline | `electrical_phase_predictor` through `current_loop_backend` | Host-tested at 4 rev/s/50-cycle geometry, both directions, stale rejection, and timer wrap; scope the true current DMA-completion-to-preload lead, quantify encoder-angle acquisition versus its completion timestamp, and record worst-case ISR cycles while stepping through the exposed range |
| Encoder production progress | 3,000 us maximum without a newly observed accepted sample | Independent total-silence guard: the 2 ms controller limit plus one nominal 1 ms foreground snapshot opportunity | `encoder_liveness` and the foreground supervisor prerequisite | Host-tested across stale state, recovery, sample-counter wrap, and microsecond-timer wrap; ordinary flash/smoke validation remains |
| Estimator velocity filter | `alpha = 0.125` per accepted 1 kHz sample | Implementation configuration; approximately eight samples of smoothing, not a hardware limit | `angle_tracker` | Load-bearing outer-loop behavior that must be measured/tuned as velocity increases |
| Estimator motion plausibility | 20 rev/s, 1,200 RPM maximum | Implementation threshold for rejecting implausible sample-to-sample motion | `angle_tracker` | Defines the present architecture boundary and leaves 20% room above the 16 rev/s command range; raising it toward at least 50 rev/s is active speed work |
| Torque duration | 3 through 2,147,483,647 ms | Timing-derived: 3 ms allows a pre-deadline update at the 2 ms feedback limit; the maximum is the signed modulo-32-bit half-range | Command service and `aligned_torque_controller` | Caller always supplies a finite deadline; duration is not a thermal/current proxy or communications lease |
| Velocity target | ±16 rev/s, ±960 RPM | Evaluation permission using 80% of the current 20 rev/s estimator boundary; the validated point remains 1 rev/s | Command service and `velocity_controller` | Stage both signs through 2, 4, 8, 12, and 16 rev/s while measuring current tracking, prediction quality, voltage effort, mechanics, and faults |
| Inner velocity-reference acceleration | 256 rev/s² | Evaluation slew with fourfold headroom over the maximum position-profile acceleration | `velocity_controller` | Direct velocity commands use this ramp; current saturation and plant inertia still determine achieved acceleration |
| Velocity-command current | 1-495 counts, up to about 2.999 A nominal per request | Evaluation permission matching the attached motor's reported rating and existing aligned actuator | `velocity_controller`, then independently slewed and bounded by `aligned_torque_controller` | The caller chooses the test current; 757 mA is the highest validated point and higher accepted values are deliberately unqualified |
| Velocity feedback speed | 20 rev/s, 1,200 RPM maximum observed | Independent runaway/feedback plausibility ceiling shared with the estimator and aligned actuator | `velocity_controller` and `aligned_torque_controller` | This is a shutdown threshold, not a commandable speed rating |
| Velocity feedback interval | 2,000 us maximum | Same timing policy as the 1 kHz aligned actuator | `velocity_controller` and `aligned_torque_controller` | Remeasure PendSV execution and encoder interval under active PI load |
| Velocity duration | 3 through 2,147,483,647 ms | Finite wrap-safe deadline contract | Command service, `velocity_controller`, and aligned actuator | The first bench run uses seconds, not the representational maximum |
| Velocity PI gains | Kp 100 counts/(rev/s), Ki 200 counts/rev | Low-gain implementation candidate with symmetric anti-windup at the per-command current limit | `velocity_controller` / `pi_controller` | Tune from signed step response; not yet a motor-independent product default |
| Relative position travel | ±100 revolutions per command | Evaluation envelope for a continuous rotary axis; separate from the Q16.16 absolute-status representation | Command service and `position_controller` | The caller chooses travel appropriate to the fixture; ±0.25 revolution is the accepted comparison point, not an unlock condition |
| Position trajectory velocity | 0-16 rev/s, 0-960 RPM magnitude | Per-command profile bound matching direct velocity permission | `position_controller`, then independently by `velocity_controller` | The caller may explore the exposed range while retaining separate current, acceleration, following-error, and deadline bounds |
| Position correction velocity | ±17 rev/s inner target | Exact headroom for the 16 rev/s profile plus `Kp 4/s × 0.25 rev` maximum following error | `position_controller` and dynamic `velocity_controller` target | Prevents the position correction from being clipped at profile speed; host regression requires a target above the profile ceiling |
| Position trajectory acceleration | 0-64 rev/s² | Per-command trapezoidal-profile permission with the inner velocity slew fixed at four times this value | `motion_profile` / `position_controller`, then velocity-reference slew | Higher requested profile acceleration makes tracking harder; use it to shape the test, not to mask insufficient torque |
| Position-command current | 1-495 counts, up to about 2.999 A nominal | Same caller-selected actuator envelope as velocity, independently retained per move | `position_controller`, `velocity_controller`, and aligned actuator | The caller chooses current from the live policy; saturation remains telemetry and cannot defeat following-error shutdown |
| Position start velocity | 0.1 rev/s maximum magnitude | Entry-condition policy preventing capture of an already-moving shaft into a relative trajectory | Foreground preflight and `position_controller` at actual start | Rejected commands must not energize the bridge |
| Position following error | 0.25 revolution maximum magnitude | Independent loss-of-tracking shutdown, not a trajectory or electronics capability ceiling | `position_controller` | Retained because it detects an actual profile/rotor divergence; cascade rate and velocity headroom no longer consume its correction authority by construction |
| Position completion | 0.002 revolution, 0.02 rev/s, 50 consecutive 1 kHz samples | Evaluation settling policy requiring position and speed agreement for about 50 ms | `position_controller` | Validate repeatability and tune from captured endpoint error, not feel |
| Position feedback interval | 2,000 us maximum | Same deterministic 1 kHz feedback-age contract as velocity | `position_controller`, velocity controller, and aligned actuator | Any violation faults and converges on `ZERO` |
| Position duration | 100 through 2,147,483,647 ms | Finite wrap-safe deadline; expiration releases normally but reports `deadline`, not `settled` | Command service and `position_controller` | Caller must choose a duration long enough for the requested profile and settling time |
| Rotating-current diagnostic frequency | 0.001 through 250 electrical Hz | Evaluation envelope matching 5 rev/s on the 50-cycle/rev motor | Product diagnostic command path | The 1 kHz reference schedule provides only four points/cycle at 250 Hz; this is useful boundary evidence, not a quality guarantee |
| Rotating-current diagnostic duration | 3 through 2,147,483,647 ms | Same wrap-safe finite-deadline basis as aligned torque | Product diagnostic command path | Replaces the inherited 100-60,000 ms commissioning window |

## Active vendor-stated performance requirements

Makerbase advertises 12-24 V operation, a 0-5200 mA current setting, 20 kHz
current/velocity/position loops, 3000+ RPM, and up to 256 "subdivision" with 16
as default. The precise meanings and test conditions are not yet established:
the current may be peak, RMS, per-phase, or a configuration scale;
"subdivision" may describe step-input interpolation rather than encoder or
closed-loop position resolution; and the three 20 kHz claims may not mean three
independent full controller calculations.

Matching these capabilities—or producing concrete evidence of the board/control
change required for each one—is active project work. They are not yet accepted
operating limits, and safe expansion remains measurement-gated, but they must
not be treated as optional or deferred until unrelated work is exhausted. The
present product runs the winding-current loop and electrical-phase/A-B mapping
at 20 kHz, while it accepts encoder observations and updates torque demand,
velocity, and position at 1 kHz. The
current tested motor is reported as 3 A, so board-level investigation toward
5.2 A requires a suitable motor or load fixture and separate electrical and
thermal qualification. Moving toward 24 V and 3000+ RPM likewise requires
bus-protection/switching evidence plus a faster phase-estimation and outer-loop
architecture; the 16 rev/s command range and 20 rev/s estimator threshold now
expose the present architecture boundary while estimator work toward 50+ rev/s
remains an active requirement.

## Performance progression

The next envelopes are product-development gates, not permanent ceilings:

1. Measure 1.503 A, 2.25 A, and the already permitted 2.999 A point through the
   existing supervisor, aligned q-current, 20 kHz current loop, telemetry, STOP,
   and common fault path. Permission is already open; the sequence organizes
   evidence rather than blocking access.
2. Bench-validate the 20 kHz phase predictor at the accepted low-speed point,
   scope its actual output lead and worst-case ISR execution, then evaluate
   through 2, 4, 8, 12, and 16 rev/s. At 16 rev/s it provides 25 fast-loop
   updates per electrical cycle; current tracking, voltage use, filtered-
   velocity lag, mechanics, and torque ripple identify the next concrete change.
3. Exercise the 64 rev/s² position profile beneath the 256 rev/s² inner slew and
   confirm that the 17 rev/s corrected target recovers lag without creating a
   following-error fault. Then make motor current, velocity, acceleration, and
   PI gains persistent application configuration while preserving the exposed
   evaluation permission.
4. Qualify the motor's reported 3 A rating only after current-sense
   clipping/bandwidth, switching waveforms, supply behavior, shunt and MOSFET
   temperature, and current-loop transients pass staged measurements. Firmware
   permission to evaluate the point is not qualification.
5. Complete the remaining relative-position gates: physical Right-button stop
   and loaded following-error behavior. Mirrored settling, deadline, and generic
   STOP already pass on firmware 0.26.0.
6. After the attached motor's 3 A point is qualified, characterize the board's
   advertised 5.2 A setting with an appropriate motor/load and thermal fixture;
   do not apply 5.2 A to the present 3 A motor. Stage 24 V separately after bus
   protection and switching waveforms are measured, then extend estimator,
   phase prediction, velocity, and position scheduling toward the advertised
   speed and loop-rate targets without merging their independent limits.

The board uses eight discrete MOSFETs and a heatsink/gap-pad assembly, which is
consistent with substantial power-stage ambition. A MOSFET headline current
rating does not by itself establish board continuous current: shunts, copper,
connectors, gate drive, switching loss, airflow, and the actual thermal path
remain part of the measured hard ceiling.

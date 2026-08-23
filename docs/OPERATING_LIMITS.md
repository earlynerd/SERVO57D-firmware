# Motor-Drive Operating Limits

Status: firmware 0.29.2 / protocol 1.12 is the current source candidate, while
firmware 0.29.1 / protocol 1.12 is flashed. The flashed image inherits the
identity, readiness, generation-3 calibration, VBUS, and bounded positive-
velocity evidence through a 12 rev/s request. Its fault-recovery command clears
following-error and current-backend chains without resetting, but a later
bounded move exposed the former matched 2 ms controller/predictor ceiling.
Firmware 0.29.1 separated that horizon and retained evidence proving a later
rejection was unsigned -424 us from a preempted SysTick epoch, not true stale
encoder production. Firmware 0.29.2 reconciles that clock race while preserving
the 2 ms controller and 3 ms predictor/liveness bounds. The firmware includes an independent
encoder-liveness guard and permits velocity evaluation through 16 rev/s
(960 RPM). At 24 V, +8 rev/s reaches target without q-current clipping;
+12 rev/s saturates q-demand and phase voltage in most samples and plateaus near
10 rev/s. All predictor, encoder, backend, current-loop, supervisor, reset, and
panic fault channels remain clear. Firmware 0.26.0 remains the bench-
qualified relative-position baseline. Firmware 0.28.0 restores production VBUS
acquisition without delaying the current-loop DMA event and makes amperes and
volts the primary host-facing electrical units. Inactive status reported
23.829 V at the 24 V supply setting; a one-second 1 rev/s / 606 mA run retained
advancing VBUS samples and clean current-loop/deadline/terminal-release state.
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

| Quantity | Firmware 0.29.2 source value | Class and basis | Enforcement owner | Status / next evidence |
| --- | ---: | --- | --- | --- |
| Current scale | 6.059 mA/count nominal | Measured conversion on the tested board: 3.3 V ADC reference, 6.65 gain, 20 mΩ shunt | ADC conversion and host tools | Verified on one board; production tolerance and temperature remain open |
| Bus-voltage scale | 13.22 mV/count nominal | Tested-board 3.3 V ADC reference and fitted 15.4 kOhm/1 kOhm divider | Automatic-injected PA3 ADC acquisition and host conversion | Inactive 0.28.0 status reported 23.829 V at the 24 V supply setting; all 22 active samples held 23.776-23.815 V with advancing samples and no ADC/deadline fault |
| Aligned q-current request | ±495 counts, ±2.999 A nominal | Evaluation envelope matching the attached motor's reported 3 A rating | `aligned_torque_controller` | 757.4 mA is validated; 1.503 A, 2.25 A, and 2.999 A are already permitted measurement points |
| Phase-current backend request | ±495 counts per winding | Evaluation envelope shared by torque, alignment, and the retained production diagnostic | `phase_current_loop` through `current_loop_backend` | At +6 rev/s/12 V, requested-vector magnitude reached 495 counts but measured-vector magnitude peaked near 148 counts because phase voltage clipped; the request value is not delivered-current evidence or the board's physical rating |
| Raw-current trip | ±600 counts, ±3.635 A nominal | Provisional protection threshold, more than 20% above maximum request | `phase_current_loop` in the 20 kHz ADC completion path | Immediate shutdown is tested; protection-grade tolerance, amplifier clipping, and temperature remain open |
| Phase-voltage command | 70% of measured bus; nominally 8.4 V at 12 V or 16.8 V at 24 V | Timing constraint: all switching edges end by 70% of the carrier | `phase_current_loop` | At +6 rev/s/12 V, one phase reached this clamp in 27/62 active host samples. The 24 V repeat removed that clamp and reduced velocity RMS error; +12 rev/s/24 V reaches it again. Expand only with a newly measured sampling strategy |
| Active-leg duty | 80% maximum | Topology/timing constraint from the retained 20% duty margin | `phase_current_loop` and TIM3 backend | Independent of the 70%-of-bus phase-voltage clamp; retain until switching/bootstrap measurements justify change |
| Current-reference slew | 10,000 counts/s, about 60.59 A/s | Evaluation envelope; reaches the 495-count maximum in 49.5 ms, about 7.6 times the measured 6.5 ms current-loop rise | `aligned_torque_controller` | Intentionally permits transient evaluation; use measured tracking/overshoot to select the next value |
| Aligned-actuator observed velocity | 20 rev/s, 1,200 RPM | Independent motion shutdown aligned with the current estimator plausibility boundary, not a command target | `aligned_torque_controller` | Leaves transient room above the 16 rev/s command range; raise with the estimator work required for 3,000+ RPM |
| Mechanical acceleration during open torque | 1,000 rev/s² observed | Evaluation ceiling chosen above expected filtered-estimator transients, not a motor command or performance rating | `aligned_torque_controller` | Prevents the initial 20 rev/s² candidate from masking current/speed boundaries; must become motor/application configuration with the closed velocity loop |
| Accepted feedback interval | 2,000 us maximum | Timing policy for the nominal 1 kHz encoder schedule, permitting one late interval | `aligned_torque_controller` | Active hardware observations were about 981-1,001 us; remeasure under outer-loop load |
| Fast electrical-phase prediction | Every 50 us; 3,000 us maximum observation age; 7 us nominal output lead; observations through 20 rev/s | Timing contract: one nominal 1 ms dispatch margin beyond the controllers' 2 ms timestamp interval, never beyond the independent 3 ms production-liveness guard | `electrical_phase_predictor` through `current_loop_backend` | Schema-2 evidence measured successful ages through 1,475 us and isolated a false unsigned -424 us stale age to the old timebase. Flash 0.29.2 and repeat alternating signed moves; then scope acquisition, preload lead, and worst-case ISR cycles |
| Encoder production progress | 3,000 us maximum without a newly observed accepted sample | Independent total-silence guard: the 2 ms controller limit plus one nominal 1 ms foreground snapshot opportunity | `encoder_liveness` and the foreground supervisor prerequisite | Host-tested across stale state, recovery, sample-counter wrap, and microsecond-timer wrap; ordinary 0.27.1 identity/readiness and bounded-motion smoke passed with 998-1,002 us active capture intervals and no liveness fault |
| Estimator velocity filter | `alpha = 0.125` per accepted 1 kHz sample | Implementation configuration; approximately eight samples of smoothing, not a hardware limit | `angle_tracker` | Load-bearing outer-loop behavior that must be measured/tuned as velocity increases |
| Estimator motion plausibility | 20 rev/s, 1,200 RPM maximum | Implementation threshold for rejecting implausible sample-to-sample motion | `angle_tracker` | Defines the present architecture boundary and leaves 20% room above the 16 rev/s command range; raising it toward at least 50 rev/s is active speed work |
| Torque duration | 3 through 2,147,483,647 ms | Timing-derived: 3 ms allows a pre-deadline update at the 2 ms feedback limit; the maximum is the signed modulo-32-bit half-range | Command service and `aligned_torque_controller` | Caller always supplies a finite deadline; duration is not a thermal/current proxy or communications lease |
| Velocity target | ±16 rev/s, ±960 RPM | Evaluation permission using 80% of the current 20 rev/s estimator boundary | Command service and `velocity_controller` | At 24 V, positive +8 rev/s reaches/passes target without q-current clipping; +12 rev/s saturates q-demand and phase voltage and plateaus near 10 rev/s. Improve current tracking and run the negative sign before treating 12 or 16 rev/s as achieved performance |
| Inner velocity-reference acceleration | 256 rev/s² | Evaluation slew with fourfold headroom over the maximum position-profile acceleration | `velocity_controller` | Direct velocity commands use this ramp; current saturation and plant inertia still determine achieved acceleration |
| Velocity-command current | 6.059 mA-2.999 A nominal per request (raw 1-495 counts) | Evaluation permission matching the attached motor's reported rating and existing aligned actuator | `velocity_controller`, then independently slewed and bounded by `aligned_torque_controller` | The +6 rev/s/12 V test exercised the requested/applied q-current ceiling without a fault, but measured-vector magnitude peaked near 0.90 A because voltage clipped. This is command-path evidence, not delivered-current or thermal qualification |
| Velocity feedback speed | 20 rev/s, 1,200 RPM maximum observed | Independent runaway/feedback plausibility ceiling shared with the estimator and aligned actuator | `velocity_controller` and `aligned_torque_controller` | This is a shutdown threshold, not a commandable speed rating |
| Velocity feedback interval | 2,000 us maximum | Same timing policy as the 1 kHz aligned actuator | `velocity_controller` and `aligned_torque_controller` | Captured intervals remained 998-1,002 us under the +5/+6 rev/s PI load; retain timing telemetry as controller load grows |
| Velocity duration | 3 through 2,147,483,647 ms | Finite wrap-safe deadline contract | Command service, `velocity_controller`, and aligned actuator | The first bench run uses seconds, not the representational maximum |
| Velocity PI gains | Kp 100 counts/(rev/s), Ki 200 counts/rev | Low-gain implementation candidate with symmetric anti-windup at the per-command current limit | `velocity_controller` / `pi_controller` | The 24 V +6/+8 evidence separates the earlier 12 V voltage clamp from velocity gain behavior. Improve the underlying high-frequency current tracking and then tune both signed velocity responses. These gains are not a motor-independent product default |
| Relative position travel | ±100 revolutions per command | Evaluation envelope for a continuous rotary axis; separate from the Q16.16 absolute-status representation | Command service and `position_controller` | The caller chooses travel appropriate to the fixture; ±0.25 revolution is the accepted comparison point, not an unlock condition |
| Position trajectory velocity | 0-16 rev/s, 0-960 RPM magnitude | Per-command profile bound matching direct velocity permission | `position_controller`, then independently by `velocity_controller` | The caller may explore the exposed range while retaining separate current, acceleration, following-error, and deadline bounds |
| Position correction velocity | ±17 rev/s inner target | Exact headroom for the 16 rev/s profile plus `Kp 4/s × 0.25 rev` maximum following error | `position_controller` and dynamic `velocity_controller` target | Prevents the position correction from being clipped at profile speed; host regression requires a target above the profile ceiling |
| Position trajectory acceleration | 0-64 rev/s² | Per-command trapezoidal-profile permission with the inner velocity slew fixed at four times this value | `motion_profile` / `position_controller`, then velocity-reference slew | Higher requested profile acceleration makes tracking harder; use it to shape the test, not to mask insufficient torque |
| Position-command current | 6.059 mA-2.999 A nominal (raw 1-495 counts) | Same caller-selected actuator envelope as velocity, independently retained per move | `position_controller`, `velocity_controller`, and aligned actuator | The caller chooses current from the live policy; saturation remains telemetry and cannot defeat following-error shutdown |
| Position start velocity | 0.1 rev/s maximum magnitude | Entry-condition policy preventing capture of an already-moving shaft into a relative trajectory | Foreground preflight and `position_controller` at actual start | Rejected commands must not energize the bridge |
| Position following error | 0.25 revolution maximum magnitude | Independent loss-of-tracking shutdown, not a trajectory or electronics capability ceiling | `position_controller` | Retained because it detects an actual profile/rotor divergence; cascade rate and velocity headroom no longer consume its correction authority by construction |
| Position completion | 0.002 revolution, 0.02 rev/s, 50 consecutive 1 kHz samples | Evaluation settling policy requiring position and speed agreement for about 50 ms | `position_controller` | Validate repeatability and tune from captured endpoint error, not feel |
| Position feedback interval | 2,000 us maximum | Same deterministic 1 kHz feedback-age contract as velocity | `position_controller`, velocity controller, and aligned actuator | Any violation faults and converges on `ZERO` |
| Position duration | 100 through 2,147,483,647 ms | Finite wrap-safe deadline; expiration releases normally but reports `deadline`, not `settled` | Command service and `position_controller` | Caller must choose a duration long enough for the requested profile and settling time |
| Rotating-current diagnostic frequency | 0.001 through 250 electrical Hz | Evaluation envelope matching 5 rev/s on the 50-cycle/rev motor | Product diagnostic command path | The 1 kHz reference schedule provides only four points/cycle at 250 Hz; this is useful boundary evidence, not a quality guarantee |
| Rotating-current diagnostic duration | 3 through 2,147,483,647 ms | Same wrap-safe finite-deadline basis as aligned torque | Product diagnostic command path | Replaces the inherited 100-60,000 ms commissioning window |

Current ADC counts are not bus-voltage dependent. They measure shunt voltage
through the current-sense amplifier and ADC reference, so the same current has
the same nominal count value at 12 V and 24 V. Bus voltage determines how much
phase voltage the bridge can command and therefore whether the loop can force
measured current to follow its request at a given electrical frequency. The
host presents amperes and volts first; raw counts and ratios remain available
for calibration, saturation, and controller diagnosis.

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
2. The 20 kHz phase predictor now has clean positive-direction bench evidence
   through a 12 rev/s request. Raising the bus from 12 V to 24 V removes the
   +6 rev/s clamp and reduces RMS velocity error from 1.197 to 0.638 rev/s.
   +8 rev/s reaches target; +12 rev/s saturates q-demand and phase voltage and
   plateaus near 10 rev/s. Improve current-loop bandwidth/phase tracking, scope
   actual output lead and worst-case ISR execution, and test the negative sign
   before using +16 rev/s as performance evidence. At 16 rev/s the predictor provides 25 fast-loop updates per
   electrical cycle; current tracking, voltage use, filtered-velocity lag,
   mechanics, and torque ripple identify the next concrete change.
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

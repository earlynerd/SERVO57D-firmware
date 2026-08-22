# Motor-Drive Operating Limits

Status: firmware 0.25.1 is the flashed, bench-qualified velocity baseline.
Firmware 0.26.0 is the host/build-validated position candidate and expands the
commandable velocity evaluation envelope to 4 rev/s (240 RPM). The velocity loop has
passed mirrored low-speed deadline/polarity, STOP, physical Right-button, and
hand-loaded saturation/recovery checks. Initial velocity qualification is
accepted; physical feedback-loss injection is indefinitely deferred on this
assembly while its fault/ZERO contract remains host/native tested. A validated
point is evidence, not automatically a request ceiling.

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

| Quantity | Firmware 0.26.0 value | Class and basis | Enforcement owner | Status / next evidence |
| --- | ---: | --- | --- | --- |
| Current scale | 6.059 mA/count nominal | Measured conversion on the tested board: 3.3 V ADC reference, 6.65 gain, 20 mΩ shunt | ADC conversion and host tools | Verified on one board; production tolerance and temperature remain open |
| Aligned q-current request | ±495 counts, ±2.999 A nominal | Evaluation envelope matching the attached motor's reported 3 A rating | `aligned_torque_controller` | 757.4 mA is validated; evaluate 1.503 A and 2.25 A before 2.999 A |
| Phase-current backend request | ±495 counts per winding | Evaluation envelope shared by torque, alignment, and the retained production diagnostic | `phase_current_loop` through `current_loop_backend` | Must advance with the q-current envelope; it is not the board's physical rating |
| Raw-current trip | ±600 counts, ±3.635 A nominal | Provisional protection threshold, more than 20% above maximum request | `phase_current_loop` in the 20 kHz ADC completion path | Immediate shutdown is tested; protection-grade tolerance, amplifier clipping, and temperature remain open |
| Phase-voltage command | 700 permille of bus | Timing constraint: all switching edges end by 70% of the carrier | `phase_current_loop` | Leaves at least 5 us before the 80%-carrier current sample; performance may expand only with a new measured sampling strategy |
| Active-leg duty | 800 permille maximum | Topology/timing constraint from the retained 200-permille duty margin | `phase_current_loop` and TIM3 backend | Independent of the 700-permille phase-voltage clamp; retain until switching/bootstrap measurements justify change |
| Current-reference slew | 10,000 counts/s, about 60.59 A/s | Evaluation envelope; reaches the 495-count maximum in 49.5 ms, about 7.6 times the measured 6.5 ms current-loop rise | `aligned_torque_controller` | Intentionally permits transient evaluation; use measured tracking/overshoot to select the next value |
| Mechanical velocity during open torque | 5 rev/s, 300 RPM | Evaluation envelope, deliberately beyond the present 1 kHz measured-phase refresh quality boundary | `aligned_torque_controller` | At the endpoint, measured phase updates only four times per electrical cycle. Measure the resulting torque ripple/tracking failure, then justify fast phase prediction from that evidence |
| Mechanical acceleration during open torque | 1,000 rev/s² observed | Evaluation ceiling chosen above expected filtered-estimator transients, not a motor command or performance rating | `aligned_torque_controller` | Prevents the initial 20 rev/s² candidate from masking current/speed boundaries; must become motor/application configuration with the closed velocity loop |
| Accepted feedback interval | 2,000 us maximum | Timing policy for the nominal 1 kHz encoder schedule, permitting one late interval | `aligned_torque_controller` | Active hardware observations were about 981-1,001 us; remeasure under outer-loop load |
| Torque duration | 3 through 2,147,483,647 ms | Timing-derived: 3 ms allows a pre-deadline update at the 2 ms feedback limit; the maximum is the signed modulo-32-bit half-range | Command service and `aligned_torque_controller` | Caller always supplies a finite deadline; duration is not a thermal/current proxy or communications lease |
| Velocity target | ±4 rev/s, ±240 RPM | Evaluation envelope below the independent 5 rev/s observed-speed shutdown threshold; the validated point remains 1 rev/s | `velocity_controller` before command acceptance and on every update | Stage both signs through 2, 3, and 4 rev/s while measuring current tracking, phase-refresh quality, voltage effort, and faults |
| Velocity-reference acceleration | 4 rev/s² | Evaluation trajectory limit, independent of the actuator's observed-acceleration trip | `velocity_controller` | Verify reference progression, reversal, saturation recovery, and loaded behavior at each expanded speed point |
| Velocity-command current | 1-100 counts, up to about 606 mA nominal per request | Initial evaluation envelope below the already demonstrated 757 mA current point | `velocity_controller`, then independently slewed and bounded by `aligned_torque_controller` | Validate current saturation and anti-windup before increasing toward the actuator ceiling |
| Velocity feedback speed | 5 rev/s, 300 RPM maximum observed | Independent runaway/feedback plausibility ceiling; deliberately larger than the initial target range | `velocity_controller` and `aligned_torque_controller` | This is a shutdown threshold, not a commandable speed rating |
| Velocity feedback interval | 2,000 us maximum | Same timing policy as the 1 kHz aligned actuator | `velocity_controller` and `aligned_torque_controller` | Remeasure PendSV execution and encoder interval under active PI load |
| Velocity duration | 3 through 2,147,483,647 ms | Finite wrap-safe deadline contract | Command service, `velocity_controller`, and aligned actuator | The first bench run uses seconds, not the representational maximum |
| Velocity PI gains | Kp 100 counts/(rev/s), Ki 200 counts/rev | Low-gain implementation candidate with symmetric anti-windup at the per-command current limit | `velocity_controller` / `pi_controller` | Tune from signed step response; not yet a motor-independent product default |
| Relative position travel | ±100 revolutions per command | Evaluation envelope for a continuous rotary axis; separate from the Q16.16 absolute-status representation | Command service and `position_controller` | Start with ±0.25 revolution and expand only after signed settling and STOP pass |
| Position trajectory velocity | 0-4 rev/s, 0-240 RPM magnitude | Per-command trajectory bound no greater than the velocity-controller ceiling | `position_controller`, then independently by `velocity_controller` | Qualify position first below 1 rev/s, then reuse the staged velocity evidence |
| Position trajectory acceleration | 0-4 rev/s² | Per-command trapezoidal-profile bound independent of current and following-error policy | `motion_profile` / `position_controller`, then velocity-reference slew | Verify monotonic bounded reference motion and clean deceleration in both directions |
| Position-command current | 1-100 counts, up to about 606 mA nominal | Same caller-selected actuator envelope as velocity, independently retained per move | `position_controller`, `velocity_controller`, and aligned actuator | The 100-count point already passed the velocity Right-button gate; verify position saturation cannot defeat following-error shutdown |
| Position start velocity | 0.1 rev/s maximum magnitude | Entry-condition policy preventing capture of an already-moving shaft into a relative trajectory | Foreground preflight and `position_controller` at actual start | Rejected commands must not energize the bridge |
| Position following error | 0.25 revolution maximum magnitude | Independent trajectory-to-measurement shutdown threshold | `position_controller` | Exercise with a repeatable guarded load fixture; manual loading is qualitative evidence only |
| Position completion | 0.002 revolution, 0.02 rev/s, 50 consecutive 1 kHz samples | Evaluation settling policy requiring position and speed agreement for about 50 ms | `position_controller` | Validate repeatability and tune from captured endpoint error, not feel |
| Position feedback interval | 2,000 us maximum | Same deterministic 1 kHz feedback-age contract as velocity | `position_controller`, velocity controller, and aligned actuator | Any violation faults and converges on `ZERO` |
| Position duration | 100 through 2,147,483,647 ms | Finite wrap-safe deadline; expiration releases normally but reports `deadline`, not `settled` | Command service and `position_controller` | Caller must choose a duration long enough for the requested profile and settling time |
| Rotating-current diagnostic frequency | 0.001 through 250 electrical Hz | Evaluation envelope matching 5 rev/s on the 50-cycle/rev motor | Product diagnostic command path | The 1 kHz reference schedule provides only four points/cycle at 250 Hz; this is useful boundary evidence, not a quality guarantee |
| Rotating-current diagnostic duration | 3 through 2,147,483,647 ms | Same wrap-safe finite-deadline basis as aligned torque | Product diagnostic command path | Replaces the inherited 100-60,000 ms commissioning window |

## Performance progression

The next envelopes are product-development gates, not permanent ceilings:

1. Validate 1.503 A and 2.25 A through the existing supervisor, aligned
   q-current, 20 kHz current loop, telemetry, STOP, and common fault path before
   evaluating the attached motor's 2.999 A nominal rating.
2. Evaluate toward 5 rev/s (300 RPM) with the present 1 kHz phase refresh and
   record the point at which phase quantization, current tracking, voltage use,
   or torque ripple becomes unacceptable. Then move phase-reference prediction
   into the deterministic 20 kHz current path and correct it from timestamped
   encoder observations; at 5 rev/s this provides 80 fast-loop updates per
   electrical cycle instead of four held references.
3. Stage the expanded velocity service through ±2, ±3, and ±4 rev/s at bounded
   current, then make motor current, velocity, acceleration, and PI gains persistent
   application configuration. The firmware hard ceiling and selected
   application value must remain separate and both must be reported.
4. Qualify the motor's reported 3 A rating only after current-sense
   clipping/bandwidth, switching waveforms, supply behavior, shunt and MOSFET
   temperature, and current-loop transients pass staged measurements. Firmware
   permission to evaluate the point is not qualification.
5. Bench-qualify relative position in both signs through settle, deadline,
   generic STOP, Right-button stop, and following-error behavior. Travel,
   velocity, acceleration, current, start speed, feedback age, and following
   error remain separate limits.

The board uses eight discrete MOSFETs and a heatsink/gap-pad assembly, which is
consistent with substantial power-stage ambition. A MOSFET headline current
rating does not by itself establish board continuous current: shunts, copper,
connectors, gate drive, switching loss, airflow, and the actual thermal path
remain part of the measured hard ceiling.

# Motor-Drive Operating Limits

Status: firmware 0.24.15 separates the bench-validated envelope from the larger
envelope firmware permits for deliberate evaluation. A validated point is
evidence, not automatically a request ceiling.

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

| Quantity | Firmware 0.24.15 value | Class and basis | Enforcement owner | Status / next evidence |
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
3. Close velocity control with configurable motor current, velocity, and
   acceleration values. The firmware hard ceiling and the selected application
   value must remain separate and both must be reported.
4. Qualify the motor's reported 3 A rating only after current-sense
   clipping/bandwidth, switching waveforms, supply behavior, shunt and MOSFET
   temperature, and current-loop transients pass staged measurements. Firmware
   permission to evaluate the point is not qualification.
5. Close position control with independent following-error and travel policy;
   neither may be inferred from a commissioning motion.

The board uses eight discrete MOSFETs and a heatsink/gap-pad assembly, which is
consistent with substantial power-stage ambition. A MOSFET headline current
rating does not by itself establish board continuous current: shunts, copper,
connectors, gate drive, switching loss, airflow, and the actual thermal path
remain part of the measured hard ceiling.

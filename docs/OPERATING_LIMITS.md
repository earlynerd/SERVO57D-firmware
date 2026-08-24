# Motor-Drive Operating Limits

Status: firmware 0.37.0 / protocol 1.18 is the current source candidate;
firmware 0.36.1 / protocol 1.17 is the flashed baseline. The flashed image retains
the measured 55 us predictor lead, generation-3 alignment, physical VBUS, and
the complete bounded motion/fault envelope. Matched +8 rev/s trials at 24 V
reduced velocity RMS error from 0.797 to 0.621 to 0.460 rev/s while staging
current-loop Kp from 2 to 3 to 4 and retaining `Ki=1/64`; the Kp=4 burst used at
most 563/700 permille phase voltage and ended fault-free in `ZERO`. Firmware
0.31.0 makes those gains volatile-active motor/application configuration,
reports their compiled defaults and stored values, and permits persistence only
through the existing inactive dual-slot transaction. The 16 rev/s command,
20 rev/s observation, current, voltage, duty, prediction, deadline, and fault
bounds are unchanged. Firmware 0.32.2 additionally stages 8 MHz encoder SPI,
a 4 kHz rotor release, acquisition-window timestamps, time-equivalent estimator filtering and position
settling, `-O2` deferred control, Cortex-M4F hardware floating point,
trace-gated fast-loop timing, and compact/rate-limited rotor publication; those
timing and numerical changes remain unflashed and unqualified. Firmware 0.26.0
remains the bench-qualified relative-
position baseline. Physical feedback-loss injection is indefinitely deferred
on this assembly while its fault/ZERO contract remains host/native tested. A
validated point is evidence, not automatically a request ceiling.

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

Every enforced limit must protect hardware, preserve control or safety
integrity, or ensure that an operation is explicit and intentional. Its owner
and basis must be traceable here. A bound without one of those purposes must be
removed or placed above every legitimate behavior admitted by the surrounding
hardware, sensing, numerical, and scheduling contracts; conservative-looking
numbers are not justification by themselves.

Motor feasibility and drive permission are deliberately separate. Defaults
should be broadly achievable starting points, but firmware ceilings are based
on drive hardware, sensing, numerical representation, scheduling, or control
integrity—not the pull-out curve of the motor currently attached. A permitted
velocity or acceleration may therefore be physically unachievable for many
motors. Loss of synchronism does not by itself indicate danger to the drive;
electrical protections remain independent, while following-error shutdown may
still preserve position-command integrity.

## Current firmware inventory

| Quantity | Firmware 0.37.0 source value | Class and basis | Enforcement owner | Status / next evidence |
| --- | ---: | --- | --- | --- |
| Current scale | 6.059 mA/count nominal | Measured conversion on the tested board: 3.3 V ADC reference, 6.65 gain, 20 mΩ shunt | ADC conversion and host tools | Verified on one board; production tolerance and temperature remain open |
| Bus-voltage scale | 13.22 mV/count nominal | Tested-board 3.3 V ADC reference and fitted 15.4 kOhm/1 kOhm divider | Automatic-injected PA3 ADC acquisition and host conversion | Inactive 0.28.0 status reported 23.829 V at the 24 V supply setting; all 22 active samples held 23.776-23.815 V with advancing samples and no ADC/deadline fault |
| Aligned q-current request | ±495 counts, ±2.999 A nominal | Evaluation envelope matching the attached motor's reported 3 A rating | `aligned_torque_controller` | 757.4 mA is validated; 1.503 A, 2.25 A, and 2.999 A are already permitted measurement points |
| Phase-current backend request | ±495 counts per winding | Evaluation envelope shared by torque, alignment, and the retained production diagnostic | `phase_current_loop` through `current_loop_backend` | At +6 rev/s/12 V, requested-vector magnitude reached 495 counts but measured-vector magnitude peaked near 148 counts because phase voltage clipped; the request value is not delivered-current evidence or the board's physical rating |
| Phase-current PI gains | Default and flashed active Kp 4.0 permille/count, Ki 1/64 permille/count per 20 kHz step; configurable Kp 0-16 and Ki 0-4 | Motor/application tuning configuration; the range is software validation permission, not a claim that every value is stable | Foreground configuration service; immutable `current_loop_backend` copy while active | Kp 2/3/4 matched +8 rev/s trials were fault-free and improved absolute current and velocity error through Kp=4. The expanded Ki range is unqualified search space; run fixed-current/frequency sweeps, step response, signed motion, and persistence gates before accepting a profile for this motor/supply |
| Raw-current trip | ±600 counts, ±3.635 A nominal | Provisional protection threshold, more than 20% above maximum request | `phase_current_loop` in the 20 kHz ADC completion path | Immediate shutdown is tested; protection-grade tolerance, amplifier clipping, and temperature remain open |
| Phase-voltage command | 70% of measured bus; nominally 8.4 V at 12 V or 16.8 V at 24 V | Timing constraint: all switching edges end by 70% of the carrier | `phase_current_loop` | At +6 rev/s/12 V, one phase reached this clamp in 27/62 active host samples. The 24 V repeat removed that clamp and reduced velocity RMS error; +12 rev/s/24 V reaches it again. Expand only with a newly measured sampling strategy |
| Active-leg duty | 80% maximum | Topology/timing constraint from the retained 20% duty margin | `phase_current_loop` and TIM3 backend | Independent of the 70%-of-bus phase-voltage clamp; retain until switching/bootstrap measurements justify change |
| Current-reference slew | 10,000 counts/s, about 60.59 A/s | Evaluation envelope; reaches the 495-count maximum in 49.5 ms, about 7.6 times the measured 6.5 ms current-loop rise | `aligned_torque_controller` | Intentionally permits transient evaluation; use measured tracking/overshoot to select the next value |
| Aligned-actuator observed velocity | 20 rev/s, 1,200 RPM | Independent motion shutdown aligned with the current estimator plausibility boundary, not a command target | `aligned_torque_controller` | Leaves transient room above the 16 rev/s command range; raise with the estimator work required for 3,000+ RPM |
| Encoder SPI clock | 8 MHz target | Implementation timing candidate using an exact divisor of the 32 MHz SPI1 peripheral clock; four-byte mode-3 DMA frame and 2 us CS setup/hold guards are retained | `spi1` | Flashed 500 kHz operation is proven; validate 8 MHz signal integrity, parity/status behavior, and error counters on hardware |
| Rotor/control release | 4 kHz, 250 us nominal | Implementation timing candidate with 16,000 core cycles between releases at 64 MHz | TIM6/TIM7, SPI1 DMA, PendSV, and `rotor_control_runtime` | An informal 4 kHz run is encouraging; measure maximum accepted interval, PendSV WCET, higher-priority current-loop preemption, and stack high-water use |
| Rotor publication | 56-byte progress at 4 kHz; 576-byte full state at 100 Hz plus transitions | Scheduling contract separating liveness/readiness fields from telemetry-sized controller state | `rotor_control_runtime` sequence-protected publications and foreground | Arm builds/static assertion confirm sizes; measure PendSV WCET and foreground load on hardware |
| Foreground safety housekeeping | 1 kHz nominal | Implementation cadence for compact progress, liveness/readiness, runtime events, state invariants, RS-485 health, diagnostic deadline, and watchdog policy | Foreground main loop | Wrap-safe scheduling is build-reviewed; threshold crossings are observed by the next poll, so bench-check liveness and STOP reaction latency |
| Encoder observation timestamp | CS assertion at the start of the coherent four-byte window | Acquisition-window timing contract replacing post-DMA/post-hold publication time | `spi1` | Correlate CS/SCK and reported prediction age; the exact sensor-internal register-latch instant remains to be established |
| Mechanical acceleration during open torque | 8,192 rev/s² observed | Estimator-plausibility boundary above the approximately 5,350 rev/s² largest nominal-cadence velocity change the 4 kHz filtered estimator can publish while accepting raw motion at its 20 rev/s boundary; not a hardware protection threshold | `aligned_torque_controller` | Current, voltage, duty, speed, feedback-age, and faults independently protect the drive; revisit this limit with the estimator cadence/filter or a qualified motor/load inertia model |
| Accepted feedback interval | 2,000 us maximum | Timing policy for the nominal 4 kHz encoder schedule, allowing eight nominal release periods before rejection | `aligned_torque_controller` | Active 1 kHz hardware observations were about 981-1,001 us; remeasure under the 4 kHz outer-loop load |
| Fast electrical-phase prediction | Every 50 us; 3,000 us maximum observation age; 55 us measured output lead; observations through 20 rev/s | Timing contract: predict from regular-current DMA completion near 45 us to the 100 us PWM application boundary; the priority-1 guardian permits the intentional intervening update but faults a second update without new output | `electrical_phase_predictor` through `current_loop_backend` | The first firmware 0.30.0 +8 rev/s burst measured a 41.094 us trigger, 3.938 us trigger-to-DMA interval, 20.578-21.141 us DMA-to-stage path, and 33.031-33.594 us remaining to the 100 us application boundary. Firmware 0.30.1 corrects the old 7 us estimate; repeat +8 rev/s after flash before +12 rev/s |
| Current timing trace | 256 samples only after explicit active arm; timing reads dormant otherwise | Optional measurement contract; normal control avoids TIM2/DWT/preload timing overhead while retaining all current, output, PWM, and deadline checks | ADC/current backend | Re-arm, fill, stop, fault, recovery, and in-flight arm/disarm behavior are build-reviewed; remeasure the optimized disarmed ISR and explicitly armed trace on hardware |
| Encoder production progress | 3,000 us stale threshold without a newly observed accepted sample; evaluated at 1 kHz foreground cadence | Independent total-silence guard retained across the rate change; twelve nominal 250 us releases to the threshold, then observation by the next foreground poll | `encoder_liveness` and the foreground supervisor prerequisite | Host-tested across stale state, recovery, sample-counter wrap, and microsecond-timer wrap; revalidate threshold-to-`ZERO` latency under the 4 kHz candidate before tightening it |
| Estimator velocity filter | `alpha = 0.03283179` per accepted 4 kHz sample | Implementation configuration preserving the prior 1 kHz `alpha=0.125` filter pole over elapsed time | `angle_tracker` | Measure stationary/low-speed quantization noise and dynamic response on the 4 kHz candidate |
| Estimator motion plausibility | 20 rev/s, 1,200 RPM maximum | Implementation threshold for rejecting implausible sample-to-sample motion | `angle_tracker` | Defines the present architecture boundary and leaves 20% room above the 16 rev/s command range; raising it toward at least 50 rev/s is active speed work |
| Torque duration | 3 through 2,147,483,647 ms | Timing-derived: 3 ms allows a pre-deadline update at the 2 ms feedback limit; the maximum is the signed modulo-32-bit half-range | Command service and `aligned_torque_controller` | Caller always supplies a finite deadline; duration is not a thermal/current proxy or communications lease |
| Velocity target | ±16 rev/s, ±960 RPM | Evaluation permission using 80% of the current 20 rev/s estimator boundary | Command service and `velocity_controller` | At 24 V, positive +8 rev/s reaches/passes target without q-current clipping; +12 rev/s saturates q-demand and phase voltage and plateaus near 10 rev/s. Improve current tracking and run the negative sign before treating 12 or 16 rev/s as achieved performance |
| Direct velocity-reference acceleration | Caller-selected positive value through 256 rev/s²; host and legacy default 16 rev/s² | Explicit trajectory shape; 256 retains fourfold headroom over the maximum position-profile acceleration and is controller capability, not a motor claim | Command service and `velocity_controller` | The attached motor stayed synchronized at 16 rev/s² and skipped steps at 32 rev/s² in position trials; use 16 for the next direct-velocity validation while current saturation and plant inertia still determine achieved acceleration |
| Velocity-command current | 6.059 mA-2.999 A nominal per request (raw 1-495 counts) | Evaluation permission matching the attached motor's reported rating and existing aligned actuator | `velocity_controller`, then independently slewed and bounded by `aligned_torque_controller` | The +6 rev/s/12 V test exercised the requested/applied q-current ceiling without a fault, but measured-vector magnitude peaked near 0.90 A because voltage clipped. This is command-path evidence, not delivered-current or thermal qualification |
| Velocity feedback speed | 20 rev/s, 1,200 RPM maximum observed | Independent runaway/feedback plausibility ceiling shared with the estimator and aligned actuator | `velocity_controller` and `aligned_torque_controller` | This is a shutdown threshold, not a commandable speed rating |
| Velocity feedback interval | 2,000 us maximum | Same timing policy as the 4 kHz aligned actuator | `velocity_controller` and `aligned_torque_controller` | The 1 kHz baseline captured 998-1,002 us under the +5/+6 rev/s PI load; remeasure at 4 kHz |
| Velocity duration | 3 through 2,147,483,647 ms | Finite wrap-safe deadline contract | Command service, `velocity_controller`, and aligned actuator | The first bench run uses seconds, not the representational maximum |
| Velocity PI gains | Kp 100 counts/(rev/s), Ki 200 counts/rev | Low-gain implementation candidate with symmetric anti-windup at the per-command current limit | `velocity_controller` / `pi_controller` | The 24 V +6/+8 evidence separates the earlier 12 V voltage clamp from velocity gain behavior. Improve the underlying high-frequency current tracking and then tune both signed velocity responses. These gains are not a motor-independent product default |
| Relative position travel | ±100 revolutions per command | Evaluation envelope for a continuous rotary axis; separate from the Q16.16 absolute-status representation | Command service and `position_controller` | The caller chooses travel appropriate to the fixture; ±0.25 revolution is the accepted comparison point, not an unlock condition |
| Position trajectory velocity | 0-16 rev/s, 0-960 RPM magnitude | Per-command profile bound matching direct velocity permission | `position_controller`, then independently by `velocity_controller` | The caller may explore the exposed range while retaining separate current, acceleration, following-error, and deadline bounds |
| Position correction velocity | ±17 rev/s inner target | Exact headroom for the 16 rev/s profile plus `Kp 4/s × 0.25 rev` maximum following error | `position_controller` and dynamic `velocity_controller` target | Prevents the position correction from being clipped at profile speed; host regression requires a target above the profile ceiling |
| Position trajectory acceleration | 0-64 rev/s² | Per-command trapezoidal-profile permission with the inner velocity slew fixed at four times this value | `motion_profile` / `position_controller`, then velocity-reference slew | Higher requested profile acceleration makes tracking harder; use it to shape the test, not to mask insufficient torque |
| Position-command current | 6.059 mA-2.999 A nominal (raw 1-495 counts) | Same caller-selected actuator envelope as velocity, independently retained per move | `position_controller`, `velocity_controller`, and aligned actuator | The caller chooses current from the live policy; saturation remains telemetry and cannot defeat following-error shutdown |
| Position start velocity | 0.1 rev/s maximum magnitude | Entry-condition policy preventing capture of an already-moving shaft into a relative trajectory | Foreground preflight and `position_controller` at actual start | Rejected commands must not energize the bridge |
| Position following error | 0.25 revolution maximum magnitude | Independent loss-of-tracking shutdown, not a trajectory or electronics capability ceiling | `position_controller` | Retained because it detects an actual profile/rotor divergence; cascade rate and velocity headroom no longer consume its correction authority by construction |
| Position completion | 0.002 revolution, 0.02 rev/s, 200 consecutive 4 kHz samples | Evaluation settling policy preserving about 50 ms of position and speed agreement | `position_controller` | Six 1 kHz baseline moves reached finite deadline with repeatable approximately ±0.0025-revolution endpoint offset; revalidate at 4 kHz |
| Position feedback interval | 2,000 us maximum | Same deterministic 4 kHz feedback-age contract as velocity | `position_controller`, velocity controller, and aligned actuator | Any violation faults and converges on `ZERO` |
| Position duration | 100 through 2,147,483,647 ms | Finite wrap-safe deadline; expiration releases normally but reports `deadline`, not `settled` | Command service and `position_controller` | Caller must choose a duration long enough for the requested profile and settling time |
| Rotating-current diagnostic frequency | 0.001 through 1,000 electrical Hz | Implementation-based evaluation envelope retaining 20 current-loop updates and four 4 kHz encoder observations per cycle at the endpoint | Product diagnostic command path | Firmware 0.35.0 passes through 225 Hz at 303 mA and through 200 Hz at 606 mA and 1.503 A; the 1.503 A / 200 Hz trial used 697/700 voltage permille, so the widened source range is permission to characterize other motors rather than qualification of this motor |
| Rotating-current diagnostic ramp | Optional 0-to-target linear frequency ramp; production tuner default 50 electrical Hz/s | Test-shaping input rather than a qualified motor acceleration; the host converts rate to a per-frequency ramp duration | 20 kHz diagnostic generator through the product supervisor/current backend | Allows the rotor to accelerate before high-frequency hold measurements; current amplitude is applied immediately at the initial phase, and zero ramp retains the legacy step |
| Current-diagnostic controller | Stationary A/B PI by default; selectable fixed-point rotating d/q PI | Bench-proven comparison and motor-tuning mechanism | 20 kHz current backend under the same supervisor authority and electrical limits | At 303 mA and Kp=9/Ki=0.5, rotating mode reduced 200 Hz lag from 39.09 to -0.01 degrees and RMS error from 149.4 to 7.9 mA; retain it for same-condition comparison and tuning |
| Product aligned-q current controller | Fixed-point rotating d/q PI with `d=0` and signed q demand | Source-candidate product controller; static vectors and alignment remain stationary A/B PI | 20 kHz current backend using the encoder phase predictor's sample and 55 us PWM-application horizons | Host/build validation is complete; require bounded signed torque, velocity, position, timing, STOP/release, fault, and thermal bench evidence before expanding the flashed motion envelope |
| Current-loop missed PWM updates | Fault on the second consecutive PWM boundary without a new staged output; count every isolated/consecutive event | Safety timing contract plus diagnostic evidence | Priority-1 TIM3 guardian; schema-5 commissioning status | The guardian is unchanged; require both total and maximum consecutive counts to remain zero in each rotating-frame trial |
| Rotating-current diagnostic duration | Hold 3 through 2,147,483,647 ms; ramp plus hold at most 2,147,483,647 ms | Same wrap-safe finite-deadline basis as aligned torque | Product diagnostic command path | One independent deadline covers both intervals; STOP and faults remain effective during the ramp |

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
velocity, and position at 4 kHz. The
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
   the corrected output lead and worst-case ISR execution, and test the negative sign
   before using +16 rev/s as performance evidence. At 16 rev/s the predictor provides 25 fast-loop updates per
   electrical cycle; current tracking, voltage use, filtered-velocity lag,
   mechanics, and torque ripple identify the next concrete change.
3. Validate the 16 rev/s² direct-velocity default in both signs before staging
   higher acceleration. The attached motor physically skipped steps at a
   32 rev/s² position profile despite clean controller telemetry, so physical
   synchronism—not telemetry alone—owns the acceleration qualification. Then make motor current, velocity, acceleration, and
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

package BareMetalCtrl {
    import ScalarValues::*;

    item def EvArm;
    item def EvSample;
    item def EvReset;

    calc def compute_duty {
        in temp : Integer;
        return : Integer;
    }

    state def WatchdogController {
        in temp_c : Integer;
        out pwm_duty : Integer;
        attribute sample_count : Integer := 0;

        entry; then Standby;

        state Standby;
        state Active;
        state SafeShutdown;

        transition t_arm
            first Standby
            accept EvArm
            if temp_c < 90
            then Active;

        transition t_sample
            first Active
            accept EvSample
            do action RunControl {
                assign pwm_duty := compute_duty(temp_c);
                assign sample_count := sample_count + 1;
            }
            then Active;

        transition t_watchdog
            first Active
            accept after 100 [ms]
            do action ZeroOutput {
                assign pwm_duty := 0;
            }
            then SafeShutdown;

        transition t_reset
            first SafeShutdown
            accept EvReset
            then Standby;
    }
}

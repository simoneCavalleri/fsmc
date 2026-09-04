package Avionics {
    state def FlightManagementSystem {
        satisfy requirement REQ_FMS_NAV_001;
        satisfy requirement REQ_FMS_NAV_002;
        satisfy requirement REQ_FMS_SFT_001;

        enum def FlightPhase {
            enum Preflight = 0;
            enum Taxi = 1;
            enum Takeoff = 2;
            enum Climb = 3;
            enum Cruise = 4;
            enum Descent = 5;
            enum Approach = 6;
            enum GoAround = 7;
            enum Landed = 8;
        }

        struct def Waypoint {
            attribute waypointId : uint32 = 1;
            attribute targetAltitude_ft : Real [ft] = 10000.0;
            attribute groundSpeed_kts : Real [kts] = 250.0;
            attribute isFlyover : Boolean = false;
        }

        in port cross_track_error_m : Real { assert constraint { self >= -500.0 and self <= 500.0; } }
        in port vertical_deviation_ft : Real { assert constraint { self >= -2000.0 and self <= 2000.0; } }
        in port baro_alt_ft : Real;

        out port autothrottle_engaged : Boolean;
        out port flight_director_active : Boolean;

        attribute waypoints_sequenced : Integer = 0;
        attribute nav_cycles : Integer = 0;

        event def StartTaxi;
        event def TakeoffClearance;
        event def RotateSpeedReached;
        event def CruiseAltReached;
        event def TopOfDescent;
        event def LocalizerCaptured;
        event def Touchdown;
        event def MissedApproachCmd;

        entry; then GroundOps;

        state GroundOps {
            satisfy requirement REQ_FMS_GND_001;
            entry; then PreflightState;

            state PreflightState;
            state TaxiState;

            transition t_taxi
                first PreflightState
                accept StartTaxi
                do action { out.flight_director_active = true; }
                then TaxiState;

            transition t_takeoff
                first TaxiState
                accept TakeoffClearance
                do action { out.autothrottle_engaged = true; }
                then Airborne;
        }

        state Airborne {
            satisfy requirement REQ_FMS_AIR_001;
            entry; then ClimbState;

            state ClimbState;
            state CruiseState;
            state DescentState;
            state ApproachState;

            transition t_climb_cruise
                first ClimbState
                accept CruiseAltReached
                do action { reg.waypoints_sequenced = reg.waypoints_sequenced + 1; }
                then CruiseState;

            transition t_tod
                first CruiseState
                accept TopOfDescent
                then DescentState;

            transition t_app
                first DescentState
                accept LocalizerCaptured
                then ApproachState;

            transition t_go_around
                first ApproachState
                accept MissedApproachCmd
                do action { out.autothrottle_engaged = true; }
                then ClimbState;

            transition t_land
                first ApproachState
                accept Touchdown
                do action { out.autothrottle_engaged = false; out.flight_director_active = false; }
                then Landed;
        }

        state Landed {
            satisfy requirement REQ_FMS_LND_001;
        }
    }
}

/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009

	M Roberts (original release)
	Robin Birch <robinb@ruffnready.co.uk>
	Samuel Gisiger <samuel.gisiger@triadis.ch>
	Jeff Goodenough <jeff@enborne.f2s.com>
	Alastair Harrison <aharrison@magic.force9.co.uk>
	Scott Penrose <scottp@dd.com.au>
	John Wharington <jwharington@gmail.com>
	Lars H <lars_hn@hotmail.com>
	Rob Dunning <rob@raspberryridgesheepfarm.com>
	Russell King <rmk@arm.linux.org.uk>
	Paolo Ventafridda <coolwind@email.it>
	Tobias Lohner <tobias@lohner-net.de>
	Mirek Jezek <mjezek@ipplc.cz>
	Max Kellermann <max@duempel.org>
	Tobias Bieniek <tobias.bieniek@gmx.de>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "DeviceBlackboard.hpp"
#include "Protection.hpp"
#include "Math/Earth.hpp"
#include "UtilsSystem.hpp"
#include "Math/Geometry.hpp"
#include "TeamCodeCalculation.h"
#include "UtilsFLARM.hpp"
#include "Asset.hpp"
#include "Device/Parser.hpp"
#include "Device/List.hpp"
#include "Device/Descriptor.hpp"
#include "Device/All.hpp"
#include "Math/Constants.h"
#include "GlideSolvers/GlidePolar.hpp"
#include "Simulator.hpp"

DeviceBlackboard device_blackboard;

/**
 * Initializes the DeviceBlackboard
 */
void
DeviceBlackboard::Initialise()
{
  ScopeLock protect(mutexBlackboard);

  // Clear the gps_info and calculated_info
  // JMW OLD_TASK TODO: make a reset() method that does this

#ifdef OLD_TASK
  memset( &gps_info, 0, sizeof(NMEA_INFO));
  memset( &calculated_info, 0, sizeof(DERIVED_INFO));
#endif

  // Set the NAVWarning positive (assume not gps found yet)
  gps_info.gps.NAVWarning = true;

  gps_info.gps.Simulator = false;

  // Clear the SwitchStates
  gps_info.SwitchState.Available = false;
  gps_info.SwitchState.AirbrakeLocked = false;
  gps_info.SwitchState.FlapPositive = false;
  gps_info.SwitchState.FlapNeutral = false;
  gps_info.SwitchState.FlapNegative = false;
  gps_info.SwitchState.GearExtended = false;
  gps_info.SwitchState.Acknowledge = false;
  gps_info.SwitchState.Repeat = false;
  gps_info.SwitchState.SpeedCommand = false;
  gps_info.SwitchState.UserSwitchUp = false;
  gps_info.SwitchState.UserSwitchMiddle = false;
  gps_info.SwitchState.UserSwitchDown = false;
  gps_info.SwitchState.VarioCircling = false;

  // Set GPS assumed time to system time
  SYSTEMTIME pda_time;
  GetSystemTime(&pda_time);
  gps_info.aircraft.Time = pda_time.wHour * 3600 +
                  pda_time.wMinute * 60 +
                  pda_time.wSecond;
  gps_info.DateTime.year = pda_time.wYear;
  gps_info.DateTime.month = pda_time.wMonth;
  gps_info.DateTime.day = pda_time.wDay;
  gps_info.DateTime.hour = pda_time.wHour;
  gps_info.DateTime.minute = pda_time.wMinute;
  gps_info.DateTime.second = pda_time.wSecond;

  if (is_simulator()) {
    #ifdef _SIM_STARTUPSPEED
      gps_info.Speed = _SIM_STARTUPSPEED;
    #endif
    #ifdef _SIM_STARTUPALTITUDE
      gps_info.GPSAltitude = _SIM_STARTUPALTITUDE;
    #endif
  }
}

/**
 * Sets the location and altitude to loc and alt
 *
 * Called at startup when no gps data available yet
 * @param loc New location
 * @param alt New altitude
 */
void
DeviceBlackboard::SetStartupLocation(const GEOPOINT &loc, const double alt)
{
  ScopeLock protect(mutexBlackboard);
  SetBasic().aircraft.Location = loc;
  SetBasic().GPSAltitude = alt;

  // enable the "Simulator" flag because this value was not provided
  // by a real GPS
  SetBasic().gps.Simulator = true;
}

/**
 * Sets the location, altitude and other basic parameters
 *
 * Used by the ReplayLogger
 * @param loc New location
 * @param speed New speed
 * @param bearing New bearing
 * @param alt New altitude
 * @param baroalt New barometric altitude
 * @param t New time
 * @see ReplayLogger::UpdateInternal()
 */
void
DeviceBlackboard::SetLocation(const GEOPOINT &loc,
			      const double speed, const double bearing,
			      const double alt, const double baroalt, const double t)
{
  ScopeLock protect(mutexBlackboard);
  SetBasic().acceleration.Available = false;
  SetBasic().aircraft.Location = loc;
  SetBasic().aircraft.Speed = speed;
  SetBasic().aircraft.IndicatedAirspeed = speed; // cheat
  SetBasic().aircraft.TrackBearing = bearing;
  SetBasic().AirspeedAvailable = false;
  SetBasic().GPSAltitude = alt;
  SetBasic().BaroAltitude = baroalt;
  SetBasic().aircraft.Time = t;
  SetBasic().VarioAvailable = false;
  SetBasic().NettoVarioAvailable = false;
  SetBasic().ExternalWindAvailable = false;
  SetBasic().gps.Replay = true;
};

/**
 * Stops the replay
 */
void DeviceBlackboard::StopReplay() {
  ScopeLock protect(mutexBlackboard);
  SetBasic().aircraft.Speed = 0;
  SetBasic().gps.Replay = false;
}

/**
 * Sets the NAVWarning to val
 * @param val New value for NAVWarning
 */
void
DeviceBlackboard::SetNAVWarning(bool val)
{
  ScopeLock protect(mutexBlackboard);
  GPS_STATE &gps = SetBasic().gps;
  gps.NAVWarning = val;
  if (!val) {
    // if NavWarning is false, since this is externally forced
    // by the simulator, we also set the number of satelites used
    // as a simulated value
    gps.SatellitesUsed = 6;
  }
}

/**
 * Lowers the connection status of the device
 *
 * Connected + Fix -> Connected + No Fix
 * Connected + No Fix -> Not connected
 * @return True if still connected afterwards, False otherwise
 */
bool
DeviceBlackboard::LowerConnection()
{
  ScopeLock protect(mutexBlackboard);
  GPS_STATE &gps = SetBasic().gps;

  if (gps.Connected)
    gps.Connected--;

  return gps.Connected > 0;
}

/**
 * Raises the connection status to connected + fix
 */
void
DeviceBlackboard::RaiseConnection()
{
  ScopeLock protect(mutexBlackboard);
  SetBasic().gps.Connected = 2;
}

void
DeviceBlackboard::ProcessSimulation()
{
  if (!is_simulator())
    return;

  ScopeLock protect(mutexBlackboard);

  SetBasic().gps.Simulator = true;

  SetNAVWarning(false);
  FindLatitudeLongitude(Basic().aircraft.Location,
                        Basic().aircraft.TrackBearing,
                        Basic().aircraft.Speed,
                        &SetBasic().aircraft.Location);
  SetBasic().aircraft.Time += fixed_one;
  long tsec = (long)Basic().aircraft.Time;
  SetBasic().DateTime.hour = tsec / 3600;
  SetBasic().DateTime.minute = (tsec - Basic().DateTime.hour * 3600) / 60;
  SetBasic().DateTime.second = tsec-Basic().DateTime.hour * 3600
    - Basic().DateTime.minute * 60;

  // use this to test FLARM parsing/display
  if (is_debug() && !is_altair())
    DeviceList[0].parser.TestRoutine(&SetBasic());
}

/**
 * Sets the GPS speed and indicated airspeed to val
 *
 * not in use
 * @param val New speed
 */
void
DeviceBlackboard::SetSpeed(fixed val)
{
  ScopeLock protect(mutexBlackboard);
  SetBasic().aircraft.Speed = val;
  SetBasic().aircraft.IndicatedAirspeed = val;
}

/**
 * Sets the TrackBearing to val
 *
 * not in use
 * @param val New TrackBearing
 */
void
DeviceBlackboard::SetTrackBearing(fixed val)
{
  ScopeLock protect(mutexBlackboard);
  SetBasic().aircraft.TrackBearing = AngleLimit360(val);
}

/**
 * Sets the altitude and barometric altitude to val
 *
 * not in use
 * @param val New altitude
 */
void
DeviceBlackboard::SetAltitude(fixed val)
{
  ScopeLock protect(mutexBlackboard);
  SetBasic().GPSAltitude = val;
  SetBasic().BaroAltitude = val;
}

/**
 * Reads the given derived_info usually provided by the
 * GlideComputerBlackboard and saves it to the own Blackboard
 * @param derived_info Calculated information usually provided
 * by the GlideComputerBlackboard
 */
void
DeviceBlackboard::ReadBlackboard(const DERIVED_INFO &derived_info)
{
  calculated_info = derived_info;
}

/**
 * Reads the given settings usually provided by the InterfaceBlackboard
 * and saves it to the own Blackboard
 * @param settings SettingsComputer usually provided by the
 * InterfaceBlackboard
 */
void
DeviceBlackboard::ReadSettingsComputer(const SETTINGS_COMPUTER
					      &settings)
{
  settings_computer = settings;
}

/**
 * Reads the given settings usually provided by the InterfaceBlackboard
 * and saves it to the own Blackboard
 * @param settings SettingsMap usually provided by the
 * InterfaceBlackboard
 */
void
DeviceBlackboard::ReadSettingsMap(const SETTINGS_MAP
				  &settings)
{
  settings_map = settings;
}

/**
 * Checks for timeout of the FLARM targets and
 * saves the status to Basic()
 */
void
DeviceBlackboard::FLARM_RefreshSlots() {
  FLARM_STATE &flarm_state = SetBasic().flarm;
  flarm_state.Refresh(Basic().aircraft.Time);
}

/**
 * Sets the system time to GPS time if not yet done and
 * defined in settings
 */
void
DeviceBlackboard::SetSystemTime() {
  // TODO JMW: this should be done outside the parser..
  if (is_simulator())
    return;

#ifdef HAVE_WIN32
  // Altair doesn't have a battery-backed up realtime clock,
  // so as soon as we get a fix for the first time, set the
  // system clock to the GPS time.
  static bool sysTimeInitialised = false;

  const GPS_STATE &gps = Basic().gps;

  if (!gps.NAVWarning && SettingsMap().SetSystemTimeFromGPS
      && !sysTimeInitialised) {
    SYSTEMTIME sysTime;
    ::GetSystemTime(&sysTime);

    sysTime.wYear = (unsigned short)Basic().DateTime.year;
    sysTime.wMonth = (unsigned short)Basic().DateTime.month;
    sysTime.wDay = (unsigned short)Basic().DateTime.day;
    sysTime.wHour = (unsigned short)Basic().DateTime.hour;
    sysTime.wMinute = (unsigned short)Basic().DateTime.minute;
    sysTime.wSecond = (unsigned short)Basic().DateTime.second;
    sysTime.wMilliseconds = 0;
    sysTimeInitialised = (::SetSystemTime(&sysTime)==true);

#if defined(GNAV) && !defined(WINDOWSPC)
    TIME_ZONE_INFORMATION tzi;
    tzi.Bias = -SettingsComputer().UTCOffset/60;
    _tcscpy(tzi.StandardName,TEXT("Altair"));
    tzi.StandardDate.wMonth= 0; // disable daylight savings
    tzi.StandardBias = 0;
    _tcscpy(tzi.DaylightName,TEXT("Altair"));
    tzi.DaylightDate.wMonth= 0; // disable daylight savings
    tzi.DaylightBias = 0;

    SetTimeZoneInformation(&tzi);
#endif
    sysTimeInitialised =true;
  }
#else
  // XXX
#endif
}

/**
 * Tries to find a name for every current FLARM_Traffic id
 */
void
DeviceBlackboard::FLARM_ScanTraffic()
{
  // TODO: this is a bit silly, it searches every time a target is
  // visible... going to be slow..
  // should only scan the first time it appears with that ID.
  // at least it is now not being done by the parser

  FLARM_STATE &flarm = SetBasic().flarm;

  // if (FLARM data is available)
  if (!flarm.FLARM_Available)
    return;

  // for each item in FLARM_Traffic
  for (unsigned i = 0; i < FLARM_STATE::FLARM_MAX_TRAFFIC; i++) {
    FLARM_TRAFFIC &traffic = flarm.FLARM_Traffic[i];

    // if (FLARM_Traffic[flarm_slot] has data)
    // and if (Target currently without name)
    if (traffic.defined() && !traffic.HasName()) {
      // need to lookup name for this target
      const TCHAR *fname = LookupFLARMDetails(traffic.ID);
      if (fname != NULL)
        _tcscpy(traffic.Name, fname);
    }
  }
}

void
DeviceBlackboard::tick(const GlidePolar& glide_polar)
{
  // check for timeout on FLARM objects
  FLARM_RefreshSlots();
  // lookup known traffic
  FLARM_ScanTraffic();
  // set system time if necessary
  SetSystemTime();

  // calculate fast data to complete aircraft state
  FlightState(glide_polar);
  Wind();
  Heading();
  NavAltitude();
  AutoQNH();

  tick_fast(glide_polar);

  if (Basic().aircraft.Time!= LastBasic().aircraft.Time) {

    if (Basic().aircraft.Time > LastBasic().aircraft.Time) {
      TurnRate();
      Dynamics();
    }

    state_last = Basic();
  }
}


void
DeviceBlackboard::tick_fast(const GlidePolar& glide_polar)
{
  EnergyHeight();
  WorkingBand();
  Vario();
  NettoVario(glide_polar);
}


void
DeviceBlackboard::NettoVario(const GlidePolar& glide_polar)
{
  SetBasic().GliderSinkRate = 
    - glide_polar.SinkRate(Basic().aircraft.IndicatedAirspeed,
                           Basic().aircraft.Gload);

  if (!Basic().NettoVarioAvailable)
    SetBasic().aircraft.NettoVario = Basic().aircraft.Vario
      - Basic().GliderSinkRate;
}



/**
 * 1. Determines which altitude to use (GPS/baro)
 * 2. Calculates height over ground
 */
void
DeviceBlackboard::NavAltitude()
{
  if (!SettingsComputer().EnableNavBaroAltitude
      || !Basic().BaroAltitudeAvailable) {
    SetBasic().aircraft.NavAltitude = Basic().GPSAltitude;
  } else {
    SetBasic().aircraft.NavAltitude = Basic().BaroAltitude;
  }
  SetBasic().aircraft.AltitudeAGL = Basic().aircraft.NavAltitude
    - Calculated().TerrainAlt;
}


/**
 * Calculates the heading, and the estimated true airspeed
 */
void
DeviceBlackboard::Heading()
{
  AIRCRAFT_STATE &aircraft = SetBasic().aircraft;

  if ((aircraft.Speed > 0) || (aircraft.WindSpeed > 0)) {
    fixed x0 = fastsine(aircraft.TrackBearing)
      * aircraft.Speed;
    fixed y0 = fastcosine(aircraft.TrackBearing)
      * aircraft.Speed;
    x0 += fastsine(aircraft.WindDirection)
      * aircraft.WindSpeed;
    y0 += fastcosine(aircraft.WindDirection)
      * aircraft.WindSpeed;

    if (!aircraft.Flying) {
      // don't take wind into account when on ground
      SetBasic().Heading = aircraft.TrackBearing;
    } else {
      SetBasic().Heading = AngleLimit360(atan2(x0, y0) * RAD_TO_DEG);
    }

    // calculate estimated true airspeed
    SetBasic().TrueAirspeedEstimated = hypot(x0, y0);

  } else {
    SetBasic().Heading = aircraft.TrackBearing;
    SetBasic().TrueAirspeedEstimated = 0.0;
  }

  if (!Basic().AirspeedAvailable) {
    aircraft.TrueAirspeed = Basic().TrueAirspeedEstimated;
    aircraft.IndicatedAirspeed = aircraft.TrueAirspeed
      /Basic().pressure.AirDensityRatio(Basic().GetAltitudeBaroPreferred());
  }
}

/**
 * 1. Calculates the vario values for gps vario, gps total energy vario and distance vario
 * 2. Sets Vario to GPSVario or received Vario data from instrument
 */
void
DeviceBlackboard::Vario()
{
  // Calculate time passed since last calculation
  const fixed dT = Basic().aircraft.Time - LastBasic().aircraft.Time;

  if (positive(dT)) {
    const fixed Gain = Basic().aircraft.NavAltitude
      - LastBasic().aircraft.NavAltitude;
    const fixed GainTE = Basic().TEAltitude - LastBasic().TEAltitude;

    // estimate value from GPS
    SetBasic().GPSVario = Gain / dT;
    SetBasic().GPSVarioTE = GainTE / dT;
  }

  if (!Basic().VarioAvailable)
    SetBasic().aircraft.Vario = Basic().GPSVario;
}


void
DeviceBlackboard::Wind()
{
  if (!Basic().ExternalWindAvailable) {
    SetBasic().aircraft.WindSpeed = Calculated().WindSpeed_estimated;
    SetBasic().aircraft.WindDirection = Calculated().WindBearing_estimated;
  }
}

/**
 * Calculates the turn rate and the derived features.
 * Determines the current flight mode (cruise/circling).
 */
void
DeviceBlackboard::TurnRate()
{
  // Calculate time passed since last calculation
  const fixed dT = Basic().aircraft.Time - LastBasic().aircraft.Time;

  // Calculate turn rate

  if (!Basic().aircraft.Flying) {
    SetBasic().TurnRate = fixed_zero;
    SetBasic().NextTrackBearing = Basic().aircraft.TrackBearing;
    return;
  }

  SetBasic().TurnRate =
    AngleLimit180(Basic().aircraft.TrackBearing
                  - LastBasic().aircraft.TrackBearing) / dT;

  // if (time passed is less then 2 seconds) time step okay
  if (dT < fixed_two) {
    // calculate turn rate acceleration (turn rate derived)
    fixed dRate = (Basic().TurnRate - LastBasic().TurnRate) / dT;

    // integrate assuming constant acceleration, for one second
    // QUESTION TB: shouldn't dtlead be = 1, for one second?!
    static const fixed dtlead(0.3);

    const fixed calc_bearing = Basic().aircraft.TrackBearing + dtlead
      * (Basic().TurnRate + fixed_half * dtlead * dRate);

    // b_new = b_old + Rate * t + 0.5 * dRate * t * t

    // Limit the projected bearing to 360 degrees
    SetBasic().NextTrackBearing = AngleLimit360(calc_bearing);

  } else {

    // Time step too big, so just take the last measurement
    SetBasic().NextTrackBearing = Basic().aircraft.TrackBearing;
  }
}

/**
 * Calculates the turn rate of the heading,
 * the estimated bank angle and
 * the estimated pitch angle
 */
void
DeviceBlackboard::Dynamics()
{
  static const fixed fixed_inv_g(1.0/9.81);
  static const fixed fixed_small(0.001);

  if (Basic().aircraft.Flying &&
      (positive(Basic().aircraft.Speed) ||
       positive(Basic().aircraft.WindSpeed))) {

    // calculate turn rate in wind coordinates
    const fixed dT = Basic().aircraft.Time - LastBasic().aircraft.Time;

    if (positive(dT)) {
      SetBasic().TurnRateWind =
        AngleLimit180(Basic().Heading - LastBasic().Heading) / dT;
    }

    // estimate bank angle (assuming balanced turn)
    const fixed angle = atan(fixed_deg_to_rad * Basic().TurnRateWind
        * Basic().aircraft.TrueAirspeed * fixed_inv_g);

    SetBasic().acceleration.BankAngle = fixed_rad_to_deg * angle;

    if (!Basic().acceleration.Available)
      SetBasic().aircraft.Gload = fixed_one
        / max(fixed_small, fabs(cos(angle)));

    // estimate pitch angle (assuming balanced turn)
    SetBasic().acceleration.PitchAngle = fixed_rad_to_deg *
      atan2(Basic().GPSVario - Basic().aircraft.Vario,
            Basic().aircraft.TrueAirspeed);

  } else {
    SetBasic().acceleration.BankAngle = fixed_zero;
    SetBasic().acceleration.PitchAngle = fixed_zero;
    SetBasic().TurnRateWind = fixed_zero;

    if (!Basic().acceleration.Available)
      SetBasic().aircraft.Gload = fixed_one;
  }
}


/**
 * Calculates energy height on TAS basis
 *
 * \f${m/2} \times v^2 = m \times g \times h\f$ therefore \f$h = {v^2}/{2 \times g}\f$
 */
void
DeviceBlackboard::EnergyHeight()
{
  const fixed ias_to_tas =
    Basic().pressure.AirDensityRatio(Basic().GetAltitudeBaroPreferred());
  static const fixed fixed_inv_2g (1.0/(2.0*9.81));

  const fixed V_target = Calculated().common_stats.V_block * ias_to_tas;
  SetBasic().EnergyHeight = (Basic().aircraft.TrueAirspeed * Basic().aircraft.TrueAirspeed - V_target * V_target) * fixed_inv_2g;
  SetBasic().TEAltitude = Basic().aircraft.NavAltitude + Basic().EnergyHeight;
}


void
DeviceBlackboard::WorkingBand()
{
  const fixed working_band_height = Basic().TEAltitude 
    - SettingsComputer().SafetyAltitudeBreakoff 
    - Calculated().TerrainBase;
 
  SetBasic().working_band_height = working_band_height;
  
  if (negative(SetBasic().working_band_height)) {
    SetBasic().aircraft.working_band_fraction = fixed_zero;
    return;
  }
  const fixed max_height = Calculated().MaxThermalHeight;
  if (positive(max_height)) {
    SetBasic().aircraft.working_band_fraction =
      working_band_height / max_height;
  } else {
    SetBasic().aircraft.working_band_fraction = fixed_one;
  }
}

void
DeviceBlackboard::FlightState(const GlidePolar& glide_polar)
{
  if (Basic().aircraft.Time < LastBasic().aircraft.Time) {
    SetBasic().aircraft.flying_state_reset();
  }
    // GPS not lost
  if (Basic().gps.NAVWarning) {
    return;
  }

  // Speed too high for being on the ground
  if (Basic().aircraft.Speed > glide_polar.get_Vtakeoff()) {
    SetBasic().aircraft.flying_state_moving(Basic().aircraft.Time);
  } else {
    const bool on_ground = Calculated().TerrainValid &&
      Basic().aircraft.AltitudeAGL < 300;
    SetBasic().aircraft.flying_state_stationary(Basic().aircraft.Time, on_ground);
  }
}

void
DeviceBlackboard::AutoQNH()
{
  static int countdown_autoqnh = 0;

  if (!Basic().aircraft.OnGround // must be on ground
      || !countdown_autoqnh    // only do it once
      || Basic().gps.Replay // never in replay mode
      || Basic().gps.NAVWarning // Reject if no valid GPS fix
      || !Basic().BaroAltitudeAvailable // Reject if no baro altitude
    ) {
    countdown_autoqnh= 10; // restart...
    return;
  }

  if (countdown_autoqnh) {
    countdown_autoqnh--;
  }
  if (!countdown_autoqnh) {
    SetBasic().pressure.FindQNH(Basic().BaroAltitude, 
                                Calculated().TerrainAlt);
    AllDevicesPutQNH(Basic().pressure);
  }
}


void
DeviceBlackboard::SetQNH(fixed qnh)
{
  ScopeLock protect(mutexBlackboard);
  SetBasic().pressure.set_QNH(qnh);
  AllDevicesPutQNH(Basic().pressure);
}

void
DeviceBlackboard::SetMC(fixed mc)
{
  ScopeLock protect(mutexBlackboard);
  SetBasic().MacCready = mc;
  AllDevicesPutMacCready(mc);
}

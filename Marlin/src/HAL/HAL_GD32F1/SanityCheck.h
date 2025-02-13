/**
 * Marlin 3D Printer Firmware
 * Copyright (C) 2019 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (C) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * HAL for stm32duino.com based on Libmaple and compatible (STM32F1)
 */

/**
 * Test Re-ARM specific configuration values for errors at compile-time.
 */
#if ENABLED(SPINDLE_LASER_ENABLE)
  #if !PIN_EXISTS(SPINDLE_LASER_ENABLE)
    #error "SPINDLE_LASER_ENABLE requires SPINDLE_LASER_ENABLE_PIN."
  #elif SPINDLE_DIR_CHANGE && !PIN_EXISTS(SPINDLE_DIR)
    #error "SPINDLE_DIR_PIN not defined."
  #elif ENABLED(SPINDLE_LASER_PWM) && PIN_EXISTS(SPINDLE_LASER_PWM)
    #if !PWM_PIN(SPINDLE_LASER_PWM_PIN)
      #error "SPINDLE_LASER_PWM_PIN not assigned to a PWM pin."
    #elif SPINDLE_LASER_POWERUP_DELAY < 0
      #error "SPINDLE_LASER_POWERUP_DELAY must be positive"
    #elif SPINDLE_LASER_POWERDOWN_DELAY < 0
      #error "SPINDLE_LASER_POWERDOWN_DELAY must be positive"
    #elif !defined(SPINDLE_LASER_PWM_INVERT)
      #error "SPINDLE_LASER_PWM_INVERT missing."
    #elif !defined(SPEED_POWER_SLOPE) || !defined(SPEED_POWER_INTERCEPT) || !defined(SPEED_POWER_MIN) || !defined(SPEED_POWER_MAX)
      #error "SPINDLE_LASER_PWM equation constant(s) missing."
    #elif PIN_EXISTS(CASE_LIGHT) && SPINDLE_LASER_PWM_PIN == CASE_LIGHT_PIN
      #error "SPINDLE_LASER_PWM_PIN is used by CASE_LIGHT_PIN."
    #elif PIN_EXISTS(E0_AUTO_FAN) && SPINDLE_LASER_PWM_PIN == E0_AUTO_FAN_PIN
      #error "SPINDLE_LASER_PWM_PIN is used by E0_AUTO_FAN_PIN."
    #elif PIN_EXISTS(E1_AUTO_FAN) && SPINDLE_LASER_PWM_PIN == E1_AUTO_FAN_PIN
      #error "SPINDLE_LASER_PWM_PIN is used by E1_AUTO_FAN_PIN."
    #elif PIN_EXISTS(E2_AUTO_FAN) && SPINDLE_LASER_PWM_PIN == E2_AUTO_FAN_PIN
      #error "SPINDLE_LASER_PWM_PIN is used by E2_AUTO_FAN_PIN."
    #elif PIN_EXISTS(E3_AUTO_FAN) && SPINDLE_LASER_PWM_PIN == E3_AUTO_FAN_PIN
      #error "SPINDLE_LASER_PWM_PIN is used by E3_AUTO_FAN_PIN."
    #elif PIN_EXISTS(E4_AUTO_FAN) && SPINDLE_LASER_PWM_PIN == E4_AUTO_FAN_PIN
      #error "SPINDLE_LASER_PWM_PIN is used by E4_AUTO_FAN_PIN."
    #elif PIN_EXISTS(FAN) && SPINDLE_LASER_PWM_PIN == FAN_PIN
      #error "SPINDLE_LASER_PWM_PIN is used FAN_PIN."
    #elif PIN_EXISTS(FAN1) && SPINDLE_LASER_PWM_PIN == FAN1_PIN
      #error "SPINDLE_LASER_PWM_PIN is used FAN1_PIN."
    #elif PIN_EXISTS(FAN2) && SPINDLE_LASER_PWM_PIN == FAN2_PIN
      #error "SPINDLE_LASER_PWM_PIN is used FAN2_PIN."
    #elif PIN_EXISTS(CONTROLLERFAN) && SPINDLE_LASER_PWM_PIN == CONTROLLERFAN_PIN
      #error "SPINDLE_LASER_PWM_PIN is used by CONTROLLERFAN_PIN."
    #endif
  #endif
#endif // SPINDLE_LASER_ENABLE

#if ENABLED(EMERGENCY_PARSER)
  #error "EMERGENCY_PARSER is not yet implemented for STM32F1. Disable EMERGENCY_PARSER to continue."
#endif

#if ENABLED(SDIO_SUPPORT) && DISABLED(SDSUPPORT)
  #error "SDIO_SUPPORT requires SDSUPPORT. Enable SDSUPPORT to continue."
#elif ENABLED(UDISK_SUPPORT) && DISABLED(SDSUPPORT)
  #error "UDISK_SUPPORT requires SDSUPPORT. Enable SDSUPPORT to continue."
#endif

/**
 * Fixed-Time Motion limitations
 */
#if ENABLED(FT_MOTION)
  #if ENABLED(MIXING_EXTRUDER)
    #error "FT_MOTION does not currently support MIXING_EXTRUDER."
  #elif DISABLED(FTM_UNIFIED_BWS)
    #error "FT_MOTION requires FTM_UNIFIED_BWS to be enabled because FBS is not yet implemented."
  #endif
#endif

/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2023 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "../inc/MarlinConfig.h"

#include "../snapmaker/src/snapmaker.h"

#if ENABLED(FT_MOTION)

#include "ft_motion.h"
#include "stepper.h" // Access stepper block queue function and abort status.

FTMotion ftMotion;

#if !HAS_X_AXIS
  static_assert(FTM_DEFAULT_MODE == ftMotionMode_ZV, "ftMotionMode_ZV requires at least one linear axis.");
  static_assert(FTM_DEFAULT_MODE == ftMotionMode_ZVD, "ftMotionMode_ZVD requires at least one linear axis.");
  static_assert(FTM_DEFAULT_MODE == ftMotionMode_ZVDD, "ftMotionMode_ZVD requires at least one linear axis.");
  static_assert(FTM_DEFAULT_MODE == ftMotionMode_ZVDDD, "ftMotionMode_ZVD requires at least one linear axis.");
  static_assert(FTM_DEFAULT_MODE == ftMotionMode_EI, "ftMotionMode_EI requires at least one linear axis.");
  static_assert(FTM_DEFAULT_MODE == ftMotionMode_2HEI, "ftMotionMode_2HEI requires at least one linear axis.");
  static_assert(FTM_DEFAULT_MODE == ftMotionMode_3HEI, "ftMotionMode_3HEI requires at least one linear axis.");
  static_assert(FTM_DEFAULT_MODE == ftMotionMode_MZV, "ftMotionMode_MZV requires at least one linear axis.");
#endif
#if !HAS_DYNAMIC_FREQ_MM
  static_assert(FTM_DEFAULT_DYNFREQ_MODE != dynFreqMode_Z_BASED, "dynFreqMode_Z_BASED requires a Z axis.");
#endif
#if !HAS_DYNAMIC_FREQ_G
  static_assert(FTM_DEFAULT_DYNFREQ_MODE != dynFreqMode_MASS_BASED, "dynFreqMode_MASS_BASED requires an X axis and an extruder.");
#endif

//-----------------------------------------------------------------
// Variables.
//-----------------------------------------------------------------

// Public variables.

ft_config_t FTMotion::cfg;
bool FTMotion::busy; // = false
int32_t FTMotion::positionSyncBuff[FTM_SYNC_POSITION_SIZE][NUM_AXIS_ENUMS] = {0U};
int8_t FTMotion::positionSyncIndex = 0;
int8_t  FTMotion::blockInfoSyncBuffIndex = 0;
FtMotionBlockInfo_t FTMotion::blockInfoSyncBuff[FT_MOTION_BLOCK_INFO_BUFF_SIZE] = {0U};
FtMotionBlockInfo_t FTMotion::ft_current_block = {0};
ft_command_t FTMotion::stepperCmdBuff[FTM_STEPPERCMD_BUFF_SIZE] = {0U}; // Stepper commands buffer.
int32_t FTMotion::stepperCmdBuff_produceIdx = 0, // Index of next stepper command write to the buffer.
        FTMotion::stepperCmdBuff_consumeIdx = 0; // Index of next stepper command read from the buffer.

bool FTMotion::sts_stepperBusy = false;         // The stepper buffer has items and is in use.
// Private variables.

// NOTE: These are sized for Ulendo FBS use.
xyze_trajectory_t    FTMotion::traj;            // = {0.0f} Storage for fixed-time-based trajectory.
xyze_trajectoryMod_t FTMotion::trajMod;         // = {0.0f} Storage for fixed time trajectory window.

bool FTMotion::blockProcRdy = false,            // Indicates a block is ready to be processed.
     FTMotion::blockProcRdy_z1 = false,         // Storage for the previous indicator.
     FTMotion::blockProcDn = false;             // Indicates current block is done being processed.
bool FTMotion::batchRdy = false;                // Indicates a batch of the fixed time trajectory
                                                //  has been generated, is now available in the upper -
                                                //  half of traj.x[], y, z ... e vectors, and is ready to be
                                                //  post processed, if applicable, then interpolated.
bool FTMotion::batchRdyForInterp = false;       // Indicates the batch is done being post processed,
                                                //  if applicable, and is ready to be converted to step commands.
bool FTMotion::runoutEna = false;               // True if runout of the block hasn't been done and is allowed.
bool FTMotion::blockDataIsRunout = false;       // Indicates the last loaded block variables are for a runout.

// Trapezoid data variables.
xyze_pos_t   FTMotion::startPosn,                     // (mm) Start position of block
             FTMotion::endPosn_prevBlock = { 0.0f };  // (mm) End position of previous block
xyze_float_t FTMotion::ratio;                         // (ratio) Axis move ratio of block
float FTMotion::accel_P,                        // Acceleration prime of block. [mm/sec/sec]
      FTMotion::decel_P,                        // Deceleration prime of block. [mm/sec/sec]
      FTMotion::F_P,                            // Feedrate prime of block. [mm/sec]
      FTMotion::f_s,                            // Starting feedrate of block. [mm/sec]
      FTMotion::s_1e,                           // Position after acceleration phase of block.
      FTMotion::s_2e;                           // Position after acceleration and coasting phase of block.

uint32_t FTMotion::N1,                          // Number of data points in the acceleration phase.
         FTMotion::N2,                          // Number of data points in the coasting phase.
         FTMotion::N3;                          // Number of data points in the deceleration phase.

uint32_t FTMotion::max_intervals;               // Total number of data points that will be generated from block.

// Make vector variables.
uint32_t FTMotion::makeVector_idx = 0,          // Index of fixed time trajectory generation of the overall block.
         FTMotion::makeVector_idx_z1 = 0,       // Storage for the previously calculated index above.
         FTMotion::makeVector_batchIdx = 0;     // Index of fixed time trajectory generation within the batch.

// Interpolation variables.
xyze_long_t FTMotion::steps = { 0 };            // Step count accumulator.

uint32_t FTMotion::interpIdx = 0,               // Index of current data point being interpolated.
         FTMotion::interpIdx_z1 = 0;            // Storage for the previously calculated index above.

// Shaping variables.
#if HAS_X_AXIS
  FTMotion::shaping_t FTMotion::shaping = {0};
  // below initialization will cause build errors, so use above instead it, by scott
  // actual init will be done in FTMotion::init()
  // FTMotion::shaping_t FTMotion::shaping = {
  //   0, 0,
  //   { { 0.0f }, { 0.0f }, { 0 } },            // d_zi, Ai, Ni
  //   #if HAS_Y_AXIS
  //     { { 0.0f }, { 0.0f }, { 0 } }           // d_zi, Ai, Ni
  //   #endif
  // };
#endif

#if HAS_EXTRUDERS
  // Linear advance variables.
  float FTMotion::e_raw_z1 = 0.0f;        // (ms) Unit delay of raw extruder position.
  float FTMotion::e_advanced_z1 = 0.0f;   // (ms) Unit delay of advanced extruder position.
#endif

constexpr uint32_t last_batchIdx = (FTM_WINDOW_SIZE) - (FTM_BATCH_SIZE);

//-----------------------------------------------------------------
// Function definitions.
//-----------------------------------------------------------------

// Public functions.

// Sets controller states to begin processing a block.
void FTMotion::startBlockProc() {
  blockProcRdy = true;
  blockProcDn = false;
  runoutEna = true;
}

// Move any free data points to the stepper buffer even if a full batch isn't ready.
void FTMotion::runoutBlock() {

  if (!runoutEna) return;

  startPosn = endPosn_prevBlock;
  ratio.reset();

  max_intervals = cfg.modeHasShaper() ? shaper_intervals : 0;
  if (max_intervals <= TERN(FTM_UNIFIED_BWS, FTM_BATCH_SIZE, min_max_intervals - (FTM_BATCH_SIZE)))
    max_intervals = min_max_intervals;

  max_intervals += (
    #if ENABLED(FTM_UNIFIED_BWS)
      FTM_WINDOW_SIZE - makeVector_batchIdx
    #else
      FTM_WINDOW_SIZE - ((last_batchIdx < (FTM_BATCH_SIZE)) ? 0 : makeVector_batchIdx)
    #endif
  );
  blockProcRdy = blockDataIsRunout = true;
  runoutEna = blockProcDn = false;
}

void FTMotion::addSyncCommand(block_t *blk) {
  if (blk->sync_e) {
    stepperCmdBuff[stepperCmdBuff_produceIdx] = _BV(FT_BIT_SYNC_POS_E);
    positionSyncBuff[positionSyncIndex][E_AXIS] = blk->position[E_AXIS];
  }
  else {
    stepperCmdBuff[stepperCmdBuff_produceIdx] = _BV(FT_BIT_SYNC_POS);
    // copy XYZ B axes
    memcpy(positionSyncBuff[positionSyncIndex], blk->position, sizeof(int32_t) * XN);
  }

  // record index
  stepperCmdBuff[stepperCmdBuff_produceIdx] |= positionSyncIndex;
  if (++positionSyncIndex >= FTM_SYNC_POSITION_SIZE)
    positionSyncIndex = 0;

  if (++stepperCmdBuff_produceIdx >= FTM_STEPPERCMD_BUFF_SIZE)
    stepperCmdBuff_produceIdx = 0;
}

void FTMotion::addSyncCommandBlockInfo(block_t *blk) {
  if (NULL == blk) {
    return;
  }

  blockInfoSyncBuff[blockInfoSyncBuffIndex].new_block_file_position = blk->filePos;
  blockInfoSyncBuff[blockInfoSyncBuffIndex].new_block_steps_x = blk->steps[X_AXIS];
  blockInfoSyncBuff[blockInfoSyncBuffIndex].new_block_steps_y = blk->steps[Y_AXIS];
  blockInfoSyncBuff[blockInfoSyncBuffIndex].new_block_steps_e = blk->steps[E_AXIS] 
      * (TEST(blk->direction_bits, E_AXIS) ? -1 : 1);

  stepperCmdBuff[stepperCmdBuff_produceIdx] = _BV(FT_BIT_SYNC_BLOCK_INFO);
  stepperCmdBuff[stepperCmdBuff_produceIdx] |= blockInfoSyncBuffIndex;

  if (++blockInfoSyncBuffIndex >= FT_MOTION_BLOCK_INFO_BUFF_SIZE) {
    blockInfoSyncBuffIndex = 0;
  }

  if (++stepperCmdBuff_produceIdx >= FTM_STEPPERCMD_BUFF_SIZE) {
    stepperCmdBuff_produceIdx = 0;
  }
}

// Controller main, to be invoked from non-isr task.
void FTMotion::loop() {

  if (sm2_handle->marlin != xTaskGetCurrentTaskHandle())
    return;

  if (!cfg.mode) return;

  // Handle block abort with the following sequence:
  // 1. Zero out commands in stepper ISR.
  // 2. Drain the motion buffer, stop processing until they are emptied.
  // 3. Reset all the states / memory.
  // 4. Signal ready for new block.
  if (stepper.abort_current_block) {
    portDISABLE_INTERRUPTS();
    reset();
    blockProcDn = true;                   // Set queueing to look for next block.
    stepper.abort_current_block = false;
    planner.new_block = 0;
    sts_stepperBusy = false;
    portENABLE_INTERRUPTS();
  }

  if (quickstop.isInStopping()) {
    portDISABLE_INTERRUPTS();
    reset();
    blockProcDn = true;                   // Set queueing to look for next block.
    sts_stepperBusy = false;
    portENABLE_INTERRUPTS();
  }

  // Planner processing and block conversion.
  if (!blockProcRdy) {
    if (planner.new_block) {
      planner.new_block = 0;
      planner.recalculate_trapezoids();
    }
    stepper.ftMotion_blockQueueUpdate();
  }

  if (blockProcRdy) {
    if (!blockProcRdy_z1) { // One-shot.
      if (!blockDataIsRunout) loadBlockData(stepper.current_block);
      else blockDataIsRunout = false;
    }
    // blockProcDn 表示当前block是否已经处理完毕
    while (!blockProcDn && !batchRdy && (makeVector_idx - makeVector_idx_z1 < (FTM_POINTS_PER_LOOP)))
      makeVector();
  }

  // FBS / post processing.
  if (batchRdy && !batchRdyForInterp) {

    // Call Ulendo FBS here.

    #if ENABLED(FTM_UNIFIED_BWS)
      trajMod = traj; // Move the window to traj
    #else
      // Copy the uncompensated vectors.
      #define TCOPY(A) memcpy(trajMod.A, traj.A, sizeof(trajMod.A))
      LOGICAL_AXIS_CODE(
        TCOPY(e),
        TCOPY(x), TCOPY(y), TCOPY(z),
        TCOPY(i), TCOPY(j), TCOPY(k),
        TCOPY(u), TCOPY(v), TCOPY(w)
      );

      // Shift the time series back in the window
      #define TSHIFT(A) memcpy(traj.A, &traj.A[FTM_BATCH_SIZE], last_batchIdx * sizeof(traj.A[0]))
      LOGICAL_AXIS_CODE(
        TSHIFT(e),
        TSHIFT(x), TSHIFT(y), TSHIFT(z),
        TSHIFT(i), TSHIFT(j), TSHIFT(k),
        TSHIFT(u), TSHIFT(v), TSHIFT(w)
      );
    #endif

    // ... data is ready in trajMod.
    batchRdyForInterp = true;

    batchRdy = false; // Clear so makeVector() can resume generating points.
  }

  // Interpolation.
  while (batchRdyForInterp
    && (stepperCmdBuffItems() < (FTM_STEPPERCMD_BUFF_SIZE) - (FTM_STEPS_PER_UNIT_TIME))
    && (interpIdx - interpIdx_z1 < (FTM_STEPS_PER_LOOP))
  ) {
    convertToSteps(interpIdx);
    if (++interpIdx == FTM_BATCH_SIZE) {
      batchRdyForInterp = false;
      interpIdx = 0;
    }
  }

  // Report busy status to planner.
  busy = (sts_stepperBusy || ((!blockProcDn && blockProcRdy) || batchRdy || batchRdyForInterp || runoutEna));

  blockProcRdy_z1 = blockProcRdy;
  makeVector_idx_z1 = makeVector_idx;
  interpIdx_z1 = interpIdx;

  return;
}

#if HAS_X_AXIS

  // Refresh the gains used by shaping functions.
  // To be called on init or mode or zeta change.

  void FTMotion::Shaping::updateShapingA(float zeta[]/*=cfg.zeta*/, float vtol[]/*=cfg.vtol*/) {

    const float Kx = exp(-zeta[0] * M_PI / sqrt(1.0f - sq(zeta[0]))),
                Ky = exp(-zeta[1] * M_PI / sqrt(1.0f - sq(zeta[1]))),
                Kx2 = sq(Kx),
                Ky2 = sq(Ky);

    switch (cfg.mode) {

      case ftMotionMode_ZV:
        max_i = 1U;
        x.Ai[0] = 1.0f / (1.0f + Kx);
        x.Ai[1] = x.Ai[0] * Kx;

        y.Ai[0] = 1.0f / (1.0f + Ky);
        y.Ai[1] = y.Ai[0] * Ky;
        break;

      case ftMotionMode_ZVD:
        max_i = 2U;
        x.Ai[0] = 1.0f / (1.0f + 2.0f * Kx + Kx2);
        x.Ai[1] = x.Ai[0] * 2.0f * Kx;
        x.Ai[2] = x.Ai[0] * Kx2;

        y.Ai[0] = 1.0f / (1.0f + 2.0f * Ky + Ky2);
        y.Ai[1] = y.Ai[0] * 2.0f * Ky;
        y.Ai[2] = y.Ai[0] * Ky2;
        break;

      case ftMotionMode_ZVDD:
        max_i = 3U;
        x.Ai[0] = 1.0f / (1.0f + 3.0f * Kx + 3.0f * Kx2 + cu(Kx));
        x.Ai[1] = x.Ai[0] * 3.0f * Kx;
        x.Ai[2] = x.Ai[0] * 3.0f * Kx2;
        x.Ai[3] = x.Ai[0] * cu(Kx);

        y.Ai[0] = 1.0f / (1.0f + 3.0f * Ky + 3.0f * Ky2 + cu(Ky));
        y.Ai[1] = y.Ai[0] * 3.0f * Ky;
        y.Ai[2] = y.Ai[0] * 3.0f * Ky2;
        y.Ai[3] = y.Ai[0] * cu(Ky);
        break;

      case ftMotionMode_ZVDDD:
        max_i = 4U;
        x.Ai[0] = 1.0f / (1.0f + 4.0f * Kx + 6.0f * Kx2 + 4.0f * cu(Kx) + sq(Kx2));
        x.Ai[1] = x.Ai[0] * 4.0f * Kx;
        x.Ai[2] = x.Ai[0] * 6.0f * Kx2;
        x.Ai[3] = x.Ai[0] * 4.0f * cu(Kx);
        x.Ai[4] = x.Ai[0] * sq(Kx2);

        y.Ai[0] = 1.0f / (1.0f + 4.0f * Ky + 6.0f * Ky2 + 4.0f * cu(Ky) + sq(Ky2));
        y.Ai[1] = y.Ai[0] * 4.0f * Ky;
        y.Ai[2] = y.Ai[0] * 6.0f * Ky2;
        y.Ai[3] = y.Ai[0] * 4.0f * cu(Ky);
        y.Ai[4] = y.Ai[0] * sq(Ky2);
        break;

      case ftMotionMode_EI: {
        max_i = 2U;
        x.Ai[0] = 0.25f * (1.0f + vtol[0]);
        x.Ai[1] = 0.50f * (1.0f - vtol[0]) * Kx;
        x.Ai[2] = x.Ai[0] * Kx2;

        y.Ai[0] = 0.25f * (1.0f + vtol[1]);
        y.Ai[1] = 0.50f * (1.0f - vtol[1]) * Ky;
        y.Ai[2] = y.Ai[0] * Ky2;

        const float X_adj = 1.0f / (x.Ai[0] + x.Ai[1] + x.Ai[2]);
        const float Y_adj = 1.0f / (y.Ai[0] + y.Ai[1] + y.Ai[2]);
        for (uint32_t i = 0U; i < 3U; i++) {
          x.Ai[i] *= X_adj;
          y.Ai[i] *= Y_adj;
        }
      }
      break;

      case ftMotionMode_2HEI: {
        max_i = 3U;
        const float vtolx2 = sq(vtol[0]);
        const float X = pow(vtolx2 * (sqrt(1.0f - vtolx2) + 1.0f), 1.0f / 3.0f);
        x.Ai[0] = (3.0f * sq(X) + 2.0f * X + 3.0f * vtolx2) / (16.0f * X);
        x.Ai[1] = (0.5f - x.Ai[0]) * Kx;
        x.Ai[2] = x.Ai[1] * Kx;
        x.Ai[3] = x.Ai[0] * cu(Kx);

        const float vtoly2 = sq(vtol[1]);
        const float Y = pow(vtoly2 * (sqrt(1.0f - vtoly2) + 1.0f), 1.0f / 3.0f);
        y.Ai[0] = (3.0f * sq(Y) + 2.0f * Y + 3.0f * vtoly2) / (16.0f * Y);
        y.Ai[1] = (0.5f - y.Ai[0]) * Ky;
        y.Ai[2] = y.Ai[1] * Ky;
        y.Ai[3] = y.Ai[0] * cu(Ky);

        const float X_adj = 1.0f / (x.Ai[0] + x.Ai[1] + x.Ai[2] + x.Ai[3]);
        const float Y_adj = 1.0f / (y.Ai[0] + y.Ai[1] + y.Ai[2] + y.Ai[3]);
        for (uint32_t i = 0U; i < 4U; i++) {
          x.Ai[i] *= X_adj;
          y.Ai[i] *= Y_adj;
        }
      }
      break;

      case ftMotionMode_3HEI: {
        max_i = 4U;
        x.Ai[0] = 0.0625f * ( 1.0f + 3.0f * vtol[0] + 2.0f * sqrt( 2.0f * ( vtol[0] + 1.0f ) * vtol[0] ) );
        x.Ai[1] = 0.25f * ( 1.0f - vtol[0] ) * Kx;
        x.Ai[2] = ( 0.5f * ( 1.0f + vtol[0] ) - 2.0f * x.Ai[0] ) * Kx2;
        x.Ai[3] = x.Ai[1] * Kx2;
        x.Ai[4] = x.Ai[0] * sq(Kx2);

        y.Ai[0] = 0.0625f * (1.0f + 3.0f * vtol[1] + 2.0f * sqrt(2.0f * (vtol[1] + 1.0f) * vtol[1]));
        y.Ai[1] = 0.25f * (1.0f - vtol[1]) * Ky;
        y.Ai[2] = (0.5f * (1.0f + vtol[1]) - 2.0f * y.Ai[0]) * Ky2;
        y.Ai[3] = y.Ai[1] * Ky2;
        y.Ai[4] = y.Ai[0] * sq(Ky2);

        const float X_adj = 1.0f / (x.Ai[0] + x.Ai[1] + x.Ai[2] + x.Ai[3] + x.Ai[4]);
        const float Y_adj = 1.0f / (y.Ai[0] + y.Ai[1] + y.Ai[2] + y.Ai[3] + y.Ai[4]);
        for (uint32_t i = 0U; i < 5U; i++) {
          x.Ai[i] *= X_adj;
          y.Ai[i] *= Y_adj;
        }
      }
      break;

      case ftMotionMode_MZV: {
        max_i = 2U;
        const float Bx = 1.4142135623730950488016887242097f * Kx;
        x.Ai[0] = 1.0f / (1.0f + Bx + Kx2);
        x.Ai[1] = x.Ai[0] * Bx;
        x.Ai[2] = x.Ai[0] * Kx2;

        const float By = 1.4142135623730950488016887242097f * Ky;
        y.Ai[0] = 1.0f / (1.0f + By + Ky2);
        y.Ai[1] = y.Ai[0] * By;
        y.Ai[2] = y.Ai[0] * Ky2;
      }
      break;

      default:
        ZERO(x.Ai);
        ZERO(y.Ai);
        max_i = 0;
    }

  }

  void FTMotion::updateShapingA(float zeta[]/*=cfg.zeta*/, float vtol[]/*=cfg.vtol*/) {
    shaping.updateShapingA(zeta, vtol);
  }

  // Refresh the indices used by shaping functions.
  // To be called when frequencies change.

  void FTMotion::AxisShaping::updateShapingN(const_float_t f, const_float_t df) {
    // Protections omitted for DBZ and for index exceeding array length.
    switch (cfg.mode) {
      case ftMotionMode_ZV:
        Ni[1] = round((0.5f / f / df) * (FTM_FS));
        break;
      case ftMotionMode_ZVD:
      case ftMotionMode_EI:
        Ni[1] = round((0.5f / f / df) * (FTM_FS));
        Ni[2] = Ni[1] + Ni[1];
        break;
      case ftMotionMode_ZVDD:
      case ftMotionMode_2HEI:
        Ni[1] = round((0.5f / f / df) * (FTM_FS));
        Ni[2] = Ni[1] + Ni[1];
        Ni[3] = Ni[2] + Ni[1];
        break;
      case ftMotionMode_ZVDDD:
      case ftMotionMode_3HEI:
        Ni[1] = round((0.5f / f / df) * (FTM_FS));
        Ni[2] = Ni[1] + Ni[1];
        Ni[3] = Ni[2] + Ni[1];
        Ni[4] = Ni[3] + Ni[1];
        break;
      case ftMotionMode_MZV:
        Ni[1] = round((0.375f / f / df) * (FTM_FS));
        Ni[2] = Ni[1] + Ni[1];
        break;
      default: ZERO(Ni);
    }
  }

  void FTMotion::updateShapingN(const_float_t xf OPTARG(HAS_Y_AXIS, const_float_t yf), float zeta[]/*=cfg.zeta*/) {
    const float xdf = sqrt(1.0f - sq(zeta[0]));
    shaping.x.updateShapingN(xf, xdf);

    #if HAS_Y_AXIS
      const float ydf = sqrt(1.0f - sq(zeta[1]));
      shaping.y.updateShapingN(yf, ydf);
    #endif
  }

#endif // HAS_X_AXIS

// Reset all trajectory processing variables.
void FTMotion::reset() {

  stepperCmdBuff_produceIdx = stepperCmdBuff_consumeIdx = 0;

  traj.reset();
  trajMod.reset();

  blockProcRdy = blockProcRdy_z1 = blockProcDn = false;
  batchRdy = batchRdyForInterp = false;
  runoutEna = false;

  endPosn_prevBlock.reset();

  makeVector_idx = makeVector_idx_z1 = 0;
  makeVector_batchIdx = TERN(FTM_UNIFIED_BWS, 0, _MAX(last_batchIdx, FTM_BATCH_SIZE));

  steps.reset();
  interpIdx = interpIdx_z1 = 0;

  #if HAS_X_AXIS
    ZERO(shaping.x.d_zi);
    TERN_(HAS_Y_AXIS, ZERO(shaping.y.d_zi));
    shaping.zi_idx = 0;
  #endif
  TERN_(HAS_EXTRUDERS, e_raw_z1 = e_advanced_z1 = 0.0f);

  memset(&ftMotion.ft_current_block, 0, sizeof(ftMotion.ft_current_block));
  memset(blockInfoSyncBuff, 0, sizeof(blockInfoSyncBuff));
  blockInfoSyncBuffIndex = 0;
  memset(positionSyncBuff, 0, sizeof(positionSyncBuff));
  positionSyncIndex = 0;
}

// Private functions.

// Auxiliary function to get number of step commands in the buffer.
int32_t FTMotion::stepperCmdBuffItems() {
  const int32_t udiff = stepperCmdBuff_produceIdx - stepperCmdBuff_consumeIdx;
  return (udiff < 0) ? udiff + (FTM_STEPPERCMD_BUFF_SIZE) : udiff;
}

ftMotionMode_t FTMotion::disable() {
  auto m = cfg.mode;
  planner.synchronize();
  cfg.mode = ftMotionMode_DISABLED;
  stepper.ftMotion_syncPosition();
  reset();
  return m;
}

void FTMotion::setMode(const ftMotionMode_t &m) {
  planner.synchronize();
  cfg.mode = m; 
  #if HAS_X_AXIS
    if (cfg.modeHasShaper()) {
      ftMotion.refreshShapingN();
      ftMotion.updateShapingA();
    }
  #endif
}

// Initializes storage variables before startup.
void FTMotion::init() {
  shaping.max_i = 0;
  shaping.zi_idx = 0;
  for (int i = 0; i < 5; ++i) {
    shaping.x.Ai[i] = 0.0f;
    shaping.x.Ni[i] = 0;
    shaping.y.Ai[i] = 0.0f;
    shaping.y.Ni[i] = 0;
  }
  for (int i = 0; i < FTM_ZMAX; ++i) {
    shaping.x.d_zi[i] = 0.0f;
    shaping.y.d_zi[i] = 0.0f;
  }
  #if HAS_X_AXIS
    refreshShapingN();
    updateShapingA();
  #endif
  reset(); // Precautionary.
}

// Loads / converts block data from planner to fixed-time control variables.
void FTMotion::loadBlockData(block_t * const current_block) {

  const float totalLength = current_block->millimeters, // 原始移动长度
              oneOverLength = 1.0f / totalLength;

  startPosn = endPosn_prevBlock;

  xyze_pos_t moveDist = LOGICAL_AXIS_ARRAY(
    current_block->steps[E_AXIS] * planner.steps_to_mm[E_AXIS] * (TEST(current_block->direction_bits, E_AXIS) ? -1 : 1),
    current_block->steps[X_AXIS] * planner.steps_to_mm[X_AXIS] * (TEST(current_block->direction_bits, X_AXIS) ? -1 : 1),
    current_block->steps[Y_AXIS] * planner.steps_to_mm[Y_AXIS] * (TEST(current_block->direction_bits, Y_AXIS) ? -1 : 1),
    current_block->steps[Z_AXIS] * planner.steps_to_mm[Z_AXIS] * (TEST(current_block->direction_bits, Z_AXIS) ? -1 : 1),
    current_block->steps[I_AXIS] * planner.steps_to_mm[I_AXIS] * (TEST(current_block->direction_bits, I_AXIS) ? -1 : 1),
    current_block->steps[J_AXIS] * planner.steps_to_mm[J_AXIS] * (TEST(current_block->direction_bits, J_AXIS) ? -1 : 1),
    current_block->steps[K_AXIS] * planner.steps_to_mm[K_AXIS] * (TEST(current_block->direction_bits, K_AXIS) ? -1 : 1),
    current_block->steps[U_AXIS] * planner.steps_to_mm[U_AXIS] * (TEST(current_block->direction_bits, U_AXIS) ? -1 : 1),
    current_block->steps[V_AXIS] * planner.steps_to_mm[V_AXIS] * (TEST(current_block->direction_bits, V_AXIS) ? -1 : 1),
    current_block->steps[W_AXIS] * planner.steps_to_mm[W_AXIS] * (TEST(current_block->direction_bits, W_AXIS) ? -1 : 1)
  );

  ratio = moveDist * oneOverLength; // 算出各轴移动距离，与合成运动距离的比值，耦合方向，有正负号

  const float spm = totalLength / current_block->step_event_count;  // (steps/mm) Distance for each step 每步移动的距离，step_event_count是最长轴的步数

  f_s = spm * current_block->initial_rate;              // (steps/s) Start feedrate 每步移动的距离，乘以1s发出的步数，那就是1s移动的距离，所以得到速度，initial_rate是初始的step rate

  const float f_e = spm * current_block->final_rate;    // (steps/s) End feedrate

  /* Keep for comprehension
  const float a = current_block->acceleration,          // (mm/s^2) Same magnitude for acceleration or deceleration
              oneby2a = 1.0f / (2.0f * a),              // (s/mm) Time to accelerate or decelerate one mm (i.e., oneby2a * 2 // 加速/减速到1mm/s的时间
              oneby2d = -oneby2a;                       // (s/mm) Time to accelerate or decelerate one mm (i.e., oneby2a * 2
  const float fsSqByTwoA = sq(f_s) * oneby2a,           // (mm) Distance to accelerate from start speed to nominal speed // 公式2aS = V1^2 - V0^2
              feSqByTwoD = sq(f_e) * oneby2d;           // (mm) Distance to decelerate from nominal speed to end speed

  float F_n = current_block->nominal_speed;             // (mm/s) Speed we hope to achieve, if possible
  const float fdiff = feSqByTwoD - fsSqByTwoA,          // (mm) Coasting distance if nominal speed is reached // 巡航速度的移动距离，如果能达到的话
              odiff = oneby2a - oneby2d,                // (i.e., oneby2a * 2) (mm/s) Change in speed for one second of acceleration // 1s钟内的速度变化率
              ldiff = totalLength - fdiff;              // (mm) Distance to travel if nominal speed is reached // 总距离减去加减速的距离

  float T2 = (1.0f / F_n) * (ldiff - odiff * sq(F_n));  // (s) Coasting duration after nominal speed reached  公式 T = S / V
  if (T2 < 0.0f) {
    T2 = 0.0f;
    F_n = SQRT(ldiff / odiff);                          // Clip by intersection if nominal speed can't be reached.
  }

  const float T1 = (F_n - f_s) / a,                     // (s) Accel Time = difference in feedrate over acceleration 加速时间
              T3 = (F_n - f_e) / a;                     // (s) Decel Time = difference in feedrate over acceleration 减速时间
  */

  const float accel = current_block->acceleration,
              oneOverAccel = 1.0f / accel;

  float F_n = current_block->nominal_speed;
  const float ldiff = totalLength + 0.5f * oneOverAccel * (sq(f_s) + sq(f_e)); // 总距离减去 加速距离和减速距离 S - Sa - Sa

  float T2 = ldiff / F_n - oneOverAccel * F_n; // 巡航距离/巡航速度 - 巡航速度/加速度
  if (T2 < 0.0f) {
    T2 = 0.0f;
    F_n = SQRT(ldiff * accel);
  }

  const float T1 = (F_n - f_s) * oneOverAccel,
              T3 = (F_n - f_e) * oneOverAccel;

  N1 = CEIL(T1 * (FTM_FS));         // Accel datapoints based on Hz frequency // 加速时间要产生多少点轨迹，向上取整
  N2 = CEIL(T2 * (FTM_FS));         // Coast //
  N3 = CEIL(T3 * (FTM_FS));         // Decel

  const float T1_P = N1 * (FTM_TS), // (s) Accel datapoints x timestep resolution，将轨迹点数转为时间，此时是单点的整数倍
              T2_P = N2 * (FTM_TS), // (s) Coast
              T3_P = N3 * (FTM_TS); // (s) Decel

  /**
   * Calculate the reachable feedrate at the end of the accel phase.
   *  totalLength is the total distance to travel in mm
   *  f_s        : (mm/s) Starting feedrate
   *  f_e        : (mm/s) Ending feedrate
   *  T1_P       : (sec) Time spent accelerating
   *  T2_P       : (sec) Time spent coasting
   *  T3_P       : (sec) Time spent decelerating
   *  f_s * T1_P : (mm) Distance traveled during the accel phase
   *  f_e * T3_P : (mm) Distance traveled during the decel phase
   */
  F_P = (2.0f * totalLength - f_s * T1_P - f_e * T3_P) / (T1_P + 2.0f * T2_P + T3_P); // (mm/s) Feedrate at the end of the accel phase

  // Calculate the acceleration and deceleration rates
  accel_P = N1 ? ((F_P - f_s) / T1_P) : 0.0f;

  decel_P = (f_e - F_P) / T3_P;

  // Calculate the distance traveled during the accel phase
  s_1e = f_s * T1_P + 0.5f * accel_P * sq(T1_P);

  // Calculate the distance traveled during the decel phase 这里应该是开始减速的位置点
  s_2e = s_1e + F_P * T2_P; // 加速位移+巡航位移

  // Accel + Coasting + Decel datapoints
  max_intervals = N1 + N2 + N3;

  endPosn_prevBlock += moveDist; // 累计位置，这里需要注意float类型长度了
  // LOG_I("moveDist: X:%f, Y:%f, Z:%f, I:%f, E:%f\n", moveDist.x, moveDist.y, moveDist.z, moveDist.i, moveDist.e);
  // LOG_I("startPos: X:%f, Y:%f, Z:%f, I:%f, E:%f\n", startPosn.x, startPosn.y, startPosn.z, startPosn.i, startPosn.e);
  // LOG_I("endPosn: X:%f, Y:%f, Z:%f, I:%f, E:%f\n", endPosn_prevBlock.x, endPosn_prevBlock.y, endPosn_prevBlock.z, endPosn_prevBlock.i, endPosn_prevBlock.e);
  // LOG_I("steps: X:%d, Y:%d, Z:%d, I:%d, E:%d\n\n", steps.x, steps.y, steps.z, steps.i, steps.e);
  // LOG_I("dir bits: 0x%x\n\n", current_block->direction_bits);
}

// Generate data points of the trajectory.
void FTMotion::makeVector() {
  float accel_k = 0.0f;                                 // (mm/s^2) Acceleration K factor
  float tau = (makeVector_idx + 1) * (FTM_TS);          // (s) Time since start of block
  float dist = 0.0f;                                    // (mm) Distance traveled

  if (makeVector_idx < N1) {
    // Acceleration phase
    dist = (f_s * tau) + (0.5f * accel_P * sq(tau));    // (mm) Distance traveled for acceleration phase since start of block //
    accel_k = accel_P;                                  // (mm/s^2) Acceleration K factor from Accel phase
  }
  else if (makeVector_idx < (N1 + N2)) {
    // Coasting phase
    dist = s_1e + F_P * (tau - N1 * (FTM_TS));          // (mm) Distance traveled for coasting phase since start of block
    //accel_k = 0.0f;
  }
  else {
    // Deceleration phase
    tau -= (N1 + N2) * (FTM_TS);                        // (s) Time since start of decel phase
    dist = s_2e + F_P * tau + 0.5f * decel_P * sq(tau); // (mm) Distance traveled for deceleration phase since start of block
    accel_k = decel_P;                                  // (mm/s^2) Acceleration K factor from Decel phase
  }

  LOGICAL_AXIS_CODE(
    traj.e[makeVector_batchIdx] = startPosn.e + ratio.e * dist,
    traj.x[makeVector_batchIdx] = startPosn.x + ratio.x * dist,
    traj.y[makeVector_batchIdx] = startPosn.y + ratio.y * dist,
    traj.z[makeVector_batchIdx] = startPosn.z + ratio.z * dist,
    traj.i[makeVector_batchIdx] = startPosn.i + ratio.i * dist,
    traj.j[makeVector_batchIdx] = startPosn.j + ratio.j * dist,
    traj.k[makeVector_batchIdx] = startPosn.k + ratio.k * dist,
    traj.u[makeVector_batchIdx] = startPosn.u + ratio.u * dist,
    traj.v[makeVector_batchIdx] = startPosn.v + ratio.v * dist,
    traj.w[makeVector_batchIdx] = startPosn.w + ratio.w * dist
  );

  // LOG_I("traj: X:%f, Y:%f, Z:%f, I:%f, E:%f\n", traj.x[makeVector_batchIdx], traj.y[makeVector_batchIdx],
  //   traj.z[makeVector_batchIdx], traj.i[makeVector_batchIdx], traj.e[makeVector_batchIdx]);
  // LOG_I("trajMod: X:%f, Y:%f, Z:%f, I:%f, E:%f\n", trajMod.x, trajMod.y, trajMod.z, trajMod.i, trajMod.e);

  #if HAS_EXTRUDERS
    if (cfg.linearAdvEna) {
      float dedt_adj = (traj.e[makeVector_batchIdx] - e_raw_z1) * (FTM_FS);
      if (ratio.e > 0.0f) dedt_adj += accel_k * cfg.linearAdvK;

      e_raw_z1 = traj.e[makeVector_batchIdx];
      e_advanced_z1 += dedt_adj * (FTM_TS);
      traj.e[makeVector_batchIdx] = e_advanced_z1;
    }
  #endif

  // Update shaping parameters if needed.

  switch (cfg.dynFreqMode) {

    #if HAS_DYNAMIC_FREQ_MM
      case dynFreqMode_Z_BASED:
        if (traj.z[makeVector_batchIdx] != 0.0f) { // Only update if Z changed.
                 const float xf = cfg.baseFreq[X_AXIS] + cfg.dynFreqK[X_AXIS] * traj.z[makeVector_batchIdx]
          OPTARG(HAS_Y_AXIS, yf = cfg.baseFreq[Y_AXIS] + cfg.dynFreqK[Y_AXIS] * traj.z[makeVector_batchIdx]);
          updateShapingN(_MAX(xf, FTM_MIN_SHAPE_FREQ) OPTARG(HAS_Y_AXIS, _MAX(yf, FTM_MIN_SHAPE_FREQ)));
        }
        break;
    #endif

    #if HAS_DYNAMIC_FREQ_G
      case dynFreqMode_MASS_BASED:
        // Update constantly. The optimization done for Z value makes
        // less sense for E, as E is expected to constantly change.
        updateShapingN(      cfg.baseFreq[X_AXIS] + cfg.dynFreqK[X_AXIS] * traj.e[makeVector_batchIdx]
          OPTARG(HAS_Y_AXIS, cfg.baseFreq[Y_AXIS] + cfg.dynFreqK[Y_AXIS] * traj.e[makeVector_batchIdx]) );
        break;
    #endif

    default: break;
  }

  // Apply shaping if in mode.
  #if HAS_X_AXIS
    if (cfg.modeHasShaper()) {
      shaping.x.d_zi[shaping.zi_idx] = traj.x[makeVector_batchIdx];
      traj.x[makeVector_batchIdx] *= shaping.x.Ai[0];
      #if HAS_Y_AXIS
        shaping.y.d_zi[shaping.zi_idx] = traj.y[makeVector_batchIdx];
        traj.y[makeVector_batchIdx] *= shaping.y.Ai[0];
      #endif
      for (uint32_t i = 1U; i <= shaping.max_i; i++) {
        const uint32_t udiffx = shaping.zi_idx - shaping.x.Ni[i];
        traj.x[makeVector_batchIdx] += shaping.x.Ai[i] * shaping.x.d_zi[shaping.x.Ni[i] > shaping.zi_idx ? (FTM_ZMAX) + udiffx : udiffx];
        #if HAS_Y_AXIS
          const uint32_t udiffy = shaping.zi_idx - shaping.y.Ni[i];
          traj.y[makeVector_batchIdx] += shaping.y.Ai[i] * shaping.y.d_zi[shaping.y.Ni[i] > shaping.zi_idx ? (FTM_ZMAX) + udiffy : udiffy];
        #endif
      }
      if (++shaping.zi_idx == (FTM_ZMAX)) shaping.zi_idx = 0;
    }
  #endif

  // Filled up the queue with regular and shaped steps
  if (++makeVector_batchIdx == FTM_WINDOW_SIZE) {
    makeVector_batchIdx = last_batchIdx; // 目前这个值是0
    batchRdy = true;
  }

  // max_intervals 代表的是这段block的所有轨迹点数
  if (++makeVector_idx == max_intervals) {
    blockProcDn = true;
    blockProcRdy = false;
    makeVector_idx = 0;
  }
}

/**
 * Convert to steps
 * - Commands are written in a bitmask with step and dir as single bits.
 * - Tests for delta are moved outside the loop.
 * - Two functions are used for command computation with an array of function pointers.
 */
static void (*command_set[NUM_AXES TERN0(HAS_EXTRUDERS, +1)])(int32_t&, int32_t&, ft_command_t&, int32_t, int32_t);

static void command_set_pos(int32_t &e, int32_t &s, ft_command_t &b, int32_t bd, int32_t bs) {
  if (e < FTM_CTS_COMPARE_VAL) return;
  s++;
  b |= bs;
  e -= FTM_STEPS_PER_UNIT_TIME;
}

// bit is set indicates direction is negative
static void command_set_neg(int32_t &e, int32_t &s, ft_command_t &b, int32_t bd, int32_t bs) {
  if (e > -(FTM_CTS_COMPARE_VAL)) return;
  s--;
  b |= bd | bs;
  e += FTM_STEPS_PER_UNIT_TIME;
}

// Interpolates single data point to stepper commands.
void FTMotion::convertToSteps(const uint32_t idx) {
  xyze_long_t err_P = { 0 };

  //#define STEPS_ROUNDING
  #if ENABLED(STEPS_ROUNDING)
    #define TOSTEPS(A,B) int32_t(trajMod.A[idx] * planner.settings.axis_steps_per_mm[B] + (trajMod.A[idx] < 0.0f ? -0.5f : 0.5f))
    const xyze_long_t steps_tar = LOGICAL_AXIS_ARRAY(
      TOSTEPS(e, E_AXIS_N(current_block->extruder)), // May be eliminated if guaranteed positive.
      TOSTEPS(x, X_AXIS), TOSTEPS(y, Y_AXIS), TOSTEPS(z, Z_AXIS),
      TOSTEPS(i, I_AXIS), TOSTEPS(j, J_AXIS), TOSTEPS(k, K_AXIS),
      TOSTEPS(u, U_AXIS), TOSTEPS(v, V_AXIS), TOSTEPS(w, W_AXIS)
    );
    xyze_long_t delta = steps_tar - steps;
  #else
    #define TOSTEPS(A,B) int32_t(trajMod.A[idx] * planner.settings.axis_steps_per_mm[B]) - steps.A
    xyze_long_t delta = LOGICAL_AXIS_ARRAY(
      TOSTEPS(e, E_AXIS_N(current_block->extruder)),
      TOSTEPS(x, X_AXIS), TOSTEPS(y, Y_AXIS), TOSTEPS(z, Z_AXIS),
      TOSTEPS(i, I_AXIS), TOSTEPS(j, J_AXIS), TOSTEPS(k, K_AXIS),
      TOSTEPS(u, U_AXIS), TOSTEPS(v, V_AXIS), TOSTEPS(w, W_AXIS)
    );
  #endif
  // LOG_I("trajMod: X:%f, Y:%f, Z:%f, I:%f, E:%f\n", trajMod.x[idx], trajMod.y[idx],
  // trajMod.z[idx], trajMod.i[idx], trajMod.e[idx]);
  // if (global_inited) {
  //   LOG_I("steps: X:%d, Y:%d, Z:%d, I:%d, E:%d\n", steps.x, steps.y, steps.z, steps.i, steps.e);
  //   LOG_I("delta: X:%d, Y:%d, Z:%d, I:%d, E:%d\n", delta.x, delta.y, delta.z, delta.i, delta.e);
  // }

  LOGICAL_AXIS_CODE(
    command_set[E_AXIS_N(current_block->extruder)] = delta.e >= 0 ?  command_set_pos : command_set_neg,
    command_set[X_AXIS] = delta.x >= 0 ?  command_set_pos : command_set_neg,
    command_set[Y_AXIS] = delta.y >= 0 ?  command_set_pos : command_set_neg,
    command_set[Z_AXIS] = delta.z >= 0 ?  command_set_pos : command_set_neg,
    command_set[I_AXIS] = delta.i >= 0 ?  command_set_pos : command_set_neg,
    command_set[J_AXIS] = delta.j >= 0 ?  command_set_pos : command_set_neg,
    command_set[K_AXIS] = delta.k >= 0 ?  command_set_pos : command_set_neg,
    command_set[U_AXIS] = delta.u >= 0 ?  command_set_pos : command_set_neg,
    command_set[V_AXIS] = delta.v >= 0 ?  command_set_pos : command_set_neg,
    command_set[W_AXIS] = delta.w >= 0 ?  command_set_pos : command_set_neg
  );

  for (uint32_t i = 0U; i < (FTM_STEPS_PER_UNIT_TIME); i++) {

    // Init all step/dir bits to 0 (defaulting to reverse/negative motion)
    stepperCmdBuff[stepperCmdBuff_produceIdx] = 0;

    err_P += delta;

    // Set up step/dir bits for all axes
    LOGICAL_AXIS_CODE(
      command_set[E_AXIS_N(current_block->extruder)](err_P.e, steps.e, stepperCmdBuff[stepperCmdBuff_produceIdx], _BV(FT_BIT_DIR_E), _BV(FT_BIT_STEP_E)),
      command_set[X_AXIS](err_P.x, steps.x, stepperCmdBuff[stepperCmdBuff_produceIdx], _BV(FT_BIT_DIR_X), _BV(FT_BIT_STEP_X)),
      command_set[Y_AXIS](err_P.y, steps.y, stepperCmdBuff[stepperCmdBuff_produceIdx], _BV(FT_BIT_DIR_Y), _BV(FT_BIT_STEP_Y)),
      command_set[Z_AXIS](err_P.z, steps.z, stepperCmdBuff[stepperCmdBuff_produceIdx], _BV(FT_BIT_DIR_Z), _BV(FT_BIT_STEP_Z)),
      command_set[I_AXIS](err_P.i, steps.i, stepperCmdBuff[stepperCmdBuff_produceIdx], _BV(FT_BIT_DIR_I), _BV(FT_BIT_STEP_I)),
      command_set[J_AXIS](err_P.j, steps.j, stepperCmdBuff[stepperCmdBuff_produceIdx], _BV(FT_BIT_DIR_J), _BV(FT_BIT_STEP_J)),
      command_set[K_AXIS](err_P.k, steps.k, stepperCmdBuff[stepperCmdBuff_produceIdx], _BV(FT_BIT_DIR_K), _BV(FT_BIT_STEP_K)),
      command_set[U_AXIS](err_P.u, steps.u, stepperCmdBuff[stepperCmdBuff_produceIdx], _BV(FT_BIT_DIR_U), _BV(FT_BIT_STEP_U)),
      command_set[V_AXIS](err_P.v, steps.v, stepperCmdBuff[stepperCmdBuff_produceIdx], _BV(FT_BIT_DIR_V), _BV(FT_BIT_STEP_V)),
      command_set[W_AXIS](err_P.w, steps.w, stepperCmdBuff[stepperCmdBuff_produceIdx], _BV(FT_BIT_DIR_W), _BV(FT_BIT_STEP_W)),
    );

    if (++stepperCmdBuff_produceIdx == (FTM_STEPPERCMD_BUFF_SIZE))
      stepperCmdBuff_produceIdx = 0;

  } // FTM_STEPS_PER_UNIT_TIME loop
}

#endif // FT_MOTION

/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
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

/**
 * DWIN by Creality3D
 */

#include "../../../inc/MarlinConfigPre.h"

#if ENABLED(DWIN_CREALITY_LCD)

#include "dwin.h"

#if ANY(AUTO_BED_LEVELING_BILINEAR, AUTO_BED_LEVELING_LINEAR, AUTO_BED_LEVELING_3POINT) && DISABLED(PROBE_MANUALLY)
  #define HAS_ONESTEP_LEVELING 1
#endif

#if ANY(BABYSTEPPING, HAS_BED_PROBE, HAS_WORKSPACE_OFFSET)
  #define HAS_ZOFFSET_ITEM 1
#endif

#if !HAS_BED_PROBE && ENABLED(BABYSTEPPING)
  #define JUST_BABYSTEP 1
#endif

#include <WString.h>
#include <stdio.h>
#include <string.h>

#include "../../fontutils.h"
#include "../../marlinui.h"

#include "../../../sd/cardreader.h"

#include "../../../MarlinCore.h"
#include "../../../core/serial.h"
#include "../../../core/macros.h"
#include "../../../gcode/queue.h"

#include "../../../module/temperature.h"
#include "../../../module/printcounter.h"
#include "../../../module/motion.h"
#include "../../../module/planner.h"
#include "../../../libs/duration_t.h"

#if ENABLED(EEPROM_SETTINGS)
  #include "../../../module/settings.h"
#endif

#if ENABLED(HOST_ACTION_COMMANDS)
  #include "../../../feature/host_actions.h"
#endif

#if HAS_ONESTEP_LEVELING
  #include "../../../feature/bedlevel/bedlevel.h"
#endif

#if HAS_BED_PROBE
  #include "../../../module/probe.h"
#endif

#if EITHER(BABYSTEP_ZPROBE_OFFSET, JUST_BABYSTEP)
  #include "../../../feature/babystep.h"
#endif

#if ENABLED(POWER_LOSS_RECOVERY)
  #include "../../../feature/powerloss.h"
#endif

#ifndef MACHINE_SIZE
  #define MACHINE_SIZE STRINGIFY(X_BED_SIZE) "x" STRINGIFY(Y_BED_SIZE) "x" STRINGIFY(Z_MAX_POS)
#endif
#ifndef CORP_WEBSITE_C
  #define CORP_WEBSITE_C "www.cxsw3d.com"
#endif
#ifndef CORP_WEBSITE_E
  #define CORP_WEBSITE_E "www.creality.com"
#endif

#define PAUSE_HEAT

#define USE_STRING_HEADINGS

#define DWIN_FONT_MENU font8x16
#define DWIN_FONT_STAT font10x20
#define DWIN_FONT_HEAD font10x20

#define MENU_CHAR_LIMIT  24
#define STATUS_Y 360

// Fan speed limit
#define FANON           255
#define FANOFF          0

// Print speed limit
#define MAX_PRINT_SPEED   999
#define MIN_PRINT_SPEED   10

// Temp limits
#if HAS_HOTEND
  #define MAX_E_TEMP    (HEATER_0_MAXTEMP - (HOTEND_OVERSHOOT))
  #define MIN_E_TEMP    HEATER_0_MINTEMP
#endif

#if HAS_HEATED_BED
  #define MIN_BED_TEMP  BED_MINTEMP
#endif

// Feedspeed limit (max feedspeed = DEFAULT_MAX_FEEDRATE * 2)
#define MIN_MAXFEEDSPEED      1
#define MIN_MAXACCELERATION   1
#define MIN_MAXJERK           0.1
#define MIN_STEP              1

#define FEEDRATE_E      (60)

// Mininum unit (0.1) : multiple (10)
#define MINUNITMULT     10

#define ENCODER_WAIT    20
#define DWIN_SCROLL_UPDATE_INTERVAL 2000
#define DWIN_REMAIN_TIME_UPDATE_INTERVAL 20000

constexpr uint16_t TROWS = 6, MROWS = TROWS - 1,        // Total rows, and other-than-Back
                   TITLE_HEIGHT = 30,                   // Title bar height
                   MLINE = 53,                          // Menu line height
                   LBLX = 60,                           // Menu item label X
                   MENU_CHR_W = 8, STAT_CHR_W = 10;

#define MBASE(L) (49 + MLINE * (L))

#define BABY_Z_VAR TERN(HAS_BED_PROBE, probe.offset.z, dwin_zoffset)

/* Value Init */
HMI_value_t HMI_ValueStruct;
HMI_Flag_t HMI_flag{0};

millis_t dwin_heat_time = 0;

//custom variables
float zOffTrack = 0.0, fPosition = 0.0, noMoreDiff = 0.0, noLessDiff = 0.0, UBLYPOS = 0, UBLXPOS = 0, UBLZPOS = 0, oldZ = 0;
int Tool = 0, trackY= 0, trackX = 0, mat = 9, tapFlag = 0,beenHomed = 0, holdPrevious = 0, mainFlag = 0, ExtPrint_flag = 0, levelM = 0, even = -1, fillamentstep = 0;;


int checkkey = 0, last_checkkey = 0;

//conditional variable
#if HAS_LEVELING
  #ifdef BLTOUCH
    int numLines = 7;
  #else
    int numLines = 6;
  #endif
#else
  int numLines = 5;
#endif

typedef struct {
  uint8_t now, last;
  void set(uint8_t v) { now = last = v; }
  void reset() { set(0); }
  bool changed() { bool c = (now != last); if (c) last = now; return c; }
  bool dec() { if (now) now--; return changed(); }
  bool inc(uint8_t v) { if (now < (v - 1)) now++; else now = (v - 1); return changed(); }
} select_t;

select_t select_page{0}, select_file{0}, select_print{0}, select_prepare{0}
         , select_control{0}, select_axis{0}, select_temp{0}, select_motion{0}, select_tune{0}
         , select_PLA{0}, select_ABS{0}
         , select_speed{0}
         , select_acc{0}
         , select_jerk{0}
         , select_step{0}
         , select_leveling{0}
		 , select_zbox{0}
		 , select_pheat{0}
		 , select_bltm{0}
		 , select_homeMenu{0}
		 , select_UBLMenu{0}
         ;

uint8_t index_file     = MROWS,
        index_prepare  = MROWS,
        index_control  = MROWS,
        index_leveling = MROWS,
        index_tune     = MROWS;

bool dwin_abort_flag = false; // Flag to reset feedrate, return to Home

constexpr float default_max_feedrate[]        = DEFAULT_MAX_FEEDRATE;
constexpr float default_max_acceleration[]    = DEFAULT_MAX_ACCELERATION;

#if HAS_CLASSIC_JERK
  constexpr float default_max_jerk[]          = { DEFAULT_XJERK, DEFAULT_YJERK, DEFAULT_ZJERK, DEFAULT_EJERK };
#endif

uint8_t Percentrecord = 0;
uint16_t last_Printtime = 0, remain_time = 0;
float last_temp_hotend_target = 0, last_temp_bed_target = 0;
float last_temp_hotend_current = 0, last_temp_bed_current = 0;
uint8_t last_fan_speed = 0;
uint16_t last_speed = 0;
float last_X_scale = 0;
float last_Y_scale = 0;
float last_Z_scale = 0;
float last_probe_zoffset = 0;
bool DWIN_lcd_sd_status = 0;
bool pause_action_flag = 0;
int temphot = 0, tempbed = 0;
float zprobe_zoffset = 0;
float last_zoffset = 0;



#define DWIN_LANGUAGE_EEPROM_ADDRESS 0x01   // Between 0x01 and 0x63 (EEPROM_OFFSET-1)
                                            // BL24CXX::check() uses 0x00

void lcd_select_language(void) {
  BL24CXX::read(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language_flag, sizeof(HMI_flag.language_flag));
  if (HMI_flag.language_flag)
    DWIN_JPG_CacheTo1(Language_Chinese);
  else
    DWIN_JPG_CacheTo1(Language_English);
}

void set_english_to_eeprom(void) {
  HMI_flag.language_flag = 0;
  DWIN_JPG_CacheTo1(Language_English);
  BL24CXX::write(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language_flag, sizeof(HMI_flag.language_flag));
}
void set_chinese_to_eeprom(void) {
  HMI_flag.language_flag = 1;
  DWIN_JPG_CacheTo1(Language_Chinese);
  BL24CXX::write(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language_flag, sizeof(HMI_flag.language_flag));
}

void DWIN_Draw_Signed_Float(uint8_t size, uint16_t bColor, uint8_t iNum, uint8_t fNum, uint16_t x, uint16_t y, long value) {
  if (value < 0) {
    DWIN_Draw_String(false, true, size, White, bColor, x - 6, y, (char*)"-");
    DWIN_Draw_FloatValue(true, true, 0, size, White, bColor, iNum, fNum, x, y, -value);
  }
  else {
    DWIN_Draw_String(false, true, size, White, bColor, x - 6, y, (char*)" ");
    DWIN_Draw_FloatValue(true, true, 0, size, White, bColor, iNum, fNum, x, y, value);
  }
}

void ICON_Print() {
  if (select_page.now == 0) {
    DWIN_ICON_Show(ICON, ICON_Print_1, 17, 130);
	DWIN_Draw_Rectangle(0, White,  16,  129, 127, 230);
    if (HMI_flag.language_flag)
      DWIN_Frame_AreaCopy(1, 1, 447, 271 - 243, 479 - 19, 58, 201);
    else
      DWIN_Frame_AreaCopy(1, 1, 451, 31, 463, 57, 201);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Print_0, 17, 130);
	DWIN_Draw_Rectangle(0, Background_black,  16,  129, 127, 230);
    if (HMI_flag.language_flag)
      DWIN_Frame_AreaCopy(1, 1, 405, 271 - 243, 420, 58, 201);
    else
      DWIN_Frame_AreaCopy(1, 1, 423, 31, 435, 57, 201);
  }
}

void ICON_Prepare() {
  if (select_page.now == 1) {
    DWIN_ICON_Show(ICON, ICON_Prepare_1, 145, 130);
	DWIN_Draw_Rectangle(0, White,  144,  129, 255, 230);
    if (HMI_flag.language_flag)
      DWIN_Frame_AreaCopy(1, 31, 447, 271 - 213, 479 - 19, 186, 201);
    else
      DWIN_Frame_AreaCopy(1, 33, 451, 82, 466, 175, 201);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Prepare_0, 145, 130);
	DWIN_Draw_Rectangle(0, Background_black,  144,  129, 255, 230);
    if (HMI_flag.language_flag)
      DWIN_Frame_AreaCopy(1, 31, 405, 271 - 213, 420, 186, 201);
    else
      DWIN_Frame_AreaCopy(1, 33, 423, 82, 438, 175, 201);
  }
}

void ICON_Control() {
  if (select_page.now == 2) {
    DWIN_ICON_Show(ICON, ICON_Control_1, 17, 246);
	DWIN_Draw_Rectangle(0, White,  16,  245, 127, 346);
    if (HMI_flag.language_flag)
      DWIN_Frame_AreaCopy(1, 61, 447, 271 - 183, 479 - 19, 58, 318);
    else
      DWIN_Frame_AreaCopy(1, 85, 451, 132, 463, 48, 318);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Control_0, 17, 246);
	DWIN_Draw_Rectangle(0, Background_black,  16,  245, 127, 346);
    if (HMI_flag.language_flag)
      DWIN_Frame_AreaCopy(1, 61, 405, 271 - 183, 420, 58, 318);
    else
      DWIN_Frame_AreaCopy(1, 85, 423, 132, 434, 48, 318);
  }
}

void ICON_StartInfo(bool show) {
  if (show) {
    DWIN_ICON_Show(ICON, ICON_Info_1, 145, 246);
    DWIN_Draw_Rectangle(0, Color_White, 145, 246, 254, 345);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 91, 447, 118, 460, 186, 318);
    else
      DWIN_Frame_AreaCopy(1, 132, 451, 159, 466, 186, 318);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Info_0, 145, 246);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 91, 405, 118, 420, 186, 318);
    else
      DWIN_Frame_AreaCopy(1, 132, 423, 159, 435, 186, 318);
  }
}

void ICON_Leveling(bool show) {
  if (show) {
    DWIN_ICON_Show(ICON, ICON_Leveling_1, 145, 246);
    DWIN_Draw_Rectangle(0, Color_White, 145, 246, 254, 345);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 211, 447, 238, 460, 186, 318);
    else
      DWIN_Frame_AreaCopy(1, 84, 437, 120,  449, 182, 318);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Leveling_0, 145, 246);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 211, 405, 238, 420, 186, 318);
    else
      DWIN_Frame_AreaCopy(1, 84, 465, 120, 478, 182, 318);
  }
}

void ICON_Tune() {
  if (select_print.now == 0) {
    DWIN_ICON_Show(ICON, ICON_Setup_1, 8, 252);
    DWIN_Draw_Rectangle(0, Color_White, 8, 252, 87, 351);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 121, 447, 148, 458, 34, 325);
    else
      DWIN_Frame_AreaCopy(1,   0, 466,  34, 476, 31, 325);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Setup_0, 8, 252);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 121, 405, 148, 420, 34, 325);
    else
      DWIN_Frame_AreaCopy(1,   0, 438,  32, 448, 31, 325);
  }
}

void ICON_Pause() {
  if (select_print.now == 1) {
    DWIN_ICON_Show(ICON, ICON_Pause_1, 96, 252);
    DWIN_Draw_Rectangle(0, Color_White, 96, 252, 175, 351);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 181, 447, 208, 459, 124, 325);
    else
      DWIN_Frame_AreaCopy(1, 177, 451, 216, 462, 116, 325);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Pause_0, 96, 252);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 181, 405, 208, 420, 124, 325);
    else
      DWIN_Frame_AreaCopy(1, 177, 423, 215, 433, 116, 325);
  }
}

void ICON_Continue() {
  if (select_print.now == 1) {
    DWIN_ICON_Show(ICON, ICON_Continue_1, 96, 252);
    DWIN_Draw_Rectangle(0, Color_White, 96, 252, 175, 351);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 1, 447, 28, 460, 124, 325);
    else
      DWIN_Frame_AreaCopy(1, 1, 452, 32, 464, 121, 325);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Continue_0, 96, 252);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 1, 405, 28, 420, 124, 325);
    else
      DWIN_Frame_AreaCopy(1, 1, 424, 31, 434, 121, 325);
  }
}

void ICON_Stop() {
  if (select_print.now == 2) {
    DWIN_ICON_Show(ICON, ICON_Stop_1, 184, 252);
    DWIN_Draw_Rectangle(0, Color_White, 184, 252, 263, 351);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 151, 447, 178, 459, 210, 325);
    else
      DWIN_Frame_AreaCopy(1, 218, 452, 249, 466, 209, 325);
  }
  else {
    DWIN_ICON_Show(ICON, ICON_Stop_0, 184, 252);
    if (HMI_IsChinese())
      DWIN_Frame_AreaCopy(1, 151, 405, 178, 420, 210, 325);
    else
      DWIN_Frame_AreaCopy(1, 218, 423, 247, 436, 209, 325);
  }
}

inline void Clear_Title_Bar() {
  DWIN_Draw_Rectangle(1, Color_Bg_Blue, 0, 0, DWIN_WIDTH, 30);
}

inline void Draw_Title(const char * const title) {
  DWIN_Draw_String(false, false, DWIN_FONT_HEAD, Color_White, Color_Bg_Blue, 14, 4, (char*)title);
}

inline void Draw_Title(const __FlashStringHelper * title) {
  DWIN_Draw_String(false, false, DWIN_FONT_HEAD, Color_White, Color_Bg_Blue, 14, 4, (char*)title);
}

inline void Clear_Menu_Area() {
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 31, DWIN_WIDTH, STATUS_Y);
}

inline void Clear_Main_Window() {
  Clear_Title_Bar();
  Clear_Menu_Area();
}

inline void Clear_Popup_Area() {
  Clear_Title_Bar();
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 31, DWIN_WIDTH, DWIN_HEIGHT);
}

void Draw_Popup_Bkgd_105() {
  DWIN_Draw_Rectangle(1, Color_Bg_Window, 14, 105, 258, 374);
}

inline void Draw_More_Icon(const uint8_t line) {
  DWIN_ICON_Show(ICON, ICON_More, 226, MBASE(line) - 3);
}

inline void Draw_Menu_Cursor(const uint8_t line) {
  // DWIN_ICON_Show(ICON,ICON_Rectangle, 0, MBASE(line) - 18);
  DWIN_Draw_Rectangle(1, Rectangle_Color, 0, MBASE(line) - 18, 14, MBASE(line + 1) - 20);
}

inline void Erase_Menu_Cursor(const uint8_t line) {
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, MBASE(line) - 18, 14, MBASE(line + 1) - 20);
}

inline void Move_Highlight(const int16_t from, const uint16_t newline) {
  Erase_Menu_Cursor(newline - from);
  Draw_Menu_Cursor(newline);
}

inline void Add_Menu_Line() {
  Move_Highlight(1, MROWS);
  DWIN_Draw_Line(Line_Color, 16, MBASE(MROWS + 1) - 20, 256, MBASE(MROWS + 1) - 19);
}

inline void Scroll_Menu(const uint8_t dir) {
  DWIN_Frame_AreaMove(1, dir, MLINE, Color_Bg_Black, 0, 31, DWIN_WIDTH, 349);
  switch (dir) {
    case DWIN_SCROLL_DOWN: Move_Highlight(-1, 0); break;
    case DWIN_SCROLL_UP:   Add_Menu_Line(); break;
  }
}

inline uint16_t nr_sd_menu_items() {
  return card.get_num_Files() + !card.flag.workDirIsRoot;
}

inline void Draw_Menu_Icon(const uint8_t line, const uint8_t icon) {
  DWIN_ICON_Show(ICON, icon, 26, MBASE(line) - 3);
}

inline void Erase_Menu_Text(const uint8_t line) {
  DWIN_Draw_Rectangle(1, Color_Bg_Black, LBLX, MBASE(line) - 14, 271, MBASE(line) + 28);
}

inline void Draw_Menu_Line(const uint8_t line, const uint8_t icon=0, const char * const label=nullptr) {
  if (label) DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(line) - 1, (char*)label);
  if (icon) Draw_Menu_Icon(line, icon);
  DWIN_Draw_Line(Line_Color, 16, MBASE(line) + 33, 256, MBASE(line) + 34);
}

// The "Back" label is always on the first line
inline void Draw_Back_Label() {
  if (HMI_IsChinese())
    DWIN_Frame_AreaCopy(1, 129, 72, 156, 84, LBLX, MBASE(0));
  else
    DWIN_Frame_AreaCopy(1, 226, 179, 256, 189, LBLX, MBASE(0));
}

// Draw "Back" line at the top
inline void Draw_Back_First(const bool is_sel=true) {
  Draw_Menu_Line(0, ICON_Back);
  Draw_Back_Label();
  if (is_sel) Draw_Menu_Cursor(0);
}

inline bool Apply_Encoder(const ENCODER_DiffState &encoder_diffState, auto &valref) {
  if (encoder_diffState == ENCODER_DIFF_CW)
    valref += EncoderRate.encoderMoveValue;
  else if (encoder_diffState == ENCODER_DIFF_CCW)
    valref -= EncoderRate.encoderMoveValue;
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
    return true;
  return false;
}

//
// Draw Menus
//

#define MOTION_CASE_RATE   1
#define MOTION_CASE_ACCEL  2
#define MOTION_CASE_JERK   (MOTION_CASE_ACCEL + ENABLED(HAS_CLASSIC_JERK))
#define MOTION_CASE_STEPS  (MOTION_CASE_JERK + 1)
#define MOTION_CASE_TOTAL  MOTION_CASE_STEPS

#define PREPARE_CASE_MOVE  1
#define PREPARE_CASE_DISA  2
#define PREPARE_CASE_HOME  3
#define PREPARE_CASE_ZOFF (PREPARE_CASE_HOME + ENABLED(HAS_ZOFFSET_ITEM))
#define PREPARE_CASE_PLA  (PREPARE_CASE_ZOFF + ENABLED(HAS_HOTEND))
#define PREPARE_CASE_ABS  (PREPARE_CASE_PLA + ENABLED(HAS_HOTEND))
#define PREPARE_CASE_COOL (PREPARE_CASE_ABS + EITHER(HAS_HOTEND, HAS_HEATED_BED))
#define PREPARE_CASE_LANG (PREPARE_CASE_COOL + 1)
#define PREPARE_CASE_TOTAL PREPARE_CASE_LANG

#define CONTROL_CASE_TEMP 1
#define CONTROL_CASE_MOVE  (CONTROL_CASE_TEMP + 1)
#define CONTROL_CASE_SAVE  (CONTROL_CASE_MOVE + ENABLED(EEPROM_SETTINGS))
#define CONTROL_CASE_LOAD  (CONTROL_CASE_SAVE + ENABLED(EEPROM_SETTINGS))
#define CONTROL_CASE_RESET (CONTROL_CASE_LOAD + ENABLED(EEPROM_SETTINGS))
#define CONTROL_CASE_INFO  (CONTROL_CASE_RESET + 1)
#define CONTROL_CASE_TOTAL CONTROL_CASE_INFO

#define TUNE_CASE_SPEED 1
#define TUNE_CASE_TEMP (TUNE_CASE_SPEED + ENABLED(HAS_HOTEND))
#define TUNE_CASE_BED  (TUNE_CASE_TEMP + ENABLED(HAS_HEATED_BED))
#define TUNE_CASE_FAN  (TUNE_CASE_BED + ENABLED(HAS_FAN))
#define TUNE_CASE_ZOFF (TUNE_CASE_FAN + ENABLED(HAS_ZOFFSET_ITEM))
#define TUNE_CASE_TOTAL TUNE_CASE_ZOFF

#define TEMP_CASE_TEMP (0 + ENABLED(HAS_HOTEND))
#define TEMP_CASE_BED  (TEMP_CASE_TEMP + ENABLED(HAS_HEATED_BED))
#define TEMP_CASE_FAN  (TEMP_CASE_BED + ENABLED(HAS_FAN))
#define TEMP_CASE_PLA  (TEMP_CASE_FAN + ENABLED(HAS_HOTEND))
#define TEMP_CASE_ABS  (TEMP_CASE_PLA + ENABLED(HAS_HOTEND))
#define TEMP_CASE_TOTAL TEMP_CASE_ABS

#define PREHEAT_CASE_TEMP (0 + ENABLED(HAS_HOTEND))
#define PREHEAT_CASE_BED  (PREHEAT_CASE_TEMP + ENABLED(HAS_HEATED_BED))
#define PREHEAT_CASE_FAN  (PREHEAT_CASE_BED + ENABLED(HAS_FAN))
#define PREHEAT_CASE_SAVE (PREHEAT_CASE_FAN + ENABLED(EEPROM_SETTINGS))
#define PREHEAT_CASE_TOTAL PREHEAT_CASE_SAVE

//
// Draw Menus
//

inline void draw_move_en(const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 69, 61, 102, 71, LBLX, line); // "Move"
}

inline void DWIN_Frame_TitleCopy(uint8_t id, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) { DWIN_Frame_AreaCopy(id, x1, y1, x2, y2, 14, 8); }

inline void Item_Prepare_Move(const uint8_t row) {
  if (HMI_IsChinese())
    DWIN_Frame_AreaCopy(1, 159, 70, 200, 84, LBLX, MBASE(row));
  else
    draw_move_en(MBASE(row)); // "Move >"
      }
    Draw_Menu_Line(row, ICON_Axis);
    Draw_More_Icon(row);
}

inline void Prepare_Item_Disable(const uint8_t row) {
  if (HMI_flag.language_flag)
    DWIN_Frame_AreaCopy(1, 204, 70, 271 - 12, 479 - 397, LBLX, MBASE(row));
  else{
    DWIN_Frame_AreaCopy(1, 103, 59, 271 - 71, 479 - 405, LBLX, MBASE(row)); // "Disable Stepper"
      }
    Draw_Menu_Line(row, ICON_CloseMotor);
}

inline void Prepare_Item_Home(const uint8_t row) {
  if (HMI_flag.language_flag)
    DWIN_Frame_AreaCopy(1, 0, 89, 271 - 230, 479 - 378, LBLX, MBASE(row));
  else{
	  DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Homing Menu");
      }
	  Draw_Menu_Line(row, ICON_Homing);
	  Draw_More_Icon(row);
  
}

inline void Prepare_Item_Offset(const uint8_t row) {
  if (HMI_flag.language_flag) {
    #if HAS_BED_PROBE
      DWIN_Frame_AreaCopy(1, 174, 164, 271 - 48, 479 - 302, LBLX, MBASE(row));
      show_plus_or_minus(font8x16, Background_black, 2, 2, 202, MBASE(row), BABY_Z_VAR * 100);
    #else
      DWIN_Frame_AreaCopy(1, 43, 89, 271 - 173, 479 - 378, LBLX, MBASE(row));
    #endif
  }
  else {
    #if HAS_BED_PROBE
	  DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Z-Offset Menu");
	  Draw_More_Icon(row);
    #else
      DWIN_Frame_AreaCopy(1, 1, 76, 271 - 165, 479 - 393, LBLX, MBASE(row)); // "..."
    #endif
  }
  Draw_Menu_Line(row, ICON_SetHome);
}

inline void Prepare_Item_PreheatM(const uint8_t row) {
  if (HMI_flag.language_flag) {
    DWIN_Frame_AreaCopy(1, 100, 89, 271 - 93 - 27, 479 - 378, LBLX, MBASE(row));
  }
  else {
  DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Preheat Menu");
  }
  Draw_More_Icon(row);
  Draw_Menu_Line(row, ICON_PLAPreheat);
  
}

inline void Prepare_Item_ManLev(const uint8_t row) {
  DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Manual Leveling");
  Draw_More_Icon(row);
  Draw_Menu_Line(row, ICON_PrintSize);
}

inline void Prepare_Item_Cool(const uint8_t row) {
  if (HMI_flag.language_flag)
    DWIN_Frame_AreaCopy(1, 1, 104, 271 - 215, 479 - 362, LBLX, MBASE(row));
  else{
    DWIN_Frame_AreaCopy(1, 200,  76, 271 - 7, 479 - 393, LBLX, MBASE(row));// "Cooldown"
      }
  Draw_Menu_Line(row, ICON_Cool);
}

inline void Prepare_Item_Lang(const uint8_t row) {
  if (HMI_flag.language_flag) {
    DWIN_Frame_AreaCopy(1, 239, 134, 271 - 5, 479 - 333, LBLX, MBASE(row));
    DWIN_Draw_String(false, false, font8x16, White, Background_black, 226, MBASE(row), (char*)"CN");
  }
  else {
    DWIN_Frame_AreaCopy(1, 0, 194, 271 - 150, 479 - 272, LBLX, MBASE(row)); // "Language selection"
    DWIN_Draw_String(false, false, font8x16, White, Background_black, 226, MBASE(row), (char*)"EN");
  }
  Draw_Menu_Icon(row, ICON_Language);
}

inline void Prepare_Item_FilaChange(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Change Filament");
    Draw_Menu_Line(row, ICON_MoveX + 3);
}

inline void Draw_Prepare_Menu() {
  Clear_Main_Window();

  const int16_t scroll = MROWS - index_prepare; // Scrolled-up lines
  #define PSCROL(L) (scroll + (L))
  #define PVISI(L)  WITHIN(PSCROL(L), 0, MROWS)

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 133, 1, 160, 13);   // "Prepare"
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Prepare"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_TitleCopy(1, 178, 2, 229, 14); // "Prepare"
    #endif
  }

  if (PVISI(0)) Draw_Back_First(select_prepare.now == 0); // < Back
  if (PVISI(1)) Prepare_Item_Move(PSCROL(1));       // Move menu >
  if (PVISI(2)) Prepare_Item_Disable(PSCROL(2));    // disable steppers
  if (PVISI(3)) Prepare_Item_Home(PSCROL(3));       // Homing Menu >
  if (PVISI(4)) Prepare_Item_Offset(PSCROL(4));     // zoffset menu >
  if (PVISI(5)) Prepare_Item_PreheatM(PSCROL(5));   // Preheat menu >
  if (PVISI(6)) Prepare_Item_Cool(PSCROL(6));       // Cooldown
  if (PVISI(7)) Prepare_Item_FilaChange(PSCROL(7)); // filament change
  if (PVISI(8)) Prepare_Item_Lang(PSCROL(8));       // Language CN/EN
  if (PVISI(9)) Prepare_Item_ManLev(PSCROL(9));     // Persistent manual leveling

  if (select_prepare.now) Draw_Menu_Cursor(PSCROL(select_prepare.now));

  LOOP_L_N(i, MROWS) Draw_Menu_Line(i + 1);
}

inline void Prepare_Item_Bed(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Bed Only");
    Draw_Menu_Line(row, ICON_PrintSize);
}

inline void Prepare_Item_PLA(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"PLA");
    Draw_Menu_Line(row, ICON_Temperature);
}

inline void Prepare_Item_ABS(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"ABS");
    Draw_Menu_Line(row, ICON_Temperature);
}

inline void Prepare_Item_Custom1(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"PETG");
    Draw_Menu_Line(row, ICON_Temperature);
}

inline void Prepare_Item_Custom2(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"PETG");
    Draw_Menu_Line(row, ICON_Temperature);
}

inline void Prepare_Item_Custom3(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Custom 2");
    Draw_Menu_Line(row, ICON_Temperature);
}

inline void Draw_Preheat_Menu() {
  Clear_Main_Window();

  const int16_t scroll = MROWS - index_prepare; // Scrolled-up lines
  #define PSCROL(L) (scroll + (L))
  #define PVISI(L)  WITHIN(PSCROL(L), 0, MROWS)

  if (HMI_flag.language_flag) {
    DWIN_Frame_AreaCopy(1, 133, 1, 271 - 111, 479 - 465 - 1, 14, 8); // "Prepare"
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("PreHeat Menu"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_AreaCopy(1, 178, 2, 271 - 42, 479 - 464 - 1, 14, 8); // "Prepare"
    #endif
  }

  if (PVISI(0)) Draw_Back_First(select_pheat.now == 0);	 // < Back
  if (PVISI(1)) Prepare_Item_Bed(PSCROL(1));       	     // preheat BED only
  if (PVISI(2)) Prepare_Item_PLA(PSCROL(2));             // preheat PLA
  if (PVISI(3)) Prepare_Item_ABS(PSCROL(3)); 	         // preheat ABS
  if (PVISI(4)) Prepare_Item_Custom1(PSCROL(4));         // Preheat PETG
  if (PVISI(5)) Prepare_Item_Custom2(PSCROL(5));         // Preheat Custom 2
  if (PVISI(6)) Prepare_Item_Custom3(PSCROL(6));         // Preheat Custom 3

  if (select_pheat.now) Draw_Menu_Cursor(PSCROL(select_pheat.now));

  LOOP_L_N(i, MROWS) Draw_Menu_Line(i + 1);
}

inline void Prepare_Item_TempSetMenu(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Temperature");
    Draw_Menu_Line(row, ICON_SetEndTemp);
	Draw_More_Icon(row);
}

inline void Prepare_Item_MotionSetMenu(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Motion");
    Draw_Menu_Line(row, ICON_Motion);
	Draw_More_Icon(row);
}

inline void Prepare_Item_SaveEeprom(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Store EEPROM settings");
    Draw_Menu_Line(row, ICON_WriteEEPROM);
}

inline void Prepare_Item_ReadEeprom(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Read EEPROM settings");
    Draw_Menu_Line(row, ICON_ReadEEPROM);
}

inline void Prepare_Item_ResetEeprom(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Reset EEPROM settings");
    Draw_Menu_Line(row, ICON_ResumeEEPROM);
}

inline void Prepare_Item_BltouchMenu(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)GET_TEXT_F(MSG_BED_LEVELING));
    Draw_Menu_Line(row, ICON_SetHome);
	Draw_More_Icon(row);
}

inline void Prepare_Item_AltInfoMenu(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Device Info");
    Draw_Menu_Line(row, ICON_Info);
	Draw_More_Icon(row);
}

inline void Draw_Control_Menu() {
  Clear_Main_Window();

  const int16_t scroll = MROWS - index_control; // Scrolled-up lines
  #define PSCROL(L) (scroll + (L))
  #define PVISI(L)  WITHIN(PSCROL(L), 0, MROWS)

  if (HMI_flag.language_flag) {
    DWIN_Frame_AreaCopy(1, 133, 1, 271 - 111, 479 - 465 - 1, 14, 8); // "Prepare"
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Control Menu"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_AreaCopy(1, 178, 2, 271 - 42, 479 - 464 - 1, 14, 8); // "Prepare"
    #endif
  }

  if (PVISI(0)) Draw_Back_First(select_control.now == 0);   // < Back
  if (PVISI(1)) Prepare_Item_TempSetMenu(PSCROL(1));       // temperature >
  if (PVISI(2)) Prepare_Item_MotionSetMenu(PSCROL(2));          // motion >
  if (PVISI(3)) Prepare_Item_SaveEeprom(PSCROL(3));         // store EEPROM
  if (PVISI(4)) Prepare_Item_ReadEeprom(PSCROL(4));      // read EEPROM
  if (PVISI(5)) Prepare_Item_ResetEeprom(PSCROL(5));      // reset EEPROM

  #if HAS_LEVELING
	
	#ifdef BLTOUCH
		if (PVISI(6)) Prepare_Item_BltouchMenu(PSCROL(6));  // bltouch
		if (PVISI(7)) Prepare_Item_AltInfoMenu(PSCROL(7));  // info
	#else
		if (PVISI(6)) Prepare_Item_AltInfoMenu(PSCROL(6));  // info
	#endif
	
  #endif
  
  if (select_control.now) Draw_Menu_Cursor(PSCROL(select_control.now));

  LOOP_L_N(i, MROWS) Draw_Menu_Line(i + 1);
}

//bltouch menu
inline void Draw_BlTouch_Menu() {
  Clear_Main_Window();

    Draw_Title("Bltouch Menu"); // TODO: GET_TEXT_F
	
    DWIN_Draw_String(false, false, font8x16, White, Background_black, LBLX, MBASE(1), (char*)"Alarm Release");
    DWIN_Draw_String(false, false, font8x16, White, Background_black, LBLX, MBASE(2), (char*)"Self Test");
    DWIN_Draw_String(false, false, font8x16, White, Background_black, LBLX, MBASE(3), (char*)"Pin DOWN");
    DWIN_Draw_String(false, false, font8x16, White, Background_black, LBLX, MBASE(4), (char*)"Pin UP");

  Draw_Back_First(select_bltm.now == 0);
  if (select_bltm.now) Draw_Menu_Cursor(select_bltm.now);

  LOOP_L_N(i, MROWS-1) Draw_Menu_Line(i + 1, ICON_SetEndTemp);
}

inline void Draw_Mlevel_Menu() {
  Clear_Main_Window();

	if (levelM == 1){
	  Draw_Title("Manual Leveling Menu"); // TODO: GET_TEXT_F
	}
	else{
      Draw_Title("Manual Mesh Menu"); // TODO: GET_TEXT_F
	}
	
	//back
	DWIN_Draw_String(true, true, font8x16, White, Background_black, 126, 50, (char*)"Back");
	if (levelM != 1){
      DWIN_Draw_String(true, true, font8x16, White, Background_black,  41, 330,  (char*)"Mesh X: "); 				        // Mesh Map Adjustments
      DWIN_Draw_String(true, true, font8x16, White, Background_black,  150, 330, (char*)"Mesh Y: "); 			         	// Mesh Map Adjustments
      DWIN_Draw_String(true, true, font8x16, White, Background_black,  41, 300,  (char*)"Mesh Offset Z: "); 				// Mesh Map Adjustments
	}
	else{
	  DWIN_Draw_String(true, true, font8x16, White, Background_black,  41, 330,  (char*)"  To X: "); 				        // Mesh Map Adjustments
      DWIN_Draw_String(true, true, font8x16, White, Background_black,  150, 330, (char*)"  To Y: "); 			         	// Mesh Map Adjustments	
	}
	DWIN_Draw_Rectangle(0, White,  116,  40, 166,  80);
	
  	even = 0;
	for( int a = 2; a > -1; a -= 1 ) {
	  int y = (a * 76) + 100;
      for( int b = 0; b < 3; b += 1 ) {
        int x = (b * 76) + 60;
		if (levelM == 1){
		  if (even % 2 == 0){
			DWIN_Draw_Rectangle(1, Rectangle_Color,  x,  y,  x + 10, y + 10);
		  }
		  even++;
		}
		else{
		DWIN_Draw_Rectangle(1, Rectangle_Color,  x,  y,  x + 10, y + 10);
	    }
	  }
    }
	even = -1;
}

inline void Draw_Homing_Menu() {
  Clear_Main_Window();

    #ifdef USE_STRING_HEADINGS
      Draw_Title("Move"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_AreaCopy(1, 231, 2, 271 - 6, 479 - 467, 14, 8);
    #endif
    DWIN_Draw_String(true, true, font8x16, White, Background_black, LBLX, MBASE(1), (char*)"Home All");
	DWIN_Draw_String(true, true, font8x16, White, Background_black, LBLX, MBASE(2), (char*)"Home only X");
	DWIN_Draw_String(true, true, font8x16, White, Background_black, LBLX, MBASE(3), (char*)"Home only Y");
	DWIN_Draw_String(true, true, font8x16, White, Background_black, LBLX, MBASE(4), (char*)"Home only Z");

  // Draw icons and lines
  uint8_t i = 0;
  #define _TEMP_ICON(N) do{ ++i; if (CVISI(i)) Draw_Menu_Line(CSCROL(i), ICON_Temperature + (N) - 1); }while(0)

  _TEMP_ICON(CONTROL_CASE_TEMP);
  if (CVISI(i)) Draw_More_Icon(CSCROL(i));

  _TEMP_ICON(CONTROL_CASE_MOVE);
  Draw_More_Icon(CSCROL(i));

  #if ENABLED(EEPROM_SETTINGS)
    _TEMP_ICON(CONTROL_CASE_SAVE);
    _TEMP_ICON(CONTROL_CASE_LOAD);
    _TEMP_ICON(CONTROL_CASE_RESET);
  #endif

  _TEMP_ICON(CONTROL_CASE_INFO);
  if (CVISI(CONTROL_CASE_INFO)) Draw_More_Icon(CSCROL(i));
}

inline void Draw_Tune_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_AreaCopy(1, 73, 2, 100, 13, 14, 9);
    DWIN_Frame_AreaCopy(1, 116, 164, 171, 176, LBLX, MBASE(TUNE_CASE_SPEED));
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 1, 134, 56, 146, LBLX, MBASE(TUNE_CASE_TEMP));
    #endif
    #if HAS_HEATED_BED
      DWIN_Frame_AreaCopy(1, 58, 134, 113, 146, LBLX, MBASE(TUNE_CASE_BED));
    #endif
    #if HAS_FAN
      DWIN_Frame_AreaCopy(1, 115, 134, 170, 146, LBLX, MBASE(TUNE_CASE_FAN));
    #endif
    #if HAS_ZOFFSET_ITEM
      DWIN_Frame_AreaCopy(1, 174, 164, 223, 177, LBLX, MBASE(TUNE_CASE_ZOFF));
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_TUNE));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(TUNE_CASE_SPEED), GET_TEXT_F(MSG_SPEED));
      #if HAS_HOTEND
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(TUNE_CASE_TEMP), GET_TEXT_F(MSG_UBL_SET_TEMP_HOTEND));
      #endif
      #if HAS_HEATED_BED
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(TUNE_CASE_BED), GET_TEXT_F(MSG_UBL_SET_TEMP_BED));
      #endif
      #if HAS_FAN
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(TUNE_CASE_FAN), GET_TEXT_F(MSG_FAN_SPEED));
      #endif
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(TUNE_CASE_ZOFF), GET_TEXT_F(MSG_ZPROBE_ZOFFSET));
    #else
      DWIN_Frame_AreaCopy(1, 94, 2, 126, 12, 14, 9);
      DWIN_Frame_AreaCopy(1, 1, 179, 92, 190, LBLX, MBASE(TUNE_CASE_SPEED));      // Print speed
      #if HAS_HOTEND
        DWIN_Frame_AreaCopy(1, 197, 104, 238, 114, LBLX, MBASE(TUNE_CASE_TEMP));  // Hotend...
        DWIN_Frame_AreaCopy(1, 1, 89, 83, 101, LBLX + 44, MBASE(TUNE_CASE_TEMP)); // ...Temperature
      #endif
      #if HAS_HEATED_BED
        DWIN_Frame_AreaCopy(1, 240, 104, 264, 114, LBLX, MBASE(TUNE_CASE_BED));   // Bed...
        DWIN_Frame_AreaCopy(1, 1, 89, 83, 101, LBLX + 27, MBASE(TUNE_CASE_BED));  // ...Temperature
      #endif
      #if HAS_FAN
        DWIN_Frame_AreaCopy(1, 0, 119, 64, 132, LBLX, MBASE(TUNE_CASE_FAN));      // Fan speed
      #endif
      #if HAS_ZOFFSET_ITEM
        DWIN_Frame_AreaCopy(1, 93, 179, 141, 189, LBLX, MBASE(TUNE_CASE_ZOFF));   // Z-offset
      #endif
    #endif
    DWIN_Frame_AreaCopy(1, 1, 179, 271 - 179, 479 - 287 - 2, LBLX, MBASE(1)); // print speed
    DWIN_Frame_AreaCopy(1, 197, 104, 271 - 33, 479 - 365, LBLX, MBASE(2)); // Hotend...
    DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 41 + 3, MBASE(2)); // ...Temperature
    DWIN_Frame_AreaCopy(1, 240, 104, 271 - 7, 479 - 365, LBLX, MBASE(3)); // Bed...
    DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 3, MBASE(3)); // ...Temperature
    DWIN_Frame_AreaCopy(1, 0, 119, 271 - 207, 479 - 347, LBLX, MBASE(4)); // fan speed
	#if EITHER(HAS_BED_PROBE, BABYSTEPPING)
    DWIN_Frame_AreaCopy(1, 93, 179, 271 - 130, 479 - 290, LBLX, MBASE(5)); // Z-offset
	#endif
  }

  if (ExtPrint_flag != 2){
    Draw_Back_First(select_tune.now == 0);
  }
  if (select_tune.now) Draw_Menu_Cursor(select_tune.now);

  Draw_Menu_Line(TUNE_CASE_SPEED, ICON_Speed);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_SPEED), feedrate_percentage);

  #if HAS_HOTEND
    Draw_Menu_Line(TUNE_CASE_TEMP, ICON_HotendTemp);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_TEMP), thermalManager.temp_hotend[0].target);
  #endif
  #if HAS_HEATED_BED
    Draw_Menu_Line(TUNE_CASE_BED, ICON_BedTemp);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_BED), thermalManager.temp_bed.target);
  #endif
  #if HAS_FAN
    Draw_Menu_Line(TUNE_CASE_FAN, ICON_FanSpeed);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_FAN), thermalManager.fan_speed[0]);
  #endif
  #if HAS_ZOFFSET_ITEM
    Draw_Menu_Line(TUNE_CASE_ZOFF, ICON_Zoffset);
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 2, 2, 202, MBASE(TUNE_CASE_ZOFF), BABY_Z_VAR * 100);
  #endif
}

inline void draw_max_en(const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 245, 119, 269, 129, LBLX, line);   // "Max"
}
inline void draw_max_accel_en(const uint16_t line) {
  draw_max_en(line);
  DWIN_Frame_AreaCopy(1, 1, 135, 79, 145, LBLX + 27, line); // "Acceleration"
}
inline void draw_speed_en(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + inset, line); // "Speed"
}
inline void draw_jerk_en(const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 64, 119, 106, 129, LBLX + 27, line); // "Jerk"
}
inline void draw_steps_per_mm(const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 1, 151, 101, 161, LBLX, line);   // "Steps-per-mm"
}
inline void say_x(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 95, 104, 102, 114, LBLX + inset, line); // "X"
}
inline void say_y(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 104, 104, 110, 114, LBLX + inset, line); // "Y"
}
inline void say_z(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 112, 104, 120, 114, LBLX + inset, line); // "Z"
}
inline void say_e(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 237, 119, 244, 129, LBLX + inset, line); // "E"
}

inline void Draw_Motion_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 1, 16, 28, 28);                                     // "Motion"
    DWIN_Frame_AreaCopy(1, 173, 133, 228, 147, LBLX, MBASE(MOTION_CASE_RATE));  // Max speed
    DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(MOTION_CASE_ACCEL));        // Max...
    DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(MOTION_CASE_ACCEL) + 1); // ...Acceleration
    #if HAS_CLASSIC_JERK
      DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(MOTION_CASE_JERK));        // Max...
      DWIN_Frame_AreaCopy(1, 1, 180, 28, 192, LBLX + 27, MBASE(MOTION_CASE_JERK) + 1);  // ...
      DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 54, MBASE(MOTION_CASE_JERK));   // ...Jerk
    #endif
    DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(MOTION_CASE_STEPS));         // Flow ratio
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_MOTION));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(MOTION_CASE_RATE), F("Feedrate"));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(MOTION_CASE_ACCEL), GET_TEXT_F(MSG_ACCELERATION));
      #if HAS_CLASSIC_JERK
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(MOTION_CASE_JERK), GET_TEXT_F(MSG_JERK));
      #endif
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(MOTION_CASE_STEPS), GET_TEXT_F(MSG_STEPS_PER_MM));
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26);                                        // "Motion"
      draw_max_en(MBASE(MOTION_CASE_RATE)); draw_speed_en(27, MBASE(MOTION_CASE_RATE)); // "Max Speed"
      draw_max_accel_en(MBASE(MOTION_CASE_ACCEL));                                      // "Max Acceleration"
      #if HAS_CLASSIC_JERK
        draw_max_en(MBASE(MOTION_CASE_JERK)); draw_jerk_en(MBASE(MOTION_CASE_JERK));    // "Max Jerk"
      #endif
      draw_steps_per_mm(MBASE(MOTION_CASE_STEPS));                                      // "Steps-per-mm"
    #endif
    draw_max_en(MBASE(1)); draw_speed_en(24 + 3, MBASE(1));               // "Max Speed"
    draw_max_accel_en(MBASE(2));                                          // "Max Acceleration"
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(3), (char*)"Max Jerk Menu");
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(4), (char*)"Steps Menu");
  }

  Draw_Back_First(select_motion.now == 0);
  if (select_motion.now) Draw_Menu_Cursor(select_motion.now);

  uint8_t i = 0;
  #define _MOTION_ICON(N) Draw_Menu_Line(++i, ICON_MaxSpeed + (N) - 1)
  _MOTION_ICON(MOTION_CASE_RATE); Draw_More_Icon(i);
  _MOTION_ICON(MOTION_CASE_ACCEL); Draw_More_Icon(i);
  #if HAS_CLASSIC_JERK
    _MOTION_ICON(MOTION_CASE_JERK); Draw_More_Icon(i);
  #endif
  _MOTION_ICON(MOTION_CASE_STEPS); Draw_More_Icon(i);
}

//
// Draw Popup Windows
//
#if HAS_HOTEND || HAS_HEATED_BED

  void DWIN_Popup_Temperature(const bool toohigh) {
    Clear_Popup_Area();
    Draw_Popup_Bkgd_105();
    if (toohigh) {
      DWIN_ICON_Show(ICON, ICON_TempTooHigh, 102, 165);
      if (HMI_IsChinese()) {
        DWIN_Frame_AreaCopy(1, 103, 371, 237, 386, 52, 285);
        DWIN_Frame_AreaCopy(1, 151, 389, 185, 402, 187, 285);
        DWIN_Frame_AreaCopy(1, 189, 389, 271, 402, 95, 310);
      }
      else {
        DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 36, 300, F("Nozzle or Bed temperature"));
        DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 92, 300, F("is too high"));
      }
    }
    else {
      DWIN_ICON_Show(ICON, ICON_TempTooLow, 102, 165);
      if (HMI_IsChinese()) {
        DWIN_Frame_AreaCopy(1, 103, 371, 270, 386, 52, 285);
        DWIN_Frame_AreaCopy(1, 189, 389, 271, 402, 95, 310);
      }
      else {
        DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 36, 300, F("Nozzle or Bed temperature"));
        DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 92, 300, F("is too low"));
      }
    }
  }

#endif

inline void Draw_Popup_Bkgd_60() {
  DWIN_Draw_Rectangle(1, Color_Bg_Window, 14, 60, 258, 330);
}

void Popup_Window_Save(){
	Clear_Main_Window();
    Draw_Popup_Bkgd_60();
	DWIN_ICON_Show(ICON, ICON_Version, 125, 105);
	DWIN_Draw_String(false, true, font8x16, Font_window, Background_window, 20, 175, (char*)"EEPROM items reset to default");
	DWIN_Draw_String(false, true, font8x16, Font_window, Background_window, 40, 205, (char*)"If you now wish to save");
	DWIN_Draw_String(false, true, font8x16, Font_window, Background_window, 20, 235, (char*)"Select store EEPROM settings");
    DWIN_ICON_Show(ICON, ICON_Confirm_E, 86, 280);
}

void Popup_Window_Filament_step1(){
	Clear_Main_Window();
    Draw_Popup_Bkgd_60();
	DWIN_ICON_Show(ICON, ICON_Version, 125, 105);
	DWIN_Draw_String(false, true, font8x16, Font_window, Background_window, 20, 175, (char*)"Filament is being retracted");
	DWIN_Draw_String(false, true, font8x16, Font_window, Background_window, 20, 205, (char*)"One you insert new filament");
	DWIN_Draw_String(false, true, font8x16, Font_window, Background_window, 20, 235, (char*)"Press Confirm to feed filament");
    DWIN_ICON_Show(ICON, ICON_Confirm_E, 86, 280);
}

void Popup_Window_Filament_step2(){
	Clear_Main_Window();
    Draw_Popup_Bkgd_60();
	DWIN_ICON_Show(ICON, ICON_Version, 125, 105);
	DWIN_Draw_String(false, true, font8x16, Font_window, Background_window, 20, 175, (char*)"Filament is being fed");
	DWIN_Draw_String(false, true, font8x16, Font_window, Background_window, 20, 205, (char*)"Please Wait");
	DWIN_Draw_String(false, true, font8x16, Font_window, Background_window, 20, 235, (char*)"Press Confirm once complete");
    DWIN_ICON_Show(ICON, ICON_Confirm_E, 86, 280);
}

#ifdef AUTO_BED_LEVELING_UBL

inline void Draw_UBL_Level_Menu() {
  Clear_Main_Window();

    Draw_Title("UBL Menu");
    DWIN_Draw_String(true, true, font8x16, White, Background_black, LBLX, MBASE(1), (char*)"Auto Probe Bed");                       // Auto Probe Bed
    DWIN_Draw_String(true, true, font8x16, White, Background_black, LBLX, MBASE(2), (char*)"Fill in Mesh and Save");                // Fill in Mesh and Save
    DWIN_Draw_String(true, true, font8x16, White, Background_black, LBLX, MBASE(3), (char*)"3 Point Level");                        // 3 point Level (must have mesh first)
    DWIN_Draw_String(true, true, font8x16, White, Background_black, LBLX, MBASE(4), (char*)"Activate UBL"); 						// Activate UBL
	DWIN_Draw_String(true, true, font8x16, White, Background_black, LBLX, MBASE(5), (char*)"Mesh Map Adjustments"); 				// Mesh Map Adjustments
	
  

  Draw_Back_First(select_UBLMenu.now == 0);
  if (select_UBLMenu.now) Draw_Menu_Cursor(select_UBLMenu.now);

  LOOP_L_N(i, MROWS) Draw_Menu_Line(i + 1, ICON_MoveX + i);
  
}

inline void Draw_UBL_Level_Map() {
  Clear_Main_Window();
  Draw_Title("UBL Map Adjustments");
  //back
  DWIN_Draw_String(true, true, font8x16, White, Background_black, 126, 50,   (char*)"Back");
  DWIN_Draw_String(true, true, font8x16, White, Background_black,  41, 300,  (char*)"Mesh Offset Z: "); 				// Mesh Map Adjustments
  DWIN_Draw_String(true, true, font8x16, White, Background_black,  41, 330,  (char*)"Mesh X: "); 				// Mesh Map Adjustments
  DWIN_Draw_String(true, true, font8x16, White, Background_black,  150, 330, (char*)"Mesh Y: "); 				// Mesh Map Adjustments
  DWIN_Draw_Rectangle(0, White,  116,  40, 166,  80);
  //create map squares
  	for( int a = 9; a > -1; a -= 1 ) {
	  int y = (a * 20) + 100;
      for( int b = 0; b < 10; b += 1 ) {
        int x = (b * 20) + 41;
		DWIN_Draw_Rectangle(1, Rectangle_Color,  x,  y,  x + 10, y + 10);
	  }
	}
}

#endif

#if HAS_HOTEND

  void Popup_Window_ETempTooLow() {
    Clear_Main_Window();
    Draw_Popup_Bkgd_60();
    DWIN_ICON_Show(ICON, ICON_TempTooLow, 102, 105);
    if (HMI_IsChinese()) {
      DWIN_Frame_AreaCopy(1, 103, 371, 136, 386, 69, 240);
      DWIN_Frame_AreaCopy(1, 170, 371, 270, 386, 102, 240);
      DWIN_ICON_Show(ICON, ICON_Confirm_C, 86, 280);
    }
    else {
      DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 20, 235, F("Nozzle is too cold"));
      DWIN_ICON_Show(ICON, ICON_Confirm_E, 86, 280);
    }
  }

#endif

void Popup_Window_Resume() {
  Clear_Popup_Area();
  Draw_Popup_Bkgd_105();
  if (HMI_IsChinese()) {
    DWIN_Frame_AreaCopy(1, 160, 338, 235, 354, 98, 135);
    DWIN_Frame_AreaCopy(1, 103, 321, 271, 335, 52, 192);
    DWIN_ICON_Show(ICON, ICON_Cancel_C,    26, 307);
    DWIN_ICON_Show(ICON, ICON_Continue_C, 146, 307);
  }
  else {
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 14) / 2, 115, F("Continue Print"));
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 22) / 2, 192, F("It looks like the last"));
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 22) / 2, 212, F("file was interrupted."));
    DWIN_ICON_Show(ICON, ICON_Cancel_E,    26, 307);
    DWIN_ICON_Show(ICON, ICON_Continue_E, 146, 307);
  }
}

void Popup_Window_Home(const bool parking/*=false*/) {
  Clear_Main_Window();
  Draw_Popup_Bkgd_60();
  DWIN_ICON_Show(ICON, ICON_BLTouch, 101, 105);
  if (HMI_IsChinese()) {
    DWIN_Frame_AreaCopy(1, 0, 371, 33, 386, 85, 240);
    DWIN_Frame_AreaCopy(1, 203, 286, 271, 302, 118, 240);
    DWIN_Frame_AreaCopy(1, 0, 389, 150, 402, 61, 280);
  }
  else {
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * (parking ? 7 : 10)) / 2, 230, parking ? F("Parking") : F("Homing XYZ"));
    DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 23) / 2, 260, F("Please wait until done."));
  }
}

#if HAS_ONESTEP_LEVELING

  void Popup_Window_Leveling() {
    Clear_Main_Window();
    Draw_Popup_Bkgd_60();
    DWIN_ICON_Show(ICON, ICON_AutoLeveling, 101, 105);
    if (HMI_IsChinese()) {
      DWIN_Frame_AreaCopy(1, 0, 371, 100, 386, 84, 240);
      DWIN_Frame_AreaCopy(1, 0, 389, 150, 402, 61, 280);
    }
    else {
      DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 13) / 2, 230, GET_TEXT_F(MSG_BED_LEVELING));
      DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 23) / 2, 260, F("Please wait until done."));
    }
  }

#endif

void Draw_Select_Highlight(const bool sel) {
  HMI_flag.select_flag = sel;
  const uint16_t c1 = sel ? Select_Color : Color_Bg_Window,
                 c2 = sel ? Color_Bg_Window : Select_Color;
  DWIN_Draw_Rectangle(0, c1, 25, 279, 126, 318);
  DWIN_Draw_Rectangle(0, c1, 24, 278, 127, 319);
  DWIN_Draw_Rectangle(0, c2, 145, 279, 246, 318);
  DWIN_Draw_Rectangle(0, c2, 144, 278, 247, 319);
}

void Popup_window_PauseOrStop() {
  Clear_Main_Window();
  Draw_Popup_Bkgd_60();
  if (HMI_IsChinese()) {
         if (select_print.now == 1) DWIN_Frame_AreaCopy(1, 237, 338, 269, 356, 98, 150);
    else if (select_print.now == 2) DWIN_Frame_AreaCopy(1, 221, 320, 253, 336, 98, 150);
    DWIN_Frame_AreaCopy(1, 220, 304, 264, 319, 130, 150);
    DWIN_ICON_Show(ICON, ICON_Confirm_C, 26, 280);
    DWIN_ICON_Show(ICON, ICON_Cancel_C, 146, 280);
  }
  else {
         if (select_print.now == 1) DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 11) / 2, 150, GET_TEXT_F(MSG_PAUSE_PRINT));
    else if (select_print.now == 2) DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (272 - 8 * 10) / 2, 150, GET_TEXT_F(MSG_STOP_PRINT));
    DWIN_ICON_Show(ICON, ICON_Confirm_E, 26, 280);
    DWIN_ICON_Show(ICON, ICON_Cancel_E, 146, 280);
  }
  Draw_Select_Highlight(true);
}

void Draw_Printing_Screen() {
  if (HMI_IsChinese()) {
    DWIN_Frame_AreaCopy(1, 30,  1,  71, 14,  14,   9);  // Tune
    DWIN_Frame_AreaCopy(1,  0, 72,  63, 86,  41, 188);  // Pause
    DWIN_Frame_AreaCopy(1, 65, 72, 128, 86, 176, 188);  // Stop
  }
  else {
    DWIN_Frame_AreaCopy(1, 40,  2,  92, 14,  14,   9);  // Tune
    DWIN_Frame_AreaCopy(1,  0, 44,  96, 58,  41, 188);  // Pause
    DWIN_Frame_AreaCopy(1, 98, 44, 152, 58, 176, 188);  // Stop
  }
}

void Draw_Print_ProgressBar() {
  DWIN_ICON_Show(ICON, ICON_Bar, 15, 93);
  DWIN_Draw_Rectangle(1, BarFill_Color, 16 + Percentrecord * 240 / 100, 93, 256, 113);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Percent_Color, Color_Bg_Black, 2, 117, 133, Percentrecord);
  DWIN_Draw_String(false, false, font8x16, Percent_Color, Color_Bg_Black, 133, 133, F("%"));
}

void Draw_Print_ProgressElapsed() {
  duration_t elapsed = print_job_timer.duration(); // print timer
  DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, 42, 212, elapsed.value / 3600);
  DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 58, 212, F(":"));
  DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, 66, 212, (elapsed.value % 3600) / 60);
}

void Draw_Print_ProgressRemain() {
  DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, 176, 212, remain_time / 3600);
  DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 192, 212, F(":"));
  DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, 200, 212, (remain_time % 3600) / 60);
}

void Goto_PrintProcess() {
  checkkey = PrintProcess;

  Clear_Main_Window();
  Draw_Printing_Screen();

  ICON_Tune();
  if (printingIsPaused()) ICON_Continue(); else ICON_Pause();
  ICON_Stop();

  // Copy into filebuf string before entry
  char * const name = card.longest_filename();
  const int8_t npos = _MAX(0U, DWIN_WIDTH - strlen(name) * MENU_CHR_W) / 2;
  DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, npos, 60, name);

  DWIN_ICON_Show(ICON, ICON_PrintTime, 17, 193);
  DWIN_ICON_Show(ICON, ICON_RemainTime, 150, 191);

  Draw_Print_ProgressBar();
  Draw_Print_ProgressElapsed();
  Draw_Print_ProgressRemain();
}

void Goto_MainMenu() {
  checkkey = MainMenu;

  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_AreaCopy(1, 2, 2, 27, 14, 14, 9); // "Home"
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Home"); // TODO: GET_TEXT
    #else
      DWIN_Frame_AreaCopy(1, 0, 2, 39, 12, 14, 9);
    #endif
  }

  DWIN_ICON_Show(ICON, ICON_LOGO, 71, 52);

  ICON_Print();
  ICON_Prepare();
  ICON_Control();
  ICON_Leveling();
}

inline ENCODER_DiffState get_encoder_state() {
  static millis_t Encoder_ms = 0;
  const millis_t ms = millis();
  if (PENDING(ms, Encoder_ms)) return ENCODER_DIFF_NO;
  const ENCODER_DiffState state = Encoder_ReceiveAnalyze();
  if (state != ENCODER_DIFF_NO) Encoder_ms = ms + ENCODER_WAIT;
  return state;
}

void HMI_Move_X() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_X_scale)) {
      checkkey = AxisMove;
      EncoderRate.enabled = false;
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 216, MBASE(1), HMI_ValueStruct.Move_X_scale);
      if (!planner.is_full()) {
        // Wait for planner moves to finish!
        planner.synchronize();
        planner.buffer_line(current_position, homing_feedrate(X_AXIS), active_extruder);
      }
      DWIN_UpdateLCD();
      return;
    }
    NOLESS(HMI_ValueStruct.Move_X_scale, (X_MIN_POS) * MINUNITMULT);
    NOMORE(HMI_ValueStruct.Move_X_scale, (X_MAX_POS) * MINUNITMULT);
    current_position.x = HMI_ValueStruct.Move_X_scale / 10;
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, 216, MBASE(1), HMI_ValueStruct.Move_X_scale);
    DWIN_UpdateLCD();
  }
}

void HMI_Move_Y() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_Y_scale)) {
      checkkey = AxisMove;
      EncoderRate.enabled = false;
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 216, MBASE(2), HMI_ValueStruct.Move_Y_scale);
      if (!planner.is_full()) {
        // Wait for planner moves to finish!
        planner.synchronize();
        planner.buffer_line(current_position, homing_feedrate(Y_AXIS), active_extruder);
      }
      DWIN_UpdateLCD();
      return;
    }
    NOLESS(HMI_ValueStruct.Move_Y_scale, (Y_MIN_POS) * MINUNITMULT);
    NOMORE(HMI_ValueStruct.Move_Y_scale, (Y_MAX_POS) * MINUNITMULT);
    current_position.y = HMI_ValueStruct.Move_Y_scale / 10;
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, 216, MBASE(2), HMI_ValueStruct.Move_Y_scale);
    DWIN_UpdateLCD();
  }
}

void HMI_Move_Z() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_Z_scale)) {
      checkkey = AxisMove;
      EncoderRate.enabled = false;
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 216, MBASE(3), HMI_ValueStruct.Move_Z_scale);
      if (!planner.is_full()) {
        // Wait for planner moves to finish!
        planner.synchronize();
        planner.buffer_line(current_position, homing_feedrate(Z_AXIS), active_extruder);
      }
      DWIN_UpdateLCD();
      return;
    }
    NOLESS(HMI_ValueStruct.Move_Z_scale, Z_MIN_POS * MINUNITMULT);
    NOMORE(HMI_ValueStruct.Move_Z_scale, Z_MAX_POS * MINUNITMULT);
    current_position.z = HMI_ValueStruct.Move_Z_scale / 10;
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, 216, MBASE(3), HMI_ValueStruct.Move_Z_scale);
    DWIN_UpdateLCD();
  }
}

#if HAS_HOTEND

  void HMI_Move_E() {
    static float last_E_scale = 0;
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_E_scale)) {
        checkkey = AxisMove;
        EncoderRate.enabled = false;
        last_E_scale = HMI_ValueStruct.Move_E_scale;
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(4), HMI_ValueStruct.Move_E_scale);
        if (!planner.is_full()) {
          planner.synchronize(); // Wait for planner moves to finish!
          planner.buffer_line(current_position, MMM_TO_MMS(FEEDRATE_E), active_extruder);
        }
        DWIN_UpdateLCD();
        return;
      }
      if ((HMI_ValueStruct.Move_E_scale - last_E_scale) > (EXTRUDE_MAXLENGTH) * MINUNITMULT)
        HMI_ValueStruct.Move_E_scale = last_E_scale + (EXTRUDE_MAXLENGTH) * MINUNITMULT;
      else if ((last_E_scale - HMI_ValueStruct.Move_E_scale) > (EXTRUDE_MAXLENGTH) * MINUNITMULT)
        HMI_ValueStruct.Move_E_scale = last_E_scale - (EXTRUDE_MAXLENGTH) * MINUNITMULT;
      current_position.e = HMI_ValueStruct.Move_E_scale / 10;
      DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, 216, MBASE(4), HMI_ValueStruct.Move_E_scale);
      DWIN_UpdateLCD();
      return;
    }	
	  NOLESS(HMI_ValueStruct.Move_E_scale, (EXTRUDE_MAXLENGTH * MINUNITMULT) * -1); //retract max
	  NOMORE(HMI_ValueStruct.Move_E_scale, EXTRUDE_MAXLENGTH * MINUNITMULT); //extrude max
	  
      current_position.e = HMI_ValueStruct.Move_E_scale / 10;
	  show_plus_or_minus(font8x16, Select_Color, 4, 1, 216, MBASE(4), HMI_ValueStruct.Move_E_scale);
    DWIN_UpdateLCD();
  }

#endif

#if HAS_ZOFFSET_ITEM

  bool printer_busy() { return planner.movesplanned() || printingIsActive(); }

  void HMI_Zoffset() {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      uint8_t zoff_line;
      switch (HMI_ValueStruct.show_mode) {
        case -4: zoff_line = PREPARE_CASE_ZOFF + MROWS - index_prepare; break;
        default: zoff_line = TUNE_CASE_ZOFF + MROWS - index_tune;
      }
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.offset_value)) {
        EncoderRate.enabled = false;
        #if HAS_BED_PROBE
          probe.offset.z = dwin_zoffset;
          TERN_(EEPROM_SETTINGS, settings.save());
        #endif
        checkkey = HMI_ValueStruct.show_mode == -4 ? Prepare : Tune;
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 2, 2, 202, MBASE(zoff_line), TERN(HAS_BED_PROBE, BABY_Z_VAR * 100, HMI_ValueStruct.offset_value));
        DWIN_UpdateLCD();
        return;
      }
      NOLESS(HMI_ValueStruct.offset_value, (Z_PROBE_OFFSET_RANGE_MIN) * 100);
      NOMORE(HMI_ValueStruct.offset_value, (Z_PROBE_OFFSET_RANGE_MAX) * 100);
      last_zoffset = dwin_zoffset;
      dwin_zoffset = HMI_ValueStruct.offset_value / 100.0f;
      #if EITHER(BABYSTEP_ZPROBE_OFFSET, JUST_BABYSTEP)
        if (BABYSTEP_ALLOWED()) babystep.add_mm(Z_AXIS, dwin_zoffset - last_zoffset);
      #endif
      DWIN_Draw_Signed_Float(font8x16, Select_Color, 2, 2, 202, MBASE(zoff_line), HMI_ValueStruct.offset_value);
      DWIN_UpdateLCD();
    }
  }

#endif // HAS_ZOFFSET_ITEM

#if HAS_HOTEND

  void HMI_ETemp() {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      uint8_t temp_line;
      switch (HMI_ValueStruct.show_mode) {
        case -1: temp_line = TEMP_CASE_TEMP; break;
        case -2: temp_line = PREHEAT_CASE_TEMP; break;
        case -3: temp_line = PREHEAT_CASE_TEMP; break;
        default: temp_line = TUNE_CASE_TEMP + MROWS - index_tune;
      }
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.E_Temp)) {
        EncoderRate.enabled = false;
        if (HMI_ValueStruct.show_mode == -2) {
          checkkey = PLAPreheat;
          ui.material_preset[0].hotend_temp = HMI_ValueStruct.E_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(temp_line), ui.material_preset[0].hotend_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -3) {
          checkkey = ABSPreheat;
          ui.material_preset[1].hotend_temp = HMI_ValueStruct.E_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(temp_line), ui.material_preset[1].hotend_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -1) // Temperature
          checkkey = TemperatureID;
        else
          checkkey = Tune;
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(temp_line), HMI_ValueStruct.E_Temp);
        thermalManager.setTargetHotend(HMI_ValueStruct.E_Temp, 0);
        return;
      }
      // E_Temp limit
      NOMORE(HMI_ValueStruct.E_Temp, MAX_E_TEMP);
      NOLESS(HMI_ValueStruct.E_Temp, MIN_E_TEMP);
      // E_Temp value
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(temp_line), HMI_ValueStruct.E_Temp);
    }

#endif // HAS_HOTEND

	//Specify negative value -- overrides endstop limits when used on screen be careful
	NOLESS(UBLZPOS, Z_PROBE_OFFSET_RANGE_MIN * 1000);
	NOMORE(UBLZPOS, Z_PROBE_OFFSET_RANGE_MAX * 1000);
    
	if (UBLZPOS == oldZ){
	  show_plus_or_minus(font8x16, Select_Color, 1, 3, 160, 299, UBLZPOS);
	}
	else{
	  show_plus_or_minus(font8x16, Adjustment_Color, 1, 3, 160, 299, UBLZPOS);
	}

  void HMI_BedTemp() {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      uint8_t bed_line;
      switch (HMI_ValueStruct.show_mode) {
        case -1: bed_line = TEMP_CASE_BED; break;
        case -2: bed_line = PREHEAT_CASE_BED; break;
        case -3: bed_line = PREHEAT_CASE_BED; break;
        default: bed_line = TUNE_CASE_BED + MROWS - index_tune;
      }
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Bed_Temp)) {
        EncoderRate.enabled = false;
        if (HMI_ValueStruct.show_mode == -2) {
          checkkey = PLAPreheat;
          ui.material_preset[0].bed_temp = HMI_ValueStruct.Bed_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(bed_line), ui.material_preset[0].bed_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -3) {
          checkkey = ABSPreheat;
          ui.material_preset[1].bed_temp = HMI_ValueStruct.Bed_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(bed_line), ui.material_preset[1].bed_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -1)
          checkkey = TemperatureID;
        else
          checkkey = Tune;
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(bed_line), HMI_ValueStruct.Bed_Temp);
        thermalManager.setTargetBed(HMI_ValueStruct.Bed_Temp);
        return;
      }
      // Bed_Temp limit
      NOMORE(HMI_ValueStruct.Bed_Temp, BED_MAX_TARGET);
      NOLESS(HMI_ValueStruct.Bed_Temp, MIN_BED_TEMP);
      // Bed_Temp value
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(bed_line), HMI_ValueStruct.Bed_Temp);
    }
  }

#endif // HAS_HEATED_BED

#if HAS_PREHEAT

  void HMI_FanSpeed() {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      uint8_t fan_line;
      switch (HMI_ValueStruct.show_mode) {
        case -1: fan_line = TEMP_CASE_FAN; break;
        case -2: fan_line = PREHEAT_CASE_FAN; break;
        case -3: fan_line = PREHEAT_CASE_FAN; break;
        default: fan_line = TUNE_CASE_FAN + MROWS - index_tune;
      }

      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Fan_speed)) {
        EncoderRate.enabled = false;
        if (HMI_ValueStruct.show_mode == -2) {
          checkkey = PLAPreheat;
          ui.material_preset[0].fan_speed = HMI_ValueStruct.Fan_speed;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(fan_line), ui.material_preset[0].fan_speed);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -3) {
          checkkey = ABSPreheat;
          ui.material_preset[1].fan_speed = HMI_ValueStruct.Fan_speed;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(fan_line), ui.material_preset[1].fan_speed);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -1)
          checkkey = TemperatureID;
        else
          checkkey = Tune;
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(fan_line), HMI_ValueStruct.Fan_speed);
        thermalManager.set_fan_speed(0, HMI_ValueStruct.Fan_speed);
        return;
      }
      // Fan_speed limit
      NOMORE(HMI_ValueStruct.Fan_speed, FANON);
      NOLESS(HMI_ValueStruct.Fan_speed, FANOFF);
      // Fan_speed value
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(fan_line), HMI_ValueStruct.Fan_speed);
    }
  }

#endif // HAS_PREHEAT

void HMI_PrintSpeed() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.print_speed)) {
      checkkey = Tune;
      EncoderRate.enabled = false;
      feedrate_percentage = HMI_ValueStruct.print_speed;
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(select_tune.now + MROWS - index_tune), HMI_ValueStruct.print_speed);
      return;
    }
    // print_speed limit
    NOMORE(HMI_ValueStruct.print_speed, MAX_PRINT_SPEED);
    NOLESS(HMI_ValueStruct.print_speed, MIN_PRINT_SPEED);
    // print_speed value
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(select_tune.now + MROWS - index_tune), HMI_ValueStruct.print_speed);
  }
}

void HMI_MaxFeedspeedXYZE() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Feedspeed)) {
      checkkey = MaxSpeed;
      EncoderRate.enabled = false;
      if (WITHIN(HMI_flag.feedspeed_axis, X_AXIS, E_AXIS))
        planner.set_max_feedrate(HMI_flag.feedspeed_axis, HMI_ValueStruct.Max_Feedspeed);
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(select_speed.now), HMI_ValueStruct.Max_Feedspeed);
      return;
    }
    // MaxFeedspeed limit
    if (WITHIN(HMI_flag.feedspeed_axis, X_AXIS, E_AXIS))
      NOMORE(HMI_ValueStruct.Max_Feedspeed, default_max_feedrate[HMI_flag.feedspeed_axis] * 2);
    if (HMI_ValueStruct.Max_Feedspeed < MIN_MAXFEEDSPEED) HMI_ValueStruct.Max_Feedspeed = MIN_MAXFEEDSPEED;
    // MaxFeedspeed value
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, 210, MBASE(select_speed.now), HMI_ValueStruct.Max_Feedspeed);
  }
}

void HMI_MaxAccelerationXYZE() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Acceleration)) {
      checkkey = MaxAcceleration;
      EncoderRate.enabled = false;
      if (HMI_flag.acc_axis == X_AXIS) planner.set_max_acceleration(X_AXIS, HMI_ValueStruct.Max_Acceleration);
      else if (HMI_flag.acc_axis == Y_AXIS) planner.set_max_acceleration(Y_AXIS, HMI_ValueStruct.Max_Acceleration);
      else if (HMI_flag.acc_axis == Z_AXIS) planner.set_max_acceleration(Z_AXIS, HMI_ValueStruct.Max_Acceleration);
      #if HAS_HOTEND
        else if (HMI_flag.acc_axis == E_AXIS) planner.set_max_acceleration(E_AXIS, HMI_ValueStruct.Max_Acceleration);
      #endif
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(select_acc.now), HMI_ValueStruct.Max_Acceleration);
      return;
    }
    // MaxAcceleration limit
    if (WITHIN(HMI_flag.acc_axis, X_AXIS, E_AXIS))
      NOMORE(HMI_ValueStruct.Max_Acceleration, default_max_acceleration[HMI_flag.acc_axis] * 2);
    if (HMI_ValueStruct.Max_Acceleration < MIN_MAXACCELERATION) HMI_ValueStruct.Max_Acceleration = MIN_MAXACCELERATION;
    // MaxAcceleration value
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, 210, MBASE(select_acc.now), HMI_ValueStruct.Max_Acceleration);
  }
}

#if HAS_CLASSIC_JERK

  void HMI_MaxJerkXYZE() {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Jerk)) {
        checkkey = MaxJerk;
        EncoderRate.enabled = false;
        if (WITHIN(HMI_flag.jerk_axis, X_AXIS, E_AXIS))
          planner.set_max_jerk(HMI_flag.jerk_axis, HMI_ValueStruct.Max_Jerk / 10);
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(select_jerk.now), HMI_ValueStruct.Max_Jerk);
        return;
      }
      // MaxJerk limit
      if (WITHIN(HMI_flag.jerk_axis, X_AXIS, E_AXIS))
        NOMORE(HMI_ValueStruct.Max_Jerk, default_max_jerk[HMI_flag.jerk_axis] * 2 * MINUNITMULT);
      NOLESS(HMI_ValueStruct.Max_Jerk, (MIN_MAXJERK) * MINUNITMULT);
      // MaxJerk value
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, 210, MBASE(select_jerk.now), HMI_ValueStruct.Max_Jerk);
    }
  }

#endif // HAS_CLASSIC_JERK

void HMI_StepXYZE() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Step)) {
      checkkey = Step;
      EncoderRate.enabled = false;
      if (WITHIN(HMI_flag.step_axis, X_AXIS, E_AXIS))
        planner.settings.axis_steps_per_mm[HMI_flag.step_axis] = HMI_ValueStruct.Max_Step / 10;
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(select_step.now), HMI_ValueStruct.Max_Step);
      return;
    }
    // Step limit
    if (WITHIN(HMI_flag.step_axis, X_AXIS, E_AXIS))
      NOMORE(HMI_ValueStruct.Max_Step, 999.9 * MINUNITMULT);
    NOLESS(HMI_ValueStruct.Max_Step, MIN_STEP);
    // Step value
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, 210, MBASE(select_step.now), HMI_ValueStruct.Max_Step);
  }
}

void external_print_tune(void) {
	if (print_job_timer.duration() > 1 && ExtPrint_flag == 0) {
	  ExtPrint_flag = 2;
	  checkkey = Tune;
      HMI_ValueStruct.show_mode = 0;
      select_tune.now = 1;
      index_tune = 5;
      Draw_Tune_Menu();
	}
	
	if (print_job_timer.duration() < 2 && ExtPrint_flag == 2){
	  ExtPrint_flag = 0;
	  select_page.set(3);
      Goto_MainMenu();
    }
	
	if (ExtPrint_flag == 5){
		if (print_job_timer.duration() > 0){
			  queue.inject_P(PSTR("M77\nM75\nM77"));
			  delay(20);
		}
		else {
		  ExtPrint_flag = 0;
		}
	}
}

void update_variable(void) {
  /* Tune page temperature update */
  if (checkkey == Tune) {
    #if HAS_HOTEND
      if (last_temp_hotend_target != thermalManager.temp_hotend[0].target)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_TEMP + MROWS - index_tune), thermalManager.temp_hotend[0].target);
    #endif
    #if HAS_HEATED_BED
      if (last_temp_bed_target != thermalManager.temp_bed.target)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_BED + MROWS - index_tune), thermalManager.temp_bed.target);
    #endif
    #if HAS_FAN
      if (last_fan_speed != thermalManager.fan_speed[0]) {
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TUNE_CASE_FAN + MROWS - index_tune), thermalManager.fan_speed[0]);
        last_fan_speed = thermalManager.fan_speed[0];
      }
    #endif
  }

  /* Temperature page temperature update */
  if (checkkey == TemperatureID) {
    #if HAS_HOTEND
      if (last_temp_hotend_target != thermalManager.temp_hotend[0].target)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TEMP_CASE_TEMP), thermalManager.temp_hotend[0].target);
    #endif
    #if HAS_HEATED_BED
      if (last_temp_bed_target != thermalManager.temp_bed.target)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TEMP_CASE_BED), thermalManager.temp_bed.target);
    #endif
    #if HAS_FAN
      if (last_fan_speed != thermalManager.fan_speed[0]) {
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(TEMP_CASE_FAN), thermalManager.fan_speed[0]);
        last_fan_speed = thermalManager.fan_speed[0];
      }
    #endif
  }

  /* Bottom temperature update */
  #if HAS_HOTEND
    if (last_temp_hotend_current != thermalManager.temp_hotend[0].celsius) {
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 33, 382, thermalManager.temp_hotend[0].celsius);
      last_temp_hotend_current = thermalManager.temp_hotend[0].celsius;
    }
    if (last_temp_hotend_target != thermalManager.temp_hotend[0].target) {
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 33 + 4 * STAT_CHR_W + 6, 382, thermalManager.temp_hotend[0].target);
      last_temp_hotend_target = thermalManager.temp_hotend[0].target;
    }
  #endif
  #if HAS_HEATED_BED
    if (last_temp_bed_current != thermalManager.temp_bed.celsius) {
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 178, 382, thermalManager.temp_bed.celsius);
      last_temp_bed_current = thermalManager.temp_bed.celsius;
    }
    if (last_temp_bed_target != thermalManager.temp_bed.target) {
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 178 + 4 * STAT_CHR_W + 6, 382, thermalManager.temp_bed.target);
      last_temp_bed_target = thermalManager.temp_bed.target;
    }
  #endif
  static uint16_t last_speed = 0;
  if (last_speed != feedrate_percentage) {
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 33 + 2 * STAT_CHR_W, 429, feedrate_percentage);
    last_speed = feedrate_percentage;
  }
  #if HAS_ZOFFSET_ITEM
    if (last_zoffset != BABY_Z_VAR) {
      DWIN_Draw_Signed_Float(DWIN_FONT_STAT, Color_Bg_Black, 2, 2, 178 + STAT_CHR_W, 429, BABY_Z_VAR * 100);
      last_zoffset = BABY_Z_VAR;
    }
  #endif
}

/**
 * Read and cache the working directory.
 *
 * TODO: New code can follow the pattern of menu_media.cpp
 * and rely on Marlin caching for performance. No need to
 * cache files here.
 */

#ifndef strcasecmp_P
  #define strcasecmp_P(a, b) strcasecmp((a), (b))
#endif

inline void make_name_without_ext(char *dst, char *src, int maxlen=MENU_CHAR_LIMIT) {
  char * const name = card.longest_filename();
  size_t pos        = strlen(name); // index of ending nul
  
  // For files, remove the extension
  // which may be .gcode, .gco, or .g
  if (!card.flag.filenameIsDir)
    while (pos && src[pos] != '.') pos--; // find last '.' (stop at 0)

  int len = pos;      // nul or '.'
  if (len > maxlen) { // Keep the name short
    pos        = len = maxlen; // move nul down
    dst[--pos] = '.'; // insert dots
    dst[--pos] = '.';
    dst[--pos] = '.';
  }

  dst[len] = '\0';    // end it

  // Copy down to 0
  while (pos--) dst[pos] = src[pos];
}

inline void HMI_SDCardInit() { card.cdroot(); }

void MarlinUI::refresh() { /* Nothing to see here */ }

#define ICON_Folder ICON_More

#if ENABLED(SCROLL_LONG_FILENAMES)

  char shift_name[LONG_FILENAME_LENGTH + 1];
  int8_t shift_amt; // = 0
  millis_t shift_ms; // = 0

  // Init the shift name based on the highlighted item
  inline void Init_Shift_Name() {
    const bool is_subdir = !card.flag.workDirIsRoot;
    const int8_t filenum = select_file.now - 1 - is_subdir; // Skip "Back" and ".."
    const uint16_t fileCnt = card.get_num_Files();
    if (WITHIN(filenum, 0, fileCnt - 1)) {
      card.getfilename_sorted(SD_ORDER(filenum, fileCnt));
      char * const name = card.longest_filename();
      make_name_without_ext(shift_name, name, 100);
    }
  }

  inline void Init_SDItem_Shift() {
    shift_amt = 0;
    shift_ms  = select_file.now > 0 && strlen(shift_name) > MENU_CHAR_LIMIT
           ? millis() + 750UL : 0;
  }

#endif

/**
 * Display an SD item, adding a CDUP for subfolders.
 */
inline void Draw_SDItem(const uint16_t item, int16_t row=-1) {
  if (row < 0) row = item + 1 + MROWS - index_file;
  const bool is_subdir = !card.flag.workDirIsRoot;
  if (is_subdir && item == 0) {
    Draw_Menu_Line(row, ICON_Folder, (char*)"..");
    return;
  }

  card.getfilename_sorted(SD_ORDER(item - is_subdir, card.get_num_Files()));
  char * const name = card.longest_filename();

  #if ENABLED(SCROLL_LONG_FILENAMES)
    // Init the current selected name
    // This is used during scroll drawing
    if (item == select_file.now - 1) {
      make_name_without_ext(shift_name, name, 100);
      Init_SDItem_Shift();
    }
  #endif

  // Draw the file/folder with name aligned left
  char str[strlen(name) + 1];
  make_name_without_ext(str, name);
  Draw_Menu_Line(row, card.flag.filenameIsDir ? ICON_Folder : ICON_File, str);
}

#if ENABLED(SCROLL_LONG_FILENAMES)

  inline void Draw_SDItem_Shifted(int8_t &shift) {
    // Limit to the number of chars past the cutoff
    const size_t len = strlen(shift_name);
    NOMORE(shift, _MAX(len - MENU_CHAR_LIMIT, 0U));

    // Shorten to the available space
    const size_t lastchar = _MIN((signed)len, shift + MENU_CHAR_LIMIT);

    const char c = shift_name[lastchar];
    shift_name[lastchar] = '\0';

    const uint8_t row = select_file.now + MROWS - index_file; // skip "Back" and scroll
    Erase_Menu_Text(row);
    Draw_Menu_Line(row, 0, &shift_name[shift]);

    shift_name[lastchar] = c;
  }

#endif

// Redraw the first set of SD Files
inline void Redraw_SD_List() {
  select_file.reset();
  index_file = MROWS;

  Clear_Menu_Area(); // Leave title bar unchanged

  Draw_Back_First();

  if (card.isMounted()) {
    // As many files as will fit
    LOOP_L_N(i, _MIN(nr_sd_menu_items(), MROWS))
      Draw_SDItem(i, i + 1);

    TERN_(SCROLL_LONG_FILENAMES, Init_SDItem_Shift());
  }
  else {
    DWIN_Draw_Rectangle(1, Color_Bg_Red, 10, MBASE(3) - 10, DWIN_WIDTH - 10, MBASE(4));
    DWIN_Draw_String(false, false, font16x32, Color_Yellow, Color_Bg_Red, ((DWIN_WIDTH) - 8 * 16) / 2, MBASE(3), F("No Media"));
  }
}

bool DWIN_lcd_sd_status = false;

inline void SDCard_Up() {
  card.cdup();
  Redraw_SD_List();
  DWIN_lcd_sd_status = false; // On next DWIN_Update
}

inline void SDCard_Folder(char * const dirname) {
  card.cd(dirname);
  Redraw_SD_List();
  DWIN_lcd_sd_status = false; // On next DWIN_Update
}

//
// Watch for media mount / unmount
//
void HMI_SDCardUpdate() {
  if (HMI_flag.home_flag) return;
  if (DWIN_lcd_sd_status != card.isMounted()) {
    DWIN_lcd_sd_status = card.isMounted();
    // SERIAL_ECHOLNPAIR("HMI_SDCardUpdate: ", int(DWIN_lcd_sd_status));
    if (DWIN_lcd_sd_status) {
      if (checkkey == SelectFile)
        Redraw_SD_List();
    }
    else {
      // clean file icon
      if (checkkey == SelectFile) {
        Redraw_SD_List();
      }
      else if (checkkey == PrintProcess || checkkey == Tune || printingIsActive()) {
        // TODO: Move card removed abort handling
        //       to CardReader::manage_media.
        card.flag.abort_sd_printing = true;
        wait_for_heatup = wait_for_user = false;
        dwin_abort_flag = true; // Reset feedrate, return to Home
      }
    }
    DWIN_UpdateLCD();
  }
}

//
// The status area is always on-screen, except during
// full-screen modal dialogs. (TODO: Keep alive during dialogs)
//
void Draw_Status_Area(const bool with_update) {

  // Clear the bottom area of the screen
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, STATUS_Y, DWIN_WIDTH, DWIN_HEIGHT - 1);

  //
  // Status Area
  //
  #if HAS_HOTEND
    DWIN_ICON_Show(ICON, ICON_HotendTemp, 13, 381);
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 33, 382, thermalManager.temp_hotend[0].celsius);
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 33 + 3 * STAT_CHR_W + 5, 383, F("/"));
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 33 + 4 * STAT_CHR_W + 6, 382, thermalManager.temp_hotend[0].target);
  #endif
  #if HOTENDS > 1
    // DWIN_ICON_Show(ICON,ICON_HotendTemp, 13, 381);
  #endif

  #if HAS_HEATED_BED
    DWIN_ICON_Show(ICON, ICON_BedTemp, 158, 381);
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 178, 382, thermalManager.temp_bed.celsius);
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 178 + 3 * STAT_CHR_W + 5, 383, F("/"));
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 178 + 4 * STAT_CHR_W + 6, 382, thermalManager.temp_bed.target);
  #endif

  DWIN_ICON_Show(ICON, ICON_Speed, 13, 429);
  DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 33 + 2 * STAT_CHR_W, 429, feedrate_percentage);
  DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 33 + 5 * STAT_CHR_W + 2, 429, F("%"));

  #if HAS_ZOFFSET_ITEM
    DWIN_ICON_Show(ICON, ICON_Zoffset, 158, 428);
    dwin_zoffset = BABY_Z_VAR;
    DWIN_Draw_Signed_Float(DWIN_FONT_STAT, Color_Bg_Black, 2, 2, 178, 429, dwin_zoffset * 100);
  #endif

  if (with_update) {
    DWIN_UpdateLCD();
    delay(5);
  }
}

void HMI_StartFrame(const bool with_update) {
  Goto_MainMenu();
  Draw_Status_Area(with_update);
}

inline void Draw_Info_Menu() {
  Clear_Main_Window();

  DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(MACHINE_SIZE) * MENU_CHR_W) / 2, 122, (char*)MACHINE_SIZE);
  DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(SHORT_BUILD_VERSION) * MENU_CHR_W) / 2, 195, (char*)SHORT_BUILD_VERSION);

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 30, 17, 57, 29); // "Info"

    DWIN_Frame_AreaCopy(1, 197, 149, 252, 161, 108, 102);
    DWIN_Frame_AreaCopy(1, 1, 164, 56, 176, 108, 175);
    DWIN_Frame_AreaCopy(1, 58, 164, 113, 176, 105, 248);
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(CORP_WEBSITE_C) * MENU_CHR_W) / 2, 268, (char*)CORP_WEBSITE_C);
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Info"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_TitleCopy(1, 190, 16, 215, 26); // "Info"
    #endif

    DWIN_Frame_AreaCopy(1, 120, 150, 146, 161, 124, 102);
    DWIN_Frame_AreaCopy(1, 146, 151, 254, 161, 82, 175);
    DWIN_Frame_AreaCopy(1, 0, 165, 94, 175, 89, 248);
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(CORP_WEBSITE_E) * MENU_CHR_W) / 2, 268, (char*)CORP_WEBSITE_E);
  }

  Draw_Back_First();
  LOOP_L_N(i, 3) {
    DWIN_ICON_Show(ICON, ICON_PrintSize + i, 26, 99 + i * 73);
    DWIN_Draw_Line(Line_Color, 16, MBASE(2) + i * 73, 256, 156 + i * 73);
  }
}

inline void Draw_Print_File_Menu() {
  Clear_Title_Bar();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 0, 31, 55, 44); // "Print file"
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Print file"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_TitleCopy(1, 52, 31, 137, 41); // "Print file"
    #endif
  }

  Redraw_SD_List();
}

/* Main Process */
void HMI_MainMenu() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_page.inc(4)) {
      switch (select_page.now) {
        case 0: ICON_Print(); break;
        case 1: ICON_Print(); ICON_Prepare(); break;
        case 2: ICON_Prepare(); ICON_Control(); break;
        case 3: ICON_Control(); ICON_Leveling(); break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_page.dec()) {
      switch (select_page.now) {
        case 0: ICON_Print(); ICON_Prepare(); break;
        case 1: ICON_Prepare(); ICON_Control(); break;
        case 2: ICON_Control(); ICON_Leveling(); break;
        case 3: ICON_Leveling(); break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_page.now) {
      case 0: // Print File
        checkkey = SelectFile;
		ExtPrint_flag = 1;
        Draw_Print_File_Menu();
        break;

      case 1: // Prepare
        checkkey = Prepare;
        select_prepare.reset();
        index_prepare = MROWS;
        Draw_Prepare_Menu();
        break;

      case 2: // Control
        checkkey = Control;
        select_control.reset();
        index_control = MROWS;
        Draw_Control_Menu();
        break;

      case 3: // Leveling or Info
        #if HAS_ONESTEP_LEVELING
          checkkey = Leveling;
          HMI_Leveling();
		  break;
		  #endif
        #else
          if (beenHomed == 0){
			levelM = 1;
		    mainFlag = 1;
			checkkey = Last_Prepare;
			beenHomed = 2;
			queue.inject_P(PSTR("G28"));
			Popup_Window_Home();
			break;
		  }
		  else{
		   checkkey = Mlevel;
		   levelM = 1;
           Tool = -1;
		   mainFlag = 1;
           Draw_Mlevel_Menu();
		   break;
		  }
        #endif
        break;
    }
  }
  DWIN_UpdateLCD();
}

// Select (and Print) File
void HMI_SelectFile() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();

  const uint16_t hasUpDir = !card.flag.workDirIsRoot;

  if (encoder_diffState == ENCODER_DIFF_NO) {
    #if ENABLED(SCROLL_LONG_FILENAMES)
      if (shift_ms && select_file.now >= 1 + hasUpDir) {
        // Scroll selected filename every second
        const millis_t ms = millis();
        if (ELAPSED(ms, shift_ms)) {
          const bool was_reset = shift_amt < 0;
          shift_ms = ms + 375UL + was_reset * 250UL;  // ms per character
          int8_t shift_new = shift_amt + 1;           // Try to shift by...
          Draw_SDItem_Shifted(shift_new);             // Draw the item
          if (!was_reset && shift_new == 0)           // Was it limited to 0?
            shift_ms = 0;                             // No scrolling needed
          else if (shift_new == shift_amt)            // Scroll reached the end
            shift_new = -1;                           // Reset
          shift_amt = shift_new;                      // Set new scroll
        }
      }
    #endif
    return;
	}
  }

  // First pause is long. Easy.
  // On reset, long pause must be after 0.

  const uint16_t fullCnt = nr_sd_menu_items();

  if (encoder_diffState == ENCODER_DIFF_CW && fullCnt) {
    if (select_file.inc(1 + fullCnt)) {
      const uint8_t itemnum = select_file.now - 1;              // -1 for "Back"
      if (TERN0(SCROLL_LONG_FILENAMES, shift_ms)) {             // If line was shifted
        Erase_Menu_Text(itemnum + MROWS - index_file);          // Erase and
        Draw_SDItem(itemnum - 1);                               // redraw
      }
      if (select_file.now > MROWS && select_file.now > index_file) { // Cursor past the bottom
        index_file = select_file.now;                           // New bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        Draw_SDItem(itemnum, MROWS);                            // Draw and init the shift name
      }
      else {
        Move_Highlight(1, select_file.now + MROWS - index_file); // Just move highlight
        TERN_(SCROLL_LONG_FILENAMES, Init_Shift_Name());         // ...and init the shift name
      }
      TERN_(SCROLL_LONG_FILENAMES, Init_SDItem_Shift());
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW && fullCnt) {
    if (select_file.dec()) {
      const uint8_t itemnum = select_file.now - 1;              // -1 for "Back"
      if (TERN0(SCROLL_LONG_FILENAMES, shift_ms)) {             // If line was shifted
        Erase_Menu_Text(select_file.now + 1 + MROWS - index_file); // Erase and
        Draw_SDItem(itemnum + 1);                               // redraw
      }
      if (select_file.now < index_file - MROWS) {               // Cursor past the top
        index_file--;                                           // New bottom line
        Scroll_Menu(DWIN_SCROLL_DOWN);
        if (index_file == MROWS) {
          Draw_Back_First();
          TERN_(SCROLL_LONG_FILENAMES, shift_ms = 0);
        }
        else {
          Draw_SDItem(itemnum, 0);                              // Draw the item (and init shift name)
        }
      }
      else {
        Move_Highlight(-1, select_file.now + MROWS - index_file); // Just move highlight
        TERN_(SCROLL_LONG_FILENAMES, Init_Shift_Name());        // ...and init the shift name
      }
      TERN_(SCROLL_LONG_FILENAMES, Init_SDItem_Shift());        // Reset left. Init timer.
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (select_file.now == 0) { // Back
      select_page.set(0);
      Goto_MainMenu();
	  //reset flag
	  ExtPrint_flag = 5;
    }
    else if (hasUpDir && select_file.now == 1) { // CD-Up
      SDCard_Up();
      goto HMI_SelectFileExit;
    }
    else {
      const uint16_t filenum = select_file.now - 1 - hasUpDir;
      card.getfilename_sorted(SD_ORDER(filenum, card.get_num_Files()));

      // Enter that folder!
      if (card.flag.filenameIsDir) {
        SDCard_Folder(card.filename);
        goto HMI_SelectFileExit;
      }

      // Reset highlight for next entry
      select_print.reset();
      select_file.reset();

      // Start choice and print SD file
      HMI_flag.heat_flag = true;
      HMI_flag.print_finish = false;
      HMI_ValueStruct.show_mode = 0;

	  
      card.openAndPrintFile(card.filename);

      #if FAN_COUNT > 0
        // All fans on for Ender 3 v2 ?
        // The slicer should manage this for us.
        // for (uint8_t i = 0; i < FAN_COUNT; i++)
        //  thermalManager.fan_speed[i] = FANON;
      #endif

      Goto_PrintProcess();
    }
  }
HMI_SelectFileExit:
  DWIN_UpdateLCD();
}

/* Printing */
void HMI_Printing() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  if (HMI_flag.done_confirm_flag) {
    if (encoder_diffState == ENCODER_DIFF_ENTER) {
      HMI_flag.done_confirm_flag = false;
      dwin_abort_flag = true; // Reset feedrate, return to Home
    }
    return;
  }

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_print.inc(3)) {
      switch (select_print.now) {
        case 0: ICON_Tune(); break;
        case 1:
          ICON_Tune();
          if (printingIsPaused()) ICON_Continue(); else ICON_Pause();
          break;
        case 2:
          if (printingIsPaused()) ICON_Continue(); else ICON_Pause();
          ICON_Stop();
          break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_print.dec()) {
      switch (select_print.now) {
        case 0:
          ICON_Tune();
          if (printingIsPaused()) ICON_Continue(); else ICON_Pause();
          break;
        case 1:
          if (printingIsPaused()) ICON_Continue(); else ICON_Pause();
          ICON_Stop();
          break;
        case 2: ICON_Stop(); break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_print.now) {
      case 0: // Tune
        checkkey = Tune;
        HMI_ValueStruct.show_mode = 0;
        select_tune.reset();
        index_tune = MROWS;
        Draw_Tune_Menu();
        break;
      case 1: // Pause
        if (HMI_flag.pause_flag) {
          ICON_Pause();

          char cmd[40];
          cmd[0] = '\0';

          #if ENABLED(PAUSE_HEAT)
            #if HAS_HEATED_BED
              if (tempbed) sprintf_P(cmd, PSTR("M190 S%i\n"), tempbed);
            #endif
            #if HAS_HOTEND
              if (temphot) sprintf_P(&cmd[strlen(cmd)], PSTR("M109 S%i\n"), temphot);
            #endif
          #endif

          strcat_P(cmd, M24_STR);
          queue.inject(cmd);
        }
        else {
          HMI_flag.select_flag = true;
          checkkey = Print_window;
          Popup_window_PauseOrStop();
        }
        break;

      case 2: // Stop
        HMI_flag.select_flag = true;
        checkkey = Print_window;
		
        Popup_window_PauseOrStop();
        break;

      default: break;
    }
  }
  DWIN_UpdateLCD();
}

/* Pause and Stop window */
void HMI_PauseOrStop() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  if (encoder_diffState == ENCODER_DIFF_CW)
    Draw_Select_Highlight(false);
  else if (encoder_diffState == ENCODER_DIFF_CCW)
    Draw_Select_Highlight(true);
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (select_print.now == 1) { // pause window
      if (HMI_flag.select_flag) {
        HMI_flag.pause_action = true;
        ICON_Continue();
        #if ENABLED(POWER_LOSS_RECOVERY)
          if (recovery.enabled) recovery.save(true);
        #endif
        queue.inject_P(PSTR("M25"));
      }
      else {
        // cancel pause
      }
      Goto_PrintProcess();
    }
    else if (select_print.now == 2) { // stop window
      if (HMI_flag.select_flag) {
        wait_for_heatup = false; // Stop waiting for heater

        #if 0
          // TODO: In ExtUI or MarlinUI add a common stop event
          // card.flag.abort_sd_printing = true;
        #else
          checkkey = Back_Main;
          // Wait for planner moves to finish!
          if (HMI_flag.home_flag) planner.synchronize();
          card.endFilePrint();
          #ifdef ACTION_ON_CANCEL
            host_action_cancel();
          #endif
          #ifdef EVENT_GCODE_SD_STOP
            Popup_Window_Home();
            queue.inject_P(PSTR("M77\nG28 X Y")); // For Ender 3 "G28 X Y"
          #endif
          abort_flag = true;
		  // set flag high
		  ExtPrint_flag = 5;
        #endif
        Popup_Window_Home(true);
      }
      else
        Goto_PrintProcess(); // cancel stop
    }
  }
  DWIN_UpdateLCD();
}

inline void Draw_Move_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 192, 1, 233, 14); // "Move"
    DWIN_Frame_AreaCopy(1, 58, 118, 106, 132, LBLX, MBASE(1));
    DWIN_Frame_AreaCopy(1, 109, 118, 157, 132, LBLX, MBASE(2));
    DWIN_Frame_AreaCopy(1, 160, 118, 209, 132, LBLX, MBASE(3));
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 212, 118, 253, 131, LBLX, MBASE(4));
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Move"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_TitleCopy(1, 231, 2, 265, 12);                     // "Move"
    #endif
    draw_move_en(MBASE(1)); say_x(36, MBASE(1));                    // "Move X"
    draw_move_en(MBASE(2)); say_y(36, MBASE(2));                    // "Move Y"
    draw_move_en(MBASE(3)); say_z(36, MBASE(3));                    // "Move Z"
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 123, 192, 176, 202, LBLX, MBASE(4));   // "Extruder"
    #endif
  }

  Draw_Back_First(select_axis.now == 0);
  if (select_axis.now) Draw_Menu_Cursor(select_axis.now);

  // Draw separators and icons
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MoveX + i);
}

inline void Draw_ZTools_Menu() {
  Clear_Main_Window();
 

  if (HMI_flag.language_flag) {
    DWIN_Frame_AreaCopy(1, 256, 1, 271 - 38, 479 - 465, 14, 8);
    DWIN_Frame_AreaCopy(1, 58, 118, 271 - 165, 479 - 347, LBLX, MBASE(1));
    DWIN_Frame_AreaCopy(1, 109, 118, 271 - 114, 479 - 347, LBLX, MBASE(2));
    DWIN_Frame_AreaCopy(1, 160, 118, 271 - 62, 479 - 347, LBLX, MBASE(3));
    DWIN_Frame_AreaCopy(1, 212, 118, 271 - 18, 479 - 348, LBLX, MBASE(4));
	DWIN_Frame_AreaCopy(1, 256, 118, 271 - 18, 479 - 348, LBLX, MBASE(5));
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Z-Offset Menu"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_AreaCopy(1, 231, 2, 271 - 6, 479 - 467, 14, 8);
    #endif
	DWIN_Draw_String(true, true, font8x16, White, Background_black, LBLX, MBASE(1), (char*)"Level");
	Draw_Menu_Icon(1, ICON_PrintSize);
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(2), (char*)"Set closest Z");
	Draw_Menu_Icon(2, ICON_MoveZ);
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(3), (char*)"Babystep/offset up");
	Draw_Menu_Icon(3, ICON_SetHome);
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(4), (char*)"Babystep/offset down");
	Draw_Menu_Icon(4, ICON_SetHome);
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(5), (char*)"Save Offset");
	Draw_Menu_Icon(5, ICON_WriteEEPROM);
  }

  Draw_Back_First(select_zbox.now == 0);
  if (select_zbox.now) Draw_Menu_Cursor(select_zbox.now);
  LOOP_L_N(i, MROWS) Draw_Menu_Line(i + 1);
}

/* Prepare */
void HMI_Prepare() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  
    #if HAS_HOTEND
    // popup window resume
    if (HMI_flag.ETempTooLow_flag) {
      if (encoder_diffState == ENCODER_DIFF_ENTER) {
        HMI_flag.ETempTooLow_flag = 0;
        Draw_Prepare_Menu();
        DWIN_UpdateLCD();
      }
      return;
    }
  #endif

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
	  #ifdef PERSISTENT_MANUAL_LEVELING
	    int prepareItems = 9;
	  #else
		int prepareItems = 8;
	  #endif
    if (select_prepare.inc(prepareItems)) {
      if (select_prepare.now > MROWS && select_prepare.now > index_prepare) {
        index_prepare = select_prepare.now;

        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        Draw_Menu_Icon(MROWS, ICON_Axis + select_prepare.now - 1);

        // Draw "More" icon for sub-menus
        if (index_prepare < 7) Draw_More_Icon(MROWS - index_prepare + 1);

        #if HAS_HOTEND
          if (index_prepare == PREPARE_CASE_ABS) Item_Prepare_ABS(MROWS);
        #endif
        #if HAS_PREHEAT
          if (index_prepare == PREPARE_CASE_COOL) Item_Prepare_Cool(MROWS);
        #endif
        if (index_prepare == PREPARE_CASE_LANG) Item_Prepare_Lang(MROWS);
      }
      else {
        Move_Highlight(1, select_prepare.now + MROWS - index_prepare);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_prepare.dec()) {
      if (select_prepare.now < index_prepare - MROWS) {
        index_prepare--;
        Scroll_Menu(DWIN_SCROLL_DOWN);

        if (index_prepare == MROWS)
          Draw_Back_First();
        else
          Draw_Menu_Line(0, ICON_Axis + select_prepare.now - 1);

        if (index_prepare < 7) Draw_More_Icon(MROWS - index_prepare + 1);

             if (index_prepare == 6) Item_Prepare_Move(0);
        else if (index_prepare == 7) Item_Prepare_Disable(0);
        else if (index_prepare == 8) Item_Prepare_Home(0);
      }
      else {
        Move_Highlight(-1, select_prepare.now + MROWS - index_prepare);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_prepare.now) {
      case 0: // Back
        select_page.set(1);
        Goto_MainMenu();
        break;
      case PREPARE_CASE_MOVE: // Axis move
        checkkey = AxisMove;
        select_axis.reset();
        Draw_Move_Menu();

        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 216, MBASE(1), current_position.x * MINUNITMULT);
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 216, MBASE(2), current_position.y * MINUNITMULT);
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 216, MBASE(3), current_position.z * MINUNITMULT);
        #if HAS_HOTEND
          queue.inject_P(PSTR("G92 E0"));
          current_position.e = HMI_ValueStruct.Move_E_scale = 0;
          DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, 216, MBASE(4), 0);
        #endif
        break;
      case PREPARE_CASE_DISA: // Disable steppers
        queue.inject_P(PSTR("M84"));
		DWIN_Draw_String(false, false, font8x16, White, Background_black, 226, MBASE(2), (char*)"OK");
        break;
      case 3: // homing menu
		checkkey = HomeM;
        select_homeMenu.reset();
        Draw_Homing_Menu ();
        break;
      case 4: // Z-offset menu
        #ifdef MESH_BED_LEVELING
		  if (beenHomed == 0){
			levelM = 5;
			checkkey = Last_Prepare;
			beenHomed = 2;
			queue.inject_P(PSTR("G28"));
			Popup_Window_Home();
			break;
		  }
		  else{
		   checkkey = Mlevel;
		   levelM = 5;
           Tool = -1;
           Draw_Mlevel_Menu();
		  break;
		 }
		#elif HAS_BED_PROBE
		 checkkey = ZToolbox;
		 Tool = 1;
         select_zbox.reset();
         Draw_ZTools_Menu();
		 zOffTrack = probe.offset.z;
		 show_plus_or_minus(font8x16, Background_black, 3, 2, 216, MBASE(2), current_position[Z_AXIS] * MINUNITMULT * MINUNITMULT);
        #else		 
          // Apply workspace offset, making the current position 0,0,0
          queue.inject_P(PSTR("G92 X0 Y0 Z0"));
          buzzer.tone(100, 659);
          buzzer.tone(100, 698);
        #endif
        break;
	  case 5: // Preheat menu
        checkkey = PreHeats;
        select_pheat.reset();
		index_prepare = MROWS;
        Draw_Preheat_Menu();
        break;
      case 6: // cooldown
        thermalManager.zero_fan_speeds();
        thermalManager.disable_all_heaters();
        break;
      case 7: // filament change
		#ifdef PREVENT_COLD_EXTRUSION
              if (thermalManager.temp_hotend[0].celsius < EXTRUDE_MINTEMP) {
                HMI_flag.ETempTooLow_flag = 1;
                Popup_Window_ETempTooLow();
                DWIN_UpdateLCD();
                return;
              }
            #endif
		fillamentstep = 1;
        checkkey = FilamentChange;			
        Popup_Window_Filament_step1();
		queue.inject_P(PSTR("G92 E0\nG1 E-350 F1000\n"));
        DWIN_UpdateLCD();
        break;
      case 8: // language
        // select language
        HMI_flag.language_flag = !HMI_flag.language_flag;
        if (HMI_flag.language_flag) {
          set_chinese_to_eeprom();
          DWIN_JPG_CacheTo1(Language_Chinese);
        }
        else {
          set_english_to_eeprom();
          DWIN_JPG_CacheTo1(Language_English);
        }
        Draw_Prepare_Menu();
        break;
	  case 9: // Manual Level Menu
	    if (beenHomed == 0){
			levelM = 1;
			checkkey = Last_Prepare;
			beenHomed = 2;
			queue.inject_P(PSTR("G28"));
			Popup_Window_Home();
			break;
		}
		else{
		checkkey = Mlevel;
		levelM = 1;
        Tool = -1;
        Draw_Mlevel_Menu();
		break;
		}
      default:
        break;
    }
  }
  DWIN_UpdateLCD();
}

inline void Prepare_Item_nozzleTemp(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Nozzle Temperature");
	DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(row), thermalManager.temp_hotend[0].target);
    Draw_Menu_Line(row, ICON_SetEndTemp);
}

inline void Prepare_Item_bedTemp(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Bed Temperature");
	DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(row), thermalManager.temp_bed.target);
    Draw_Menu_Line(row, ICON_SetBedTemp);
}

inline void Prepare_Item_FanSpeed(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)"Fan Speed");
	DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(row), thermalManager.fan_speed[0]);
    Draw_Menu_Line(row, ICON_FanSpeed);
}

inline void Prepare_Item_PLASettings(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)PREHEAT_1_LABEL);
    Draw_Menu_Line(row, ICON_Temperature);
	Draw_More_Icon(row);
}

inline void Prepare_Item_ABSSettings(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)PREHEAT_2_LABEL);
    Draw_Menu_Line(row, ICON_Temperature);
	Draw_More_Icon(row);
}

inline void Prepare_Item_Custom1Settings(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)PREHEAT_3_LABEL);
    Draw_Menu_Line(row, ICON_Temperature);
	Draw_More_Icon(row);
}

inline void Prepare_Item_Custom2Settings(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)PREHEAT_4_LABEL);
    Draw_Menu_Line(row, ICON_Temperature);
	Draw_More_Icon(row);
}

inline void Prepare_Item_Custom3Settings(const uint8_t row) {
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(row), (char*)PREHEAT_5_LABEL);
    Draw_Menu_Line(row, ICON_Temperature);
	Draw_More_Icon(row);
}

inline void Draw_Temperature_Menu() {
  Clear_Main_Window();

  const int16_t scroll = MROWS - index_prepare; // Scrolled-up lines
  #define PSCROL(L) (scroll + (L))
  #define PVISI(L)  WITHIN(PSCROL(L), 0, MROWS)

  if (HMI_flag.language_flag) {
    DWIN_Frame_AreaCopy(1, 133, 1, 271 - 111, 479 - 465 - 1, 14, 8); // "Prepare"
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Temperature Menu"); // TODO: GET_TEXT_F
    #else
      DWIN_Frame_AreaCopy(1, 178, 2, 271 - 42, 479 - 464 - 1, 14, 8); // "Prepare"
    #endif
  }

  if (PVISI(0)) Draw_Back_First(select_temp.now == 0);   // < Back
  if (PVISI(1)) Prepare_Item_nozzleTemp(PSCROL(1));       // Nozzle temp
  if (PVISI(2)) Prepare_Item_bedTemp(PSCROL(2));          // Bed temp
  if (PVISI(3)) Prepare_Item_FanSpeed(PSCROL(3));         // fan speed
  if (PVISI(4)) Prepare_Item_PLASettings(PSCROL(4));      // preheat pla
  if (PVISI(5)) Prepare_Item_ABSSettings(PSCROL(5));      // preheat abs
  if (PVISI(6)) Prepare_Item_Custom1Settings(PSCROL(6));  // preheat PETG
  if (PVISI(7)) Prepare_Item_Custom2Settings(PSCROL(7));  // Preheat Custom 2
  if (PVISI(8)) Prepare_Item_Custom3Settings(PSCROL(8));  // Preheat Custom 3

  if (select_temp.now) Draw_Menu_Cursor(PSCROL(select_temp.now));

  LOOP_L_N(i, MROWS) Draw_Menu_Line(i + 1);
}

//Control Menu
void HMI_Control(void) {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  
  if (HMI_flag.ETempTooLow_flag) {
      if (encoder_diffState == ENCODER_DIFF_ENTER) {
        HMI_flag.ETempTooLow_flag = 0;
        Draw_Control_Menu();
        DWIN_UpdateLCD();
      }
      return;
    }

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_control.inc(1 + CONTROL_CASE_TOTAL)) {
      if (select_control.now > MROWS && select_control.now > index_control) {
        index_control = select_control.now;

        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        Draw_Menu_Icon(MROWS, ICON_Axis + select_control.now - 1);

        //Draw "More" icon for sub-menus
        //if (index_control < 7) Draw_More_Icon(MROWS - index_control + 1);
		
		if (numLines == 7){
		  if (index_control == 6) Prepare_Item_BltouchMenu(MROWS);
		  if (index_control == 7) Prepare_Item_AltInfoMenu(MROWS);	
		}
		else {
	      if (index_control == 6) Prepare_Item_AltInfoMenu(MROWS);
		}
      }
      else {
        Move_Highlight(1, select_control.now + MROWS - index_control);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_control.dec()) {
      if (select_control.now < index_control - MROWS) {
        index_control--;
        Scroll_Menu(DWIN_SCROLL_DOWN);

        if (index_control == MROWS)
          Draw_Back_First();
        else
          Draw_Menu_Line(0, ICON_Axis + select_control.now - 1);

        //if (index_control < 7) Draw_More_Icon(MROWS - index_control + 1);

        if (index_control == 6) Prepare_Item_TempSetMenu(0);
      }
      else {
        Move_Highlight(-1, select_control.now + MROWS - index_control);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_control.now) {
      case 0: // Back
        select_page.set(2);
        Goto_MainMenu();
        break;
      case CONTROL_CASE_TEMP: // Temperature
        checkkey = TemperatureID;
        HMI_ValueStruct.show_mode = -1;
        select_temp.reset();
        Draw_Temperature_Menu();
        break;
      case 2: // Motion >
		checkkey = Motion;
        select_motion.reset();
        Draw_Motion_Menu();
        break;
      case 3: // store EEPROM
		if (settings.save()) {
          buzzer.tone(100, 659);
          buzzer.tone(100, 698);
        }
        else
          buzzer.tone(20, 440);
        break;
      case 4: // read EEPROM
		if (settings.load()) {
          buzzer.tone(100, 659);
          buzzer.tone(100, 698);
        }
        else {buzzer.tone(20, 440);}
        break;
	  case 5: // reset EEPROM
		settings.reset();
		HMI_flag.ETempTooLow_flag = 1;
        Popup_Window_Save();
        DWIN_UpdateLCD();
        buzzer.tone(100, 659);
        buzzer.tone(100, 698);
        break;
      case 6: // if bltouch menu ELSE info menu  
		#ifdef BLTOUCH
		checkkey = BltouchM;
        select_bltm.reset();
        Draw_BlTouch_Menu();
        break;
		#else
		  checkkey = Info;
          Draw_Info_Menu();
          break;
		#endif
      case 7: // info
        checkkey = Info;
        Draw_Info_Menu();
        break;
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

/* Leveling */
void HMI_Leveling(void) {
  Popup_Window_Leveling();
  DWIN_UpdateLCD();
  queue.inject_P(PSTR("G28O\nG29"));
}

/* manual level*/
void HMI_MlevelMenu(void) {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (encoder_diffState == ENCODER_DIFF_CW) {
	  int oldX = trackX;
	  int oldY = Tool;
	  if (tapFlag == 22){
			queue.inject_P(PSTR("G0 Z10"));
		}
	  if (even % 2 == 0){
		  trackX++;
		  even += 2;
	  }
	  tapFlag = 0;
	  //scrolling from back button to grid
	  if (Tool == -1){
		trackX = 0;
		trackY = 2;
		Tool = 0;
		if (levelM == 1){
		  even = 0;
		}
		DWIN_Draw_Rectangle(0, Background_black,  116,  40, 166,  80);
	  }
	    //scrolling from grid to back button
	  else if (trackX > 1 && Tool == 2){
		Tool = -1;
		even = -1;
		DWIN_Draw_Rectangle(0, White,  116,  40, 166,  80);		
	  }
	  //end of row array
	  else if (trackX > 1){
		trackX = 0;
		trackY --;
		Tool ++;
		if (even % 2 == 0 && Tool == 1){
		  trackX++;
		}
	  }
	  //next array cloumn
	  else {
		  trackX++;
	  }
	  if (oldY != -1){
		oldX = (oldX * 76) + 60;
		oldY = (oldY * 76) + 100;
		DWIN_Draw_Rectangle(1, Rectangle_Color,  oldX,  oldY,  oldX + 10, oldY + 10);  
	  }
	  if (Tool != -1){
		oldX = (trackX * 76) + 60;
		oldY = (Tool * 76) + 100;
		DWIN_Draw_Rectangle(1, White,  oldX,  oldY,  oldX + 10, oldY + 10);    
	  }
	  delay(25);
	  DWIN_UpdateLCD();
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW) {
      int oldX = trackX;
	  int oldY = Tool;
	  if (tapFlag == 22){
			queue.inject_P(PSTR("G0 Z10"));
		}
	  if (even % 2 == 0){
		trackX--;
		even -= 2;
	  }
	  tapFlag = 0;
	  //scrolling from back button to grid
	  if (Tool == -1){
		trackX = 2;
		trackY = 0;
		Tool = 2;
		if (levelM == 1){
		  even = 8;
		}
		DWIN_Draw_Rectangle(0, Background_black,  116,  40, 166,  80);
	  }
	  //scrolling from grid to back button
	  else if (trackX < 1 && Tool == 0){
		Tool = -1;
		even = -1;
		DWIN_Draw_Rectangle(0, White,  116,  40, 166,  80);		
	  }
	  //end of row array
	  else if (trackX < 1){
		trackX = 2;
		trackY ++;
		Tool --;
		if (even % 2 == 0 && Tool == 1){
		  trackX--;
		}
	  }
	  //next array cloumn
	  else {
		  trackX--;
	  }
	  if (oldY != -1){
		oldX = (oldX * 76) + 60;
		oldY = (oldY * 76) + 100;
		DWIN_Draw_Rectangle(1, Rectangle_Color,  oldX,  oldY,  oldX + 10, oldY + 10);  
	  }
	  if (Tool != -1){
		oldX = (trackX *76) + 60;
		oldY = (Tool * 76) + 100;
		DWIN_Draw_Rectangle(1, White,  oldX,  oldY,  oldX + 10, oldY + 10);    
	  }
	  delay(25);
	  DWIN_UpdateLCD();
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) {
	  if (Tool == -1){
		if (mainFlag == 1){
		  Tool = 0;
		  levelM = 0;
		  mainFlag = 0;
		  select_page.set(3);
          Goto_MainMenu();
		  DWIN_UpdateLCD();
		  return;
        }
		else{
	      checkkey = Prepare;
		  if (levelM == 1){
           select_prepare.set(9);
		  }
		  if (levelM == 5){
           select_prepare.set(4);
		  }
	      Tool = 0;
	      levelM = 0;
          Draw_Prepare_Menu();
	      //settings.save();
	      DWIN_UpdateLCD();
		  return;
		}
	  }
	  else {
		//second press for mesh menu
		if (levelM != 1 && tapFlag == 22){
	       checkkey = Meshoffset;
		   oldZ = UBLZPOS;
		   show_plus_or_minus(font8x16, Select_Color, 1, 3, 160, 299, UBLZPOS);
		}
		//first press sets flag for second press when in the mesh menu
		else{
	      tapFlag = 22;
	      //move to coordinate
	      char cmd[40];
          cmd[0] = '\0';
          sprintf_P(cmd, PSTR("G0 X%f Y%f\nG0 Z0\n"), UBLXPOS/10, UBLYPOS/10)  ;
          queue.inject(cmd);
		}
	    DWIN_UpdateLCD();
		return;
	  }
    }
	
	if (Tool == -1){
	  UBLYPOS = 0;
	  UBLXPOS = 0;
      UBLZPOS = 0;
	}
	else{
	  if (levelM == 1){
		//coordinate for Manual level menu
		int xEdge = 0;
		int yEdge = 0;
		if (trackX == 0){
			xEdge = 24;
		}
		if (trackX == 2){
			xEdge = -24;
		}
		if (trackY == 0){
			yEdge = 24;
		}
		if (trackY == 2){
			yEdge = -24;
		}
	    UBLXPOS = (((X_BED_SIZE/2) * trackX) + xEdge) * MINUNITMULT;
		UBLYPOS = (((Y_BED_SIZE/2) * trackY) + yEdge) * MINUNITMULT;
	  }
	  #ifdef MESH_BED_LEVELING
	  else{
		//coordinate for Manual Mesh level menu
		UBLYPOS = _GET_MESH_Y(trackY) * 10;
	    UBLXPOS = _GET_MESH_X(trackX) * 10;
        UBLZPOS = (mbl.z_values [trackX][trackY]) * 1000;	
	  }
	  #endif	
	}
	if (levelM != 1){ 
	  show_plus_or_minus(font8x16, Background_black, 1, 3, 160, 299, UBLZPOS);
	}
	DWIN_Draw_FloatValue(true, true, 0, font8x16, White, Background_black, 3, 1, 96, 329, UBLXPOS);
	DWIN_Draw_FloatValue(true, true, 0, font8x16, White, Background_black, 3, 1, 205, 329, UBLYPOS);
	DWIN_UpdateLCD();
	return;
  }
}

/* Info */
void HMI_Filament(void) {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (fillamentstep == 1){
	  Popup_Window_Filament_step2();
	  queue.inject_P(PSTR("G92 E0\nG1 E400 F1000\n"));
	  fillamentstep = 2;
      DWIN_UpdateLCD();
	  return;
	}
	else if (fillamentstep == 2){
		checkkey = Prepare;
        select_prepare.set(7);;
        Draw_Prepare_Menu();
        fillamentstep = 0;
		queue.inject_P(PSTR("G92 E0"));
		thermalManager.zero_fan_speeds();
        thermalManager.disable_all_heaters();
        DWIN_UpdateLCD();
        return;		
	}
	return;
  }
  DWIN_UpdateLCD();
}

/* Axis Move */
void HMI_AxisMove() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  #if ENABLED(PREVENT_COLD_EXTRUSION)
    // popup window resume
    if (HMI_flag.ETempTooLow_flag) {
      if (encoder_diffState == ENCODER_DIFF_ENTER) {
        HMI_flag.ETempTooLow_flag = false;
        current_position.e = HMI_ValueStruct.Move_E_scale = 0;
		#ifdef RELATIVE_MODE
		  show_plus_or_minus(font8x16, Background_black, 3, 1, 216, MBASE(1), 0);
          show_plus_or_minus(font8x16, Background_black, 3, 1, 216, MBASE(2), 0);
          show_plus_or_minus(font8x16, Background_black, 3, 1, 216, MBASE(3), 0);
		#else
          DWIN_Draw_FloatValue(true, true, 0, font8x16, White, Background_black, 3, 1, 216, MBASE(1), HMI_ValueStruct.Move_X_scale);
          DWIN_Draw_FloatValue(true, true, 0, font8x16, White, Background_black, 3, 1, 216, MBASE(2), HMI_ValueStruct.Move_Y_scale);
          DWIN_Draw_FloatValue(true, true, 0, font8x16, White, Background_black, 3, 1, 216, MBASE(3), HMI_ValueStruct.Move_Z_scale);
		#endif
        show_plus_or_minus(font8x16, Background_black, 3, 1, 216, MBASE(4), HMI_ValueStruct.Move_E_scale);
        DWIN_UpdateLCD();
      }
      return;
    }
  #endif

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_axis.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_axis.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_axis.dec()) Move_Highlight(-1, select_axis.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_axis.now) {
      case 0: // Back
        checkkey = Prepare;
        select_prepare.set(1);
        index_prepare = MROWS;
        Draw_Prepare_Menu();
        break;
      case 1: // X axis move
        checkkey = Move_X;
		#ifdef RELATIVE_MODE
		  HMI_ValueStruct.Move_X_scale = 0;
		  holdPrevious = current_position[X_AXIS] * MINUNITMULT;
		  noMoreDiff = ((X_MAX_POS * MINUNITMULT) - holdPrevious);//up limit
	      noLessDiff = (((X_MAX_POS * MINUNITMULT) - noMoreDiff) * -1); //down limit
		  if (beenHomed == 0){
			  noMoreDiff = (X_MAX_POS * MINUNITMULT);
			  noLessDiff = (X_MAX_POS * MINUNITMULT) * -1;
		  }
		  show_plus_or_minus(font8x16, Select_Color, 3, 1, 216, MBASE(1), 0);
		#else
          HMI_ValueStruct.Move_X_scale = current_position[X_AXIS] * MINUNITMULT;
          DWIN_Draw_FloatValue(true, true, 0, font8x16, White, Select_Color, 3, 1, 216, MBASE(1), HMI_ValueStruct.Move_X_scale);
		#endif
        EncoderRate.encoderRateEnabled = 1;
        break;
      case 2: // Y axis move
        checkkey = Move_Y;
		#ifdef RELATIVE_MODE
		  HMI_ValueStruct.Move_Y_scale = 0;
		  holdPrevious = current_position[Y_AXIS] * MINUNITMULT;
		  noMoreDiff = ((Y_MAX_POS * MINUNITMULT) - holdPrevious);//up limit
	      noLessDiff = (((Y_MAX_POS * MINUNITMULT) - noMoreDiff) * -1); //down limit
		  if (beenHomed == 0){
			  noMoreDiff = (Y_MAX_POS * MINUNITMULT);
			  noLessDiff = (Y_MAX_POS * MINUNITMULT) * -1;
		  }
		show_plus_or_minus(font8x16, Select_Color, 3, 1, 216, MBASE(2), 0);
		#else
          HMI_ValueStruct.Move_Y_scale = current_position[Y_AXIS] * MINUNITMULT;
          DWIN_Draw_FloatValue(true, true, 0, font8x16, White, Select_Color, 3, 1, 216, MBASE(2), HMI_ValueStruct.Move_Y_scale);
		#endif
        EncoderRate.encoderRateEnabled = 1;
        break;
      case 3: // Z axis move
        checkkey = Move_Z;
		#ifdef RELATIVE_MODE
		  HMI_ValueStruct.Move_Z_scale = 0;
		  holdPrevious = current_position[Z_AXIS] * MINUNITMULT;
		  noMoreDiff = ((Z_MAX_POS * MINUNITMULT) - holdPrevious);//up limit
	      noLessDiff = (((Z_MAX_POS * MINUNITMULT) - noMoreDiff) * -1); //down limit
		  if (beenHomed == 0){
			  noMoreDiff = (Z_MAX_POS * MINUNITMULT);
			  noLessDiff = (Z_MAX_POS * MINUNITMULT) * -1;
		  }
		show_plus_or_minus(font8x16, Select_Color, 3, 1, 216, MBASE(3), 0);
		#else
          HMI_ValueStruct.Move_Z_scale = current_position[Z_AXIS] * MINUNITMULT;
          DWIN_Draw_FloatValue(true, true, 0, font8x16, White, Select_Color, 3, 1, 216, MBASE(3), HMI_ValueStruct.Move_Z_scale);
		#endif
        EncoderRate.encoderRateEnabled = 1;
        break;
      #if HAS_HOTEND
          case 4: // Extruder
            // window tips
            #ifdef PREVENT_COLD_EXTRUSION
              if (thermalManager.temp_hotend[0].celsius < EXTRUDE_MINTEMP) {
                HMI_flag.ETempTooLow_flag = true;
                Popup_Window_ETempTooLow();
                DWIN_UpdateLCD();
                return;
              }
            #endif
              checkkey = Extruder;
			  //set extruder back to zero position
	     	  queue.inject_P(PSTR("G92 E0"));
		      HMI_ValueStruct.Move_E_scale = 0;
		      show_plus_or_minus(font8x16, Select_Color, 4, 1, 216, MBASE(4), HMI_ValueStruct.Move_E_scale);
		      EncoderRate.encoderRateEnabled = 1;
            break;
      #endif
    }
  }
  DWIN_UpdateLCD();
}

/* Z toolbox */
void HMI_ZToolbox(void) {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_zbox.inc(5)) Move_Highlight(1, select_zbox.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_zbox.dec()) Move_Highlight(-1, select_zbox.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_zbox.now) {
      case 0: // back
        checkkey = Prepare;
		Tool = 0;
        select_prepare.set(4);
        index_prepare = MROWS;
        Draw_Prepare_Menu();
        break;
      case 1:// G29 level and home if not already homed
		queue.inject_P(PSTR("G28O\nG29\nG0 Z10\nG0 X110 Y110\nG0 Z0"));
		Popup_Window_Leveling();
        break;
      case 2: // move Z
		checkkey = Homeoffset;
		//gets current position then multiplies by 10 to set proper value for DWIN_Draw function
        HMI_ValueStruct.Move_Z_scale = current_position[Z_AXIS] * MINUNITMULT;
		holdPrevious = HMI_ValueStruct.Move_Z_scale;
		//the number that show when you first push the knob to change the value -- enter menu  (number must be multiplied by 100)
        show_plus_or_minus(font8x16, Select_Color, 3, 2, 216, MBASE(2), HMI_ValueStruct.Move_Z_scale * MINUNITMULT);
        EncoderRate.encoderRateEnabled = 1;
        break;
      case 3:  // babystep up 0.01mm + zoffset if set
        queue.inject_P(PSTR("M290 Z0.01"));
		delay(500);
		break;
	  case 4:  // babystep down 0.01mm + zoffset if set
        queue.inject_P(PSTR("M290 Z-0.01"));
		delay(500);
		break;
	  case 5:  //Save z-offset
		if (settings.save()) {
          buzzer.tone(100, 659);
          buzzer.tone(100, 698);
        }
        else
          buzzer.tone(20, 440);
        break;
 
    }
  }
  DWIN_UpdateLCD();
}


/* Preheats menu*/
void HMI_PreHeats(void) {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_pheat.inc(6)) {
      if (select_pheat.now > MROWS && select_pheat.now > index_prepare) {
        index_prepare = select_pheat.now;

        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        Draw_Menu_Icon(MROWS, ICON_Axis + select_pheat.now - 1);

        //Draw "More" icon for sub-menus
        //if (index_prepare < 7) Draw_More_Icon(MROWS - index_prepare + 1);

        if (index_prepare == 6) Prepare_Item_Custom3(MROWS);
      }
      else {
        Move_Highlight(1, select_pheat.now + MROWS - index_prepare);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_pheat.dec()) {
      if (select_pheat.now < index_prepare - MROWS) {
        index_prepare--;
        Scroll_Menu(DWIN_SCROLL_DOWN);

        if (index_prepare == MROWS)
          Draw_Back_First();
        else
          Draw_Menu_Line(0, ICON_Axis + select_pheat.now - 1);

        //if (index_prepare < 7) Draw_More_Icon(MROWS - index_prepare + 1);

        if (index_prepare == 6) Prepare_Item_Bed(0);
      }
      else {
        Move_Highlight(-1, select_pheat.now + MROWS - index_prepare);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_pheat.now) {
      case 0: // back
        checkkey = Prepare;
        select_prepare.set(5);
        index_prepare = MROWS;
		mat = 9;
        Draw_Prepare_Menu();
        break;
      case 1: // preheat bed only
	    if (mat == 9) {
		mat = 0;
		}
		thermalManager.disable_all_heaters();
        thermalManager.setTargetBed(ui.material_preset[mat].bed_temp);
        thermalManager.set_fan_speed(0, ui.material_preset[mat].fan_speed);
        break;
      case 2: // preheat PLA
		mat = 0;
        thermalManager.setTargetHotend(ui.material_preset[mat].hotend_temp, 0);
        thermalManager.setTargetBed(ui.material_preset[mat].bed_temp);
        thermalManager.set_fan_speed(0, ui.material_preset[mat].fan_speed);
        break;
      case 3: // preheat ABS
		mat = 1;
        thermalManager.setTargetHotend(ui.material_preset[mat].hotend_temp, 0);
        thermalManager.setTargetBed(ui.material_preset[mat].bed_temp);
        thermalManager.set_fan_speed(0, ui.material_preset[mat].fan_speed);
        break;
	  case 4: // preheat PETG
		mat = 2;
        thermalManager.setTargetHotend(ui.material_preset[mat].hotend_temp, 0);
        thermalManager.setTargetBed(ui.material_preset[mat].bed_temp);
        thermalManager.set_fan_speed(0, ui.material_preset[mat].fan_speed);
        break;
      case 5: // preheat custom 2
		mat = 3;
        thermalManager.setTargetHotend(ui.material_preset[mat].hotend_temp, 0);
        thermalManager.setTargetBed(ui.material_preset[mat].bed_temp);
        thermalManager.set_fan_speed(0, ui.material_preset[mat].fan_speed);
        break;
      case 6: // preheat custom 3
		mat = 4;
        thermalManager.setTargetHotend(ui.material_preset[mat].hotend_temp, 0);
        thermalManager.setTargetBed(ui.material_preset[mat].bed_temp);
        thermalManager.set_fan_speed(0, ui.material_preset[mat].fan_speed);
        break;
		
      default:
        break;
    }
  }
  DWIN_UpdateLCD();
}

/* temp settings menu*/
void HMI_Temperature(void) {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_temp.inc(8)) {
      if (select_temp.now > MROWS && select_temp.now > index_prepare) {
        index_prepare = select_temp.now;

        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        Draw_Menu_Icon(MROWS, ICON_Axis + select_temp.now - 1);

        //Draw "More" icon for sub-menus
        //if (index_prepare < 7) Draw_More_Icon(MROWS - index_prepare + 1);

        if (index_prepare == 6) Prepare_Item_Custom1Settings(MROWS);
		if (index_prepare == 7) Prepare_Item_Custom2Settings(MROWS);
		if (index_prepare == 8) Prepare_Item_Custom3Settings(MROWS);
      }
      else {
        Move_Highlight(1, select_temp.now + MROWS - index_prepare);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_temp.dec()) {
      if (select_temp.now < index_prepare - MROWS) {
        index_prepare--;
        Scroll_Menu(DWIN_SCROLL_DOWN);

        if (index_prepare == MROWS)
          Draw_Back_First();
        else
          Draw_Menu_Line(0, ICON_Axis + select_temp.now - 1);

        //if (index_prepare < 7) Draw_More_Icon(MROWS - index_prepare + 1);

        if (index_prepare == 6) Prepare_Item_nozzleTemp(0);
		if (index_prepare == 7) Prepare_Item_bedTemp(0);
      }
      else {
        Move_Highlight(-1, select_temp.now + MROWS - index_prepare);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_temp.now) {
      case 0: // back
        checkkey = Control;
			select_control.set(1);
			index_control = MROWS;
			mat = 9;
			Draw_Control_Menu();
			break;
	#if HAS_HOTEND
        case 1: // nozzle temperature
			checkkey = ETemp;
			HMI_ValueStruct.E_Temp = thermalManager.temp_hotend[0].target;
			DWIN_Draw_IntValue(true, true, 0, font8x16, White, Select_Color, 3, 216, MBASE(1), thermalManager.temp_hotend[0].target);
			EncoderRate.encoderRateEnabled = 1;
			break;
    #endif
    #if HAS_HEATED_BED
        case 2: // bed temperature
			checkkey = BedTemp;
			HMI_ValueStruct.Bed_Temp = thermalManager.temp_bed.target;
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Select_Color, 3, 216, MBASE(2), thermalManager.temp_bed.target);
            EncoderRate.encoderRateEnabled = 1;
            break;
    #endif
    #if HAS_FAN
        case 3: // fan speed
            checkkey = FanSpeed;
            HMI_ValueStruct.Fan_speed = thermalManager.fan_speed[0];
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Select_Color, 3, 216, MBASE(3), thermalManager.fan_speed[0]);
            EncoderRate.encoderRateEnabled = 1;
            break;
    #endif
      case 4: // preheat PLA
		checkkey = Preheat;
            select_ABS.reset();
            HMI_ValueStruct.show_mode = -3;
			mat = 0;

            Clear_Main_Window();

            if (HMI_flag.language_flag) {
              DWIN_Frame_AreaCopy(1, 142, 16, 271 - 48, 479 - 450, 14, 8);

              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(1));
              DWIN_Frame_AreaCopy(1, 1, 134, 271 - 215, 479 - 333, LBLX + 24, MBASE(1)); // ABS nozzle temp
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(2));
              DWIN_Frame_AreaCopy(1, 58, 134, 271 - 158, 479 - 333, LBLX + 24, MBASE(2)); // ABS bed temp
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(3));
              DWIN_Frame_AreaCopy(1, 115, 134, 271 - 101, 479 - 333, LBLX + 24, MBASE(3)); // ABS fan speed
              DWIN_Frame_AreaCopy(1, 72, 148, 271 - 120, 479 - 317, LBLX, MBASE(4));
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX + 28, MBASE(4) + 2); // save ABS configuration
            }
            else {
              #ifdef USE_STRING_HEADINGS
                Draw_Title("PLA Settings"); // TODO: GET_TEXT_F
              #else
                DWIN_Frame_AreaCopy(1, 56, 16, 271 - 130, 479 - 450 - 1, 14, 8);
              #endif

              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(1));
              DWIN_Frame_AreaCopy(1, 197, 104, 271 - 33, 479 - 365, LBLX + 24 + 3, MBASE(1));
              DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 41 + 6, MBASE(1)); // ABS nozzle temp
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(2) + 3);
              DWIN_Frame_AreaCopy(1, 240, 104, 271 - 7, 479 - 365, LBLX + 24 + 3, MBASE(2) + 3);
              DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 24 + 6, MBASE(2) + 3); // ABS bed temp
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(3));
              DWIN_Frame_AreaCopy(1, 0, 119, 271 - 207, 479 - 347, LBLX + 24 + 3, MBASE(3)); // ABS fan speed
              DWIN_Frame_AreaCopy(1, 97, 165, 271 - 42, 479 - 301 - 1, LBLX, MBASE(4));
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX + 33, MBASE(4));  // save ABS configuration
            }

          Draw_Back_First();

          uint8_t i = 0;
          Draw_Menu_Line(++i, ICON_SetEndTemp);
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[0].hotend_temp);
          #if HAS_HEATED_BED
            Draw_Menu_Line(++i, ICON_SetBedTemp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[0].bed_temp);
          #endif
          #if HAS_FAN
            Draw_Menu_Line(++i, ICON_FanSpeed);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[0].fan_speed);
          #endif
          #if ENABLED(EEPROM_SETTINGS)
            Draw_Menu_Line(++i, ICON_WriteEEPROM);
          #endif
        } break;

            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(1), ui.material_preset[mat].hotend_temp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(2), ui.material_preset[mat].bed_temp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(3), ui.material_preset[mat].fan_speed);

            break;
	  case 5: // preheat ABS
		checkkey = Preheat;
            select_ABS.reset();
            HMI_ValueStruct.show_mode = -3;
			mat = 1;

            Clear_Main_Window();

            if (HMI_flag.language_flag) {
              DWIN_Frame_AreaCopy(1, 142, 16, 271 - 48, 479 - 450, 14, 8);

              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(1));
              DWIN_Frame_AreaCopy(1, 1, 134, 271 - 215, 479 - 333, LBLX + 24, MBASE(1)); // ABS nozzle temp
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(2));
              DWIN_Frame_AreaCopy(1, 58, 134, 271 - 158, 479 - 333, LBLX + 24, MBASE(2)); // ABS bed temp
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(3));
              DWIN_Frame_AreaCopy(1, 115, 134, 271 - 101, 479 - 333, LBLX + 24, MBASE(3)); // ABS fan speed
              DWIN_Frame_AreaCopy(1, 72, 148, 271 - 120, 479 - 317, LBLX, MBASE(4));
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX + 28, MBASE(4) + 2); // save ABS configuration
            }
            else {
              #ifdef USE_STRING_HEADINGS
                Draw_Title("ABS Settings"); // TODO: GET_TEXT_F
              #else
                DWIN_Frame_AreaCopy(1, 56, 16, 271 - 130, 479 - 450 - 1, 14, 8);
              #endif

              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(1));
              DWIN_Frame_AreaCopy(1, 197, 104, 271 - 33, 479 - 365, LBLX + 24 + 3, MBASE(1));
              DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 41 + 6, MBASE(1)); // ABS nozzle temp
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(2) + 3);
              DWIN_Frame_AreaCopy(1, 240, 104, 271 - 7, 479 - 365, LBLX + 24 + 3, MBASE(2) + 3);
              DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 24 + 6, MBASE(2) + 3); // ABS bed temp
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(3));
              DWIN_Frame_AreaCopy(1, 0, 119, 271 - 207, 479 - 347, LBLX + 24 + 3, MBASE(3)); // ABS fan speed
              DWIN_Frame_AreaCopy(1, 97, 165, 271 - 42, 479 - 301 - 1, LBLX, MBASE(4));
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX + 33, MBASE(4));  // save ABS configuration
            }

          Draw_Back_First();

          uint8_t i = 0;
          Draw_Menu_Line(++i, ICON_SetEndTemp);
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[1].hotend_temp);
          #if HAS_HEATED_BED
            Draw_Menu_Line(++i, ICON_SetBedTemp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[1].bed_temp);
          #endif
          #if HAS_FAN
            Draw_Menu_Line(++i, ICON_FanSpeed);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 216, MBASE(i), ui.material_preset[1].fan_speed);
          #endif
          #if ENABLED(EEPROM_SETTINGS)
            Draw_Menu_Line(++i, ICON_WriteEEPROM);
          #endif

            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(1), ui.material_preset[mat].hotend_temp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(2), ui.material_preset[mat].bed_temp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(3), ui.material_preset[mat].fan_speed);

            break;
      case 6: // preheat PETG
		checkkey = Preheat;
            select_ABS.reset();
            HMI_ValueStruct.show_mode = -3;
			mat = 2;

            Clear_Main_Window();

            if (HMI_flag.language_flag) {
              DWIN_Frame_AreaCopy(1, 142, 16, 271 - 48, 479 - 450, 14, 8);

              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(1));
              DWIN_Frame_AreaCopy(1, 1, 134, 271 - 215, 479 - 333, LBLX + 24, MBASE(1)); // PETG nozzle temp
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(2));
              DWIN_Frame_AreaCopy(1, 58, 134, 271 - 158, 479 - 333, LBLX + 24, MBASE(2)); // PETG bed temp
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(3));
              DWIN_Frame_AreaCopy(1, 115, 134, 271 - 101, 479 - 333, LBLX + 24, MBASE(3)); // PETG fan speed
              DWIN_Frame_AreaCopy(1, 72, 148, 271 - 120, 479 - 317, LBLX, MBASE(4));
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX + 28, MBASE(4) + 2); // save PETG configuration
            }
            else {
              #ifdef USE_STRING_HEADINGS
                Draw_Title("PETG Settings"); // TODO: GET_TEXT_F
              #else
                DWIN_Frame_AreaCopy(1, 56, 16, 271 - 130, 479 - 450 - 1, 14, 8);
              #endif

              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(1));
              DWIN_Frame_AreaCopy(1, 197, 104, 271 - 33, 479 - 365, LBLX + 24 + 3, MBASE(1));
              DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 41 + 6, MBASE(1)); // PETG nozzle temp
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(2) + 3);
              DWIN_Frame_AreaCopy(1, 240, 104, 271 - 7, 479 - 365, LBLX + 24 + 3, MBASE(2) + 3);
              DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 24 + 6, MBASE(2) + 3); // PETG bed temp
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(3));
              DWIN_Frame_AreaCopy(1, 0, 119, 271 - 207, 479 - 347, LBLX + 24 + 3, MBASE(3)); // PETG fan speed
              DWIN_Frame_AreaCopy(1, 97, 165, 271 - 42, 479 - 301 - 1, LBLX, MBASE(4));
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX + 33, MBASE(4));  // save PETG configuration
            }

            Draw_Back_First();

            Draw_Menu_Line(1, ICON_SetEndTemp);
            Draw_Menu_Line(2, ICON_SetBedTemp);
            Draw_Menu_Line(3, ICON_FanSpeed);
            Draw_Menu_Line(4, ICON_WriteEEPROM);

            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(1), ui.material_preset[mat].hotend_temp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(2), ui.material_preset[mat].bed_temp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(3), ui.material_preset[mat].fan_speed);

            break;
      case 7: // preheat custom 2
		checkkey = Preheat;
            select_ABS.reset();
            HMI_ValueStruct.show_mode = -3;
			mat = 3;

            Clear_Main_Window();

            if (HMI_flag.language_flag) {
              DWIN_Frame_AreaCopy(1, 142, 16, 271 - 48, 479 - 450, 14, 8);

              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(1));
              DWIN_Frame_AreaCopy(1, 1, 134, 271 - 215, 479 - 333, LBLX + 24, MBASE(1)); // custom 2 nozzle temp
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(2));
              DWIN_Frame_AreaCopy(1, 58, 134, 271 - 158, 479 - 333, LBLX + 24, MBASE(2)); // custom 2 bed temp
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(3));
              DWIN_Frame_AreaCopy(1, 115, 134, 271 - 101, 479 - 333, LBLX + 24, MBASE(3)); // custom 2 fan speed
              DWIN_Frame_AreaCopy(1, 72, 148, 271 - 120, 479 - 317, LBLX, MBASE(4));
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX + 28, MBASE(4) + 2); // save custom 2 configuration
            }
            else {
              #ifdef USE_STRING_HEADINGS
                Draw_Title("Custom 2 Settings"); // TODO: GET_TEXT_F
              #else
                DWIN_Frame_AreaCopy(1, 56, 16, 271 - 130, 479 - 450 - 1, 14, 8);
              #endif

              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(1));
              DWIN_Frame_AreaCopy(1, 197, 104, 271 - 33, 479 - 365, LBLX + 24 + 3, MBASE(1));
              DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 41 + 6, MBASE(1)); // custom 2 nozzle temp
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(2) + 3);
              DWIN_Frame_AreaCopy(1, 240, 104, 271 - 7, 479 - 365, LBLX + 24 + 3, MBASE(2) + 3);
              DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 24 + 6, MBASE(2) + 3); // custom 2 bed temp
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(3));
              DWIN_Frame_AreaCopy(1, 0, 119, 271 - 207, 479 - 347, LBLX + 24 + 3, MBASE(3)); // custom 2 fan speed
              DWIN_Frame_AreaCopy(1, 97, 165, 271 - 42, 479 - 301 - 1, LBLX, MBASE(4));
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX + 33, MBASE(4));  // save custom 2 configuration
            }

            Draw_Back_First();

            Draw_Menu_Line(1, ICON_SetEndTemp);
            Draw_Menu_Line(2, ICON_SetBedTemp);
            Draw_Menu_Line(3, ICON_FanSpeed);
            Draw_Menu_Line(4, ICON_WriteEEPROM);

            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(1), ui.material_preset[mat].hotend_temp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(2), ui.material_preset[mat].bed_temp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(3), ui.material_preset[mat].fan_speed);

            break;
      case 8: // preheat custom 3
		checkkey = Preheat;
            select_ABS.reset();
            HMI_ValueStruct.show_mode = -3;
			mat = 4;

            Clear_Main_Window();

            if (HMI_flag.language_flag) {
              DWIN_Frame_AreaCopy(1, 142, 16, 271 - 48, 479 - 450, 14, 8);

              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(1));
              DWIN_Frame_AreaCopy(1, 1, 134, 271 - 215, 479 - 333, LBLX + 24, MBASE(1)); // custom 3 nozzle temp
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(2));
              DWIN_Frame_AreaCopy(1, 58, 134, 271 - 158, 479 - 333, LBLX + 24, MBASE(2)); // custom 3 bed temp
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX, MBASE(3));
              DWIN_Frame_AreaCopy(1, 115, 134, 271 - 101, 479 - 333, LBLX + 24, MBASE(3)); // custom 3 fan speed
              DWIN_Frame_AreaCopy(1, 72, 148, 271 - 120, 479 - 317, LBLX, MBASE(4));
              DWIN_Frame_AreaCopy(1, 180, 89, 204, 479 - 379, LBLX + 28, MBASE(4) + 2); // save custom 3 configuration
            }
            else {
              #ifdef USE_STRING_HEADINGS
                Draw_Title("Custom 3 Settings"); // TODO: GET_TEXT_F
              #else
                DWIN_Frame_AreaCopy(1, 56, 16, 271 - 130, 479 - 450 - 1, 14, 8);
              #endif

              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(1));
              DWIN_Frame_AreaCopy(1, 197, 104, 271 - 33, 479 - 365, LBLX + 24 + 3, MBASE(1));
              DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 41 + 6, MBASE(1)); // custom 3 nozzle temp
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(2) + 3);
              DWIN_Frame_AreaCopy(1, 240, 104, 271 - 7, 479 - 365, LBLX + 24 + 3, MBASE(2) + 3);
              DWIN_Frame_AreaCopy(1, 1, 89, 271 - 188, 479 - 377 - 1, LBLX + 24 + 24 + 6, MBASE(2) + 3); // custom 3 bed temp
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX, MBASE(3));
              DWIN_Frame_AreaCopy(1, 0, 119, 271 - 207, 479 - 347, LBLX + 24 + 3, MBASE(3)); // custom 3 fan speed
              DWIN_Frame_AreaCopy(1, 97, 165, 271 - 42, 479 - 301 - 1, LBLX, MBASE(4));
              DWIN_Frame_AreaCopy(1, 172, 76, 198, 479 - 393, LBLX + 33, MBASE(4));  // save custom 3 configuration
            }

            Draw_Back_First();

            Draw_Menu_Line(1, ICON_SetEndTemp);
            Draw_Menu_Line(2, ICON_SetBedTemp);
            Draw_Menu_Line(3, ICON_FanSpeed);
            Draw_Menu_Line(4, ICON_WriteEEPROM);

            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(1), ui.material_preset[mat].hotend_temp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(2), ui.material_preset[mat].bed_temp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Background_black, 3, 216, MBASE(3), ui.material_preset[mat].fan_speed);

            break;
		
      default:
        break;
    }
  }
  DWIN_UpdateLCD();
}

inline void Draw_Max_Speed_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Max Speed (mm/s)"

    auto say_max_speed = [](const uint16_t row) {
      DWIN_Frame_AreaCopy(1, 173, 133, 228, 147, LBLX, row);              // "Max speed"
    };

    say_max_speed(MBASE(1));                                              // "Max speed"
    DWIN_Frame_AreaCopy(1, 229, 133, 236, 147, LBLX + 58, MBASE(1));      // X
    say_max_speed(MBASE(2));                                              // "Max speed"
    DWIN_Frame_AreaCopy(1, 1, 150, 7, 160, LBLX + 58, MBASE(2) + 3);      // Y
    say_max_speed(MBASE(3));                                              // "Max speed"
    DWIN_Frame_AreaCopy(1, 9, 150, 16, 160, LBLX + 58, MBASE(3) + 3);     // Z
    #if HAS_HOTEND
      say_max_speed(MBASE(4));                                            // "Max speed"
      DWIN_Frame_AreaCopy(1, 18, 150, 25, 160, LBLX + 58, MBASE(4) + 3);  // E
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title("Max Speed (mm/s)"); // TODO: GET_TEXT_F
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(1), F("Max Feedrate X"));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(2), F("Max Feedrate Y"));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(3), F("Max Feedrate Z"));
      #if HAS_HOTEND
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(4), F("Max Feedrate E"));
      #endif
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26); // "Max Speed (mm/s)"

      draw_max_en(MBASE(1));          // "Max"
      DWIN_Frame_AreaCopy(1, 184, 119, 234, 132, LBLX + 27, MBASE(1)); // "Speed X"

      draw_max_en(MBASE(2));          // "Max"
      draw_speed_en(27, MBASE(2));    // "Speed"
      say_y(70, MBASE(2));            // "Y"

      draw_max_en(MBASE(3));          // "Max"
      draw_speed_en(27, MBASE(3));    // "Speed"
      say_z(70, MBASE(3));            // "Z"

      #if HAS_HOTEND
        draw_max_en(MBASE(4));        // "Max"
        draw_speed_en(27, MBASE(4));  // "Speed"
        say_e(70, MBASE(4));          // "E"
      #endif
    #endif
  }

  Draw_Back_First();
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MaxSpeedX + i);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(1), planner.settings.max_feedrate_mm_s[X_AXIS]);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(2), planner.settings.max_feedrate_mm_s[Y_AXIS]);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(3), planner.settings.max_feedrate_mm_s[Z_AXIS]);
  #if HAS_HOTEND
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(4), planner.settings.max_feedrate_mm_s[E_AXIS]);
  #endif
}

inline void Draw_Max_Accel_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Acceleration"

    DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(1));
    DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(1) + 1);
    DWIN_Frame_AreaCopy(1, 229, 133, 236, 147, LBLX + 71, MBASE(1));   // Max acceleration X
    DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(2));
    DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(2) + 1);
    DWIN_Frame_AreaCopy(1, 1, 150, 7, 160, LBLX + 71, MBASE(2) + 2);   // Max acceleration Y
    DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(3));
    DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(3) + 1);
    DWIN_Frame_AreaCopy(1, 9, 150, 16, 160, LBLX + 71, MBASE(3) + 2);  // Max acceleration Z
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(4));
      DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(4) + 1);
      DWIN_Frame_AreaCopy(1, 18, 150, 25, 160, LBLX + 71, MBASE(4) + 2); // Max acceleration E
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_ACCELERATION));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(1), F("Max Accel X"));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(2), F("Max Accel Y"));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(3), F("Max Accel Z"));
      #if HAS_HOTEND
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(4), F("Max Accel E"));
      #endif
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26);          // "Acceleration"
      draw_max_accel_en(MBASE(1)); say_x(108, MBASE(1));  // "Max Acceleration X"
      draw_max_accel_en(MBASE(2)); say_y(108, MBASE(2));  // "Max Acceleration Y"
      draw_max_accel_en(MBASE(3)); say_z(108, MBASE(3));  // "Max Acceleration Z"
      #if HAS_HOTEND
        draw_max_accel_en(MBASE(4)); say_e(108, MBASE(4)); // "Max Acceleration E"
      #endif
    #endif
    draw_max_accel_en(MBASE(1)); say_x(24 + 78 + 6, MBASE(1)); // "Max Acceleration X"
    draw_max_accel_en(MBASE(2)); say_y(24 + 78 + 6, MBASE(2)); // "Max Acceleration Y"
    draw_max_accel_en(MBASE(3)); say_z(24 + 78 + 6, MBASE(3)); // "Max Acceleration Z"
    draw_max_accel_en(MBASE(4)); say_e(24 + 78 + 6, MBASE(4)); // "Max Acceleration E"
  }

  Draw_Back_First();
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MaxAccX + i);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(1), planner.settings.max_acceleration_mm_per_s2[X_AXIS]);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(2), planner.settings.max_acceleration_mm_per_s2[Y_AXIS]);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(3), planner.settings.max_acceleration_mm_per_s2[Z_AXIS]);
  #if HAS_HOTEND
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(4), planner.settings.max_acceleration_mm_per_s2[E_AXIS]);
  #endif
}

#if HAS_CLASSIC_JERK
  inline void Draw_Max_Jerk_Menu() {
    Clear_Main_Window();

    if (HMI_IsChinese()) {
      DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Jerk"

      DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX     , MBASE(1));
      DWIN_Frame_AreaCopy(1,   1, 180,  28, 192, LBLX + 27, MBASE(1) + 1);
      DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 53, MBASE(1));
      DWIN_Frame_AreaCopy(1, 229, 133, 236, 147, LBLX + 83, MBASE(1));        // Max Jerk speed X
      DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX     , MBASE(2));
      DWIN_Frame_AreaCopy(1,   1, 180,  28, 192, LBLX + 27, MBASE(2) + 1);
      DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 53, MBASE(2));
      DWIN_Frame_AreaCopy(1,   1, 150,   7, 160, LBLX + 83, MBASE(2) + 3);    // Max Jerk speed Y
      DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX     , MBASE(3));
      DWIN_Frame_AreaCopy(1,   1, 180,  28, 192, LBLX + 27, MBASE(3) + 1);
      DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 53, MBASE(3));
      DWIN_Frame_AreaCopy(1,   9, 150,  16, 160, LBLX + 83, MBASE(3) + 3);    // Max Jerk speed Z
      #if HAS_HOTEND
        DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX     , MBASE(4));
        DWIN_Frame_AreaCopy(1,   1, 180,  28, 192, LBLX + 27, MBASE(4) + 1);
        DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 53, MBASE(4));
        DWIN_Frame_AreaCopy(1,  18, 150,  25, 160, LBLX + 83, MBASE(4) + 3);  // Max Jerk speed E
      #endif
    }
    else {
      #ifdef USE_STRING_HEADINGS
        Draw_Title(GET_TEXT_F(MSG_JERK));
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(1), F("Max Jerk X"));
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(2), F("Max Jerk Y"));
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(3), F("Max Jerk Z"));
        #if HAS_HOTEND
          DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(4), F("Max Jerk E"));
        #endif
      #else
        DWIN_Frame_TitleCopy(1, 144, 16, 189, 26); // "Jerk"
        draw_max_en(MBASE(1));          // "Max"
        draw_jerk_en(MBASE(1));         // "Jerk"
        draw_speed_en(72, MBASE(1));    // "Speed"
        say_x(115, MBASE(1));           // "X"

        draw_max_en(MBASE(2));          // "Max"
        draw_jerk_en(MBASE(2));         // "Jerk"
        draw_speed_en(72, MBASE(2));    // "Speed"
        say_y(115, MBASE(2));           // "Y"

        draw_max_en(MBASE(3));          // "Max"
        draw_jerk_en(MBASE(3));         // "Jerk"
        draw_speed_en(72, MBASE(3));    // "Speed"
        say_z(115, MBASE(3));           // "Z"

        #if HAS_HOTEND
          draw_max_en(MBASE(4));        // "Max"
          draw_jerk_en(MBASE(4));       // "Jerk"
          draw_speed_en(72, MBASE(4));  // "Speed"
          say_e(115, MBASE(4));         // "E"
        #endif
      #endif
    }

    Draw_Back_First();
    LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MaxSpeedJerkX + i);
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(1), planner.max_jerk[X_AXIS] * MINUNITMULT);
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(2), planner.max_jerk[Y_AXIS] * MINUNITMULT);
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(3), planner.max_jerk[Z_AXIS] * MINUNITMULT);
    #if HAS_HOTEND
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(4), planner.max_jerk[E_AXIS] * MINUNITMULT);
    #endif
  }
#endif

inline void Draw_Steps_Menu() {
  Clear_Main_Window();

  if (HMI_IsChinese()) {
    DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Steps per mm"

    DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(1));
    DWIN_Frame_AreaCopy(1, 229, 133, 236, 147, LBLX + 44, MBASE(1)); // Transmission Ratio X
    DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(2));
    DWIN_Frame_AreaCopy(1, 1, 150, 7, 160, LBLX + 44, MBASE(2) + 3); // Transmission Ratio Y
    DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(3));
    DWIN_Frame_AreaCopy(1, 9, 150, 16, 160, LBLX + 44, MBASE(3) + 3); // Transmission Ratio Z
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(4));
      DWIN_Frame_AreaCopy(1, 18, 150, 25, 160, LBLX + 44, MBASE(4) + 3); // Transmission Ratio E
    #endif
  }
  else {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_STEPS_PER_MM));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(1), F("Steps/mm X"));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(2), F("Steps/mm Y"));
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(3), F("Steps/mm Z"));
      #if HAS_HOTEND
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(4), F("Steps/mm E"));
      #endif
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26); // "Steps per mm"
      draw_steps_per_mm(MBASE(1)); say_x(103, MBASE(1)); // "Steps-per-mm X"
      draw_steps_per_mm(MBASE(2)); say_y(103, MBASE(2)); // "Y"
      draw_steps_per_mm(MBASE(3)); say_z(103, MBASE(3)); // "Z"
      #if HAS_HOTEND
        draw_steps_per_mm(MBASE(4)); say_e(103, MBASE(4)); // "E"
      #endif
    #endif
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(1), (char*)"Steps per (mm) X");
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(2), (char*)"Steps per (mm) Y");
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(3), (char*)"Steps per (mm) Z");
	DWIN_Draw_String(false, true, font8x16, White, Background_black, LBLX, MBASE(4), (char*)"Steps per (mm) E");
  }

  Draw_Back_First();
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_StepX + i);
  DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(1), planner.settings.axis_steps_per_mm[X_AXIS] * MINUNITMULT);
  DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(2), planner.settings.axis_steps_per_mm[Y_AXIS] * MINUNITMULT);
  DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(3), planner.settings.axis_steps_per_mm[Z_AXIS] * MINUNITMULT);
  #if HAS_HOTEND
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, 210, MBASE(4), planner.settings.axis_steps_per_mm[E_AXIS] * MINUNITMULT);
  #endif
}

/* Motion */
void HMI_Motion() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_motion.inc(1 + MOTION_CASE_TOTAL)) Move_Highlight(1, select_motion.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_motion.dec()) Move_Highlight(-1, select_motion.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_motion.now) {
      case 0: // Back
        checkkey = Control;
        select_control.set(CONTROL_CASE_MOVE);
        index_control = MROWS;
        Draw_Control_Menu();
        break;
      case MOTION_CASE_RATE:   // Max speed
        checkkey = MaxSpeed;
        select_speed.reset();
        Draw_Max_Speed_Menu();
        break;
      case MOTION_CASE_ACCEL:  // Max acceleration
        checkkey = MaxAcceleration;
        select_acc.reset();
        Draw_Max_Accel_Menu();
        break;
      #if HAS_CLASSIC_JERK
        case MOTION_CASE_JERK: // Max jerk
          checkkey = MaxJerk;
          select_jerk.reset();
          Draw_Max_Jerk_Menu();
         break;
      #endif
      case MOTION_CASE_STEPS:  // Steps per mm
        checkkey = Step;
        select_step.reset();
        Draw_Steps_Menu();
        break;
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

/* homeing menu*/
void HMI_HomingMenu(void) {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_homeMenu.inc(4)) Move_Highlight(1, select_homeMenu.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_homeMenu.dec()) Move_Highlight(-1, select_homeMenu.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_homeMenu.now) {
      case 0: // back
	    checkkey = Prepare;
        select_prepare.set(3);
        Draw_Prepare_Menu();
        break;
      case 1: // Home All
	    checkkey = Last_Prepare;
        queue.inject_P(PSTR("G28"));
        Popup_Window_Home();
        break;
      case 2: // Home X
        checkkey = Last_Prepare;
		queue.inject_P(PSTR("G28 X")); 
        Popup_Window_Home();
        break;
      case 3: // Home Y
	    checkkey = Last_Prepare;
        queue.inject_P(PSTR("G28 Y")); 
        Popup_Window_Home();
        break;
      case 4: // Home Z
	    checkkey = Last_Prepare;
        queue.inject_P(PSTR("G28 Z"));
        Popup_Window_Home();
        break;
    }
  }
  DWIN_UpdateLCD();
}

/* Info */
void HMI_Info() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER) {
    #if HAS_LEVELING
      checkkey = Control;
      select_control.set(CONTROL_CASE_INFO);
      Draw_Control_Menu();
    #else
      select_page.set(3);
      Goto_MainMenu();
    #endif
  }
  DWIN_UpdateLCD();
}

//bltouch menu
void HMI_BlTouch_Menu(void) {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_bltm.inc(4)) Move_Highlight(1, select_bltm.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_bltm.dec()) Move_Highlight(-1, select_bltm.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_bltm.now) {
      case 0: // back
			checkkey = Control;
			select_control.set(6);
			Draw_Control_Menu();
			break;
        break;
      case 1: // bltouch alarm release
		  queue.inject_P(PSTR("M280 P0 S160"));
        break;
      case 2: // bltouch self test
		  queue.inject_P(PSTR("M280 P0 S120\nG4 P1000\nM280 P0 S160"));
        break;
      case 3: // pin down
		  queue.inject_P(PSTR("M280 P0 S10"));
		break;
	  case 4: // pin up
		  queue.inject_P(PSTR("M280 P0 S90"));
		break;
     
    }
  }
  DWIN_UpdateLCD();
}

/* Tune */
void HMI_Tune() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  #if EITHER(HAS_BED_PROBE, BABYSTEPPING)
	int tuneN = 5;
  #else
	int tuneN = 4;
  #endif
  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_tune.inc(1 + TUNE_CASE_TOTAL)) {
      if (select_tune.now > MROWS && select_tune.now > index_tune) {
        index_tune = select_tune.now;
        Scroll_Menu(DWIN_SCROLL_UP);
      }
      else {
        Move_Highlight(1, select_tune.now + MROWS - index_tune);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
	if (select_page.now != 1 || ExtPrint_flag != 2){
	  if (select_tune.dec()) {
        if (select_tune.now < index_tune - MROWS) {
          index_tune--;
          Scroll_Menu(DWIN_SCROLL_DOWN);
		  if (ExtPrint_flag != 2){
            if (index_tune == MROWS) Draw_Back_First();
		  }
        }
        else {
          Move_Highlight(-1, select_tune.now + MROWS - index_tune);
        }
      }
	}
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_tune.now) {
      case 0: { // Back
	    if (ExtPrint_flag == 2){
		  
		}
		else{
          select_print.set(0);
          Goto_PrintProcess();
		}
      }
      break;
      case TUNE_CASE_SPEED: // Print speed
        checkkey = PrintSpeed;
        HMI_ValueStruct.print_speed = feedrate_percentage;
        DWIN_Draw_IntValue(true, true, 0, font8x16, White, Select_Color, 3, 216, MBASE(1 + MROWS - index_tune), feedrate_percentage);
        EncoderRate.encoderRateEnabled = 1;
        break;
        #if HAS_HOTEND
          case 2: // Nozzle temp
            checkkey = ETemp;
            HMI_ValueStruct.E_Temp = thermalManager.temp_hotend[0].target;
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Select_Color, 3, 216, MBASE(2 + MROWS - index_tune), thermalManager.temp_hotend[0].target);
            EncoderRate.encoderRateEnabled = 1;
            break;
        #endif
        #if HAS_HEATED_BED
          case 3: // Bed temp
            checkkey = BedTemp;
            HMI_ValueStruct.Bed_Temp = thermalManager.temp_bed.target;
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Select_Color, 3, 216, MBASE(3 + MROWS - index_tune), thermalManager.temp_bed.target);
            EncoderRate.encoderRateEnabled = 1;
            break;
        #endif
        #if HAS_FAN
          case 4: // Fan speed
            checkkey = FanSpeed;
            HMI_ValueStruct.Fan_speed = thermalManager.fan_speed[0];
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Select_Color, 3, 216, MBASE(4 + MROWS - index_tune), thermalManager.fan_speed[0]);
            EncoderRate.encoderRateEnabled = 1;
            break;
        #endif
	  #if EITHER(HAS_BED_PROBE, BABYSTEPPING)
      case 5: // Z-offset
        checkkey = LiveTune;
        HMI_ValueStruct.offset_value = BABY_Z_VAR * 100;
        show_plus_or_minus(font8x16, Select_Color, 2, 2, 202, MBASE(5 + MROWS - index_tune), HMI_ValueStruct.offset_value);
        EncoderRate.encoderRateEnabled = 1;
        break;
	  #endif
      default: break;
    }
    DWIN_UpdateLCD();
  }

  /* ABS Preheat */
  void HMI_ABSPreheatSetting() {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW) {
      if (select_ABS.inc(1 + PREHEAT_CASE_TOTAL)) Move_Highlight(1, select_ABS.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW) {
      if (select_ABS.dec()) Move_Highlight(-1, select_ABS.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) {
      switch (select_ABS.now) {
        case 0: // Back
          checkkey = TemperatureID;
          select_temp.now = TEMP_CASE_ABS;
          HMI_ValueStruct.show_mode = -1;
          Draw_Temperature_Menu();
          break;
        #if HAS_HOTEND
          case PREHEAT_CASE_TEMP: // Set nozzle temperature
            checkkey = ETemp;
            HMI_ValueStruct.E_Temp = ui.material_preset[1].hotend_temp;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(PREHEAT_CASE_TEMP), ui.material_preset[1].hotend_temp);
            EncoderRate.enabled = true;
            break;
        #endif
        #if HAS_HEATED_BED
          case PREHEAT_CASE_BED: // Set bed temperature
            checkkey = BedTemp;
            HMI_ValueStruct.Bed_Temp = ui.material_preset[1].bed_temp;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 216, MBASE(PREHEAT_CASE_BED), ui.material_preset[1].bed_temp);
            EncoderRate.enabled = true;
            break;
        #endif
        #if HAS_FAN
          case PREHEAT_CASE_FAN: // Set fan speed
            checkkey = FanSpeed;
            HMI_ValueStruct.Fan_speed = ui.material_preset[mat].fan_speed;
            DWIN_Draw_IntValue(true, true, 0, font8x16, White, Select_Color, 3, 216, MBASE(3), ui.material_preset[mat].fan_speed);
            EncoderRate.encoderRateEnabled = 1;
            break;
        #endif
      case 4: // save PLA configuration
        if (settings.save()) {
          buzzer.tone(100, 659);
          buzzer.tone(100, 698);
        }
        else
          buzzer.tone(20, 440);
        break;
      default:
        break;
    }
    DWIN_UpdateLCD();
  }

#endif

/* Max Speed */
void HMI_MaxSpeed() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_speed.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_speed.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_speed.dec()) Move_Highlight(-1, select_speed.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (WITHIN(select_speed.now, 1, 4)) {
      checkkey = MaxSpeed_value;
      HMI_flag.feedspeed_axis = AxisEnum(select_speed.now - 1);
      HMI_ValueStruct.Max_Feedspeed = planner.settings.max_feedrate_mm_s[HMI_flag.feedspeed_axis];
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, 210, MBASE(select_speed.now), HMI_ValueStruct.Max_Feedspeed);
      EncoderRate.enabled = true;
    }
    else { // Back
      checkkey = Motion;
      select_motion.now = MOTION_CASE_RATE;
      Draw_Motion_Menu();
    }
  }
  DWIN_UpdateLCD();
}

/* Max Acceleration */
void HMI_MaxAcceleration() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_acc.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_acc.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_acc.dec()) Move_Highlight(-1, select_acc.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (WITHIN(select_acc.now, 1, 4)) {
      checkkey = MaxAcceleration_value;
      HMI_flag.acc_axis = AxisEnum(select_acc.now - 1);
      HMI_ValueStruct.Max_Acceleration = planner.settings.max_acceleration_mm_per_s2[HMI_flag.acc_axis];
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, 210, MBASE(select_acc.now), HMI_ValueStruct.Max_Acceleration);
      EncoderRate.enabled = true;
    }
    else { // Back
      checkkey = Motion;
      select_motion.now = MOTION_CASE_ACCEL;
      Draw_Motion_Menu();
    }
  }
  DWIN_UpdateLCD();
}

#if HAS_CLASSIC_JERK
  /* Max Jerk */
  void HMI_MaxJerk() {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW) {
      if (select_jerk.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_jerk.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW) {
      if (select_jerk.dec()) Move_Highlight(-1, select_jerk.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) {
      if (WITHIN(select_jerk.now, 1, 4)) {
        checkkey = MaxJerk_value;
        HMI_flag.jerk_axis = AxisEnum(select_jerk.now - 1);
        HMI_ValueStruct.Max_Jerk = planner.max_jerk[HMI_flag.jerk_axis] * MINUNITMULT;
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, 210, MBASE(select_jerk.now), HMI_ValueStruct.Max_Jerk);
        EncoderRate.enabled = true;
      }
      else { // Back
        checkkey = Motion;
        select_motion.now = MOTION_CASE_JERK;
        Draw_Motion_Menu();
      }
    }
    DWIN_UpdateLCD();
  }
#endif // HAS_CLASSIC_JERK

/* Step */
void HMI_Step() {
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_step.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_step.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_step.dec()) Move_Highlight(-1, select_step.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    if (WITHIN(select_step.now, 1, 4)) {
      checkkey = Step_value;
      HMI_flag.step_axis = AxisEnum(select_step.now - 1);
      HMI_ValueStruct.Max_Step = planner.settings.axis_steps_per_mm[HMI_flag.step_axis] * MINUNITMULT;
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, 210, MBASE(select_step.now), HMI_ValueStruct.Max_Step);
      EncoderRate.enabled = true;
    }
    else { // Back
      checkkey = Motion;
      select_motion.now = MOTION_CASE_STEPS;
      Draw_Motion_Menu();
    }
  }
  DWIN_UpdateLCD();
}

void HMI_Init() {
  HMI_SDCardInit();

  for (uint16_t t = 0; t <= 100; t += 2) {
    DWIN_ICON_Show(ICON, ICON_Bar, 15, 260);
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 15 + t * 242 / 100, 260, 257, 280);
    DWIN_UpdateLCD();
    delay(20);
  }

  lcd_select_language();

  delay(200);
}

void DWIN_Update() {
  EachMomentUpdate();   // Status update
  HMI_SDCardUpdate();   // SD card update
  DWIN_HandleScreen();  // Rotary encoder update
}

void EachMomentUpdate() {
  static millis_t next_rts_update_ms = 0;
  const millis_t ms = millis();
  if (PENDING(ms, next_rts_update_ms)) return;
  next_rts_update_ms = ms + DWIN_SCROLL_UPDATE_INTERVAL;

  // variable update
  update_variable();
  
  // launch tune menu during external launched print
  external_print_tune();

  if (checkkey == PrintProcess) {
    // if print done
    if (HMI_flag.print_finish && !HMI_flag.done_confirm_flag) {
      HMI_flag.print_finish = false;
      HMI_flag.done_confirm_flag = true;

      TERN_(POWER_LOSS_RECOVERY, recovery.cancel());

      planner.finish_and_disable();

      // show percent bar and value
      Percentrecord = 0;
      Draw_Print_ProgressBar();

      // show print done confirm
      DWIN_Draw_Rectangle(1, Background_black, 0, 250,  271, 360);
      DWIN_ICON_Show(ICON, HMI_flag.language_flag ? ICON_Confirm_C : ICON_Confirm_E, 86, 302 - 19);
	  
	  //set flag high
	  ExtPrint_flag = 5;
	  queue.inject_P(PSTR("M77"));
    }
    else if (HMI_flag.pause_flag != printingIsPaused()) {
      // print status update
      HMI_flag.pause_flag = printingIsPaused();
      if (HMI_flag.pause_flag) ICON_Continue(); else ICON_Pause();
    }
  }

  // pause after homing
  if (HMI_flag.pause_action && printingIsPaused() && !planner.has_blocks_queued()) {
    HMI_flag.pause_action = false;
    #if ENABLED(PAUSE_HEAT)
      #if HAS_HEATED_BED
        tempbed = thermalManager.temp_bed.target;
      #endif
      #if HAS_HOTEND
        temphot = thermalManager.temp_hotend[0].target;
      #endif
      thermalManager.disable_all_heaters();
    #endif
    queue.inject_P(PSTR("G1 F1200 X0 Y0"));
  }

  if (card.isPrinting() && checkkey == PrintProcess) { // print process
    const uint8_t card_pct = card.percentDone();
    static uint8_t last_cardpercentValue = 101;
    if (last_cardpercentValue != card_pct) { // print percent
      last_cardpercentValue = card_pct;
      if (card_pct) {
        Percentrecord = card_pct;
        Draw_Print_ProgressBar();
      }
    }

    duration_t elapsed = print_job_timer.duration(); // print timer

    // Print time so far
    static uint16_t last_Printtime = 0;
    const uint16_t min = (elapsed.value % 3600) / 60;
    if (last_Printtime != min) { // 1 minute update
      last_Printtime = min;
      Draw_Print_ProgressElapsed();
    }

    // Estimate remaining time every 20 seconds
    static millis_t next_remain_time_update = 0;
    if (Percentrecord > 1 && ELAPSED(ms, next_remain_time_update) && !HMI_flag.heat_flag) {
      remain_time = (elapsed.value - dwin_heat_time) / (Percentrecord * 0.01f) - (elapsed.value - dwin_heat_time);
      next_remain_time_update += 20 * 1000UL;
      Draw_Print_ProgressRemain();
    }
  }
  else if (dwin_abort_flag && !HMI_flag.home_flag) { // Print Stop
    dwin_abort_flag = false;
    HMI_ValueStruct.print_speed = feedrate_percentage = 100;
	#if HAS_BED_PROBE
    zprobe_zoffset = TERN(HAS_LEVELING, probe.offset.z, 0);
	#endif

    planner.finish_and_disable();

    #if DISABLED(SD_ABORT_NO_COOLDOWN)
      thermalManager.disable_all_heaters();
    #endif

    select_page.set(0);
    Goto_MainMenu();
  }
  else if (DWIN_lcd_sd_status && recovery.dwin_flag) { // resume print before power off
    recovery.dwin_flag = false;

    recovery.load();
    if (!recovery.valid()) return recovery.purge();

    auto draw_first_option = [](const bool sel) {
      const uint16_t c1 = sel ? Background_window : Select_Color;
      DWIN_Draw_Rectangle(0, c1, 25, 306, 126, 345);
      DWIN_Draw_Rectangle(0, c1, 24, 305, 127, 346);
    };

    auto update_selection = [&](const bool sel) {
      HMI_flag.select_flag = sel;
      draw_first_option(sel);
      const uint16_t c2 = sel ? Select_Color : Background_window;
      DWIN_Draw_Rectangle(0, c2, 145, 306, 246, 345);
      DWIN_Draw_Rectangle(0, c2, 144, 305, 247, 346);
    };

    const uint16_t fileCnt = card.get_num_Files();
    for (uint16_t i = 0; i < fileCnt; i++) {
      // TODO: Resume print via M1000 then update the UI
      // with the active filename which can come from CardReader.
      card.getfilename_sorted(SD_ORDER(i, fileCnt));
      if (!strcmp(card.filename, &recovery.info.sd_filename[1])) { // Resume print before power failure while have the same file
        recovery_flag = 1;
        HMI_flag.select_flag = 1;
        Popup_Window_Resume();
        draw_first_option(false);
        char * const name = card.longest_filename();
        DWIN_Draw_String(false, true, font8x16, Font_window, Background_window, (DWIN_WIDTH - strlen(name) * MENU_CHR_W) / 2, 252, name);
        DWIN_UpdateLCD();
        break;
      }
    }

      while (recovery_flag) {
        ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
        if (encoder_diffState != ENCODER_DIFF_NO) {
          if (encoder_diffState == ENCODER_DIFF_ENTER) {
            recovery_flag = false;
            if (HMI_flag.select_flag) break;
            TERN_(POWER_LOSS_RECOVERY, queue.inject_P(PSTR("M1000C")));
            HMI_StartFrame(true);
            return;
          }
          else
            update_selection(encoder_diffState == ENCODER_DIFF_CW);

          DWIN_UpdateLCD();
        }
      }

      select_print.set(0);
      HMI_ValueStruct.show_mode = 0;
      queue.inject_P(PSTR("M1000"));
      Goto_PrintProcess();
      Draw_Status_Area(true);
    }
  #endif
  DWIN_UpdateLCD();
}

void DWIN_HandleScreen() {
  switch (checkkey) {
    case MainMenu:        HMI_MainMenu(); break;
    case SelectFile:      HMI_SelectFile(); break;
    case Prepare:         HMI_Prepare(); break;
    case Control:         HMI_Control(); break;
    case Leveling:        break;
    case PrintProcess:    HMI_Printing(); break;
    case Print_window:    HMI_PauseOrStop(); break;
    case AxisMove:        HMI_AxisMove(); break;
    case TemperatureID:   HMI_Temperature(); break;
    case Motion:          HMI_Motion(); break;
    case Info:            HMI_Info(); break;
    case Tune:            HMI_Tune(); break;
    #if HAS_PREHEAT
      case PLAPreheat:    HMI_PLAPreheatSetting(); break;
      case ABSPreheat:    HMI_ABSPreheatSetting(); break;
    #endif
    case MaxSpeed:        HMI_MaxSpeed(); break;
    case MaxAcceleration: HMI_MaxAcceleration(); break;
    #if HAS_CLASSIC_JERK
      case MaxJerk:       HMI_MaxJerk(); break;
    #endif
    case Step:            HMI_Step(); break;
    case Move_X:          HMI_Move_X(); break;
    case Move_Y:          HMI_Move_Y(); break;
    case Move_Z:          HMI_Move_Z(); break;
    #if HAS_HOTEND
      case Extruder:      HMI_Move_E(); break;
      case ETemp:         HMI_ETemp(); break;
    #endif
    #if EITHER(HAS_BED_PROBE, BABYSTEPPING)
      case Homeoffset:    HMI_Zoffset(); break;
    #endif
    #if HAS_HEATED_BED
      case BedTemp:       HMI_BedTemp(); break;
    #endif
    #if HAS_PREHEAT
      case FanSpeed:      HMI_FanSpeed(); break;
    #endif
    case PrintSpeed:      HMI_PrintSpeed(); break;
    case MaxSpeed_value:  HMI_MaxFeedspeedXYZE(); break;
    case MaxAcceleration_value: HMI_MaxAccelerationXYZE(); break;
    #if HAS_CLASSIC_JERK
      case MaxJerk_value: HMI_MaxJerkXYZE(); break;
    #endif
    case Step_value:      HMI_StepXYZE(); break;
    default: break;
  }
}

void DWIN_CompletedHoming() {
  HMI_flag.home_flag = false;
  dwin_zoffset = TERN0(HAS_BED_PROBE, probe.offset.z);
  if (checkkey == Last_Prepare) {
    checkkey = Prepare;
    select_prepare.now = PREPARE_CASE_HOME;
    index_prepare = MROWS;
    Draw_Prepare_Menu();
  }
  else if (checkkey == Back_Main) {
    HMI_ValueStruct.print_speed = feedrate_percentage = 100;
    planner.finish_and_disable();
    Goto_MainMenu();
  }
}

void DWIN_CompletedLeveling() {
  if (checkkey == Leveling) Goto_MainMenu();
}
#endif // DWIN_CREALITY_LCD
